#include "surl/cmd/commands.hpp"

#include "surl/net/http_client.hpp"
#include "surl/util/json.hpp"
#include "surl/util/log.hpp"
#include "surl/util/strings.hpp"
#include "surl/version.hpp"

#include <cstdlib>

namespace surl {
namespace {

struct SemanticVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

/// Parses "1.2.3", tolerating a leading "v" and any pre-release suffix.
bool parse_version(std::string_view text, SemanticVersion& out) {
    std::string s = trim(text);
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) s.erase(s.begin());
    if (s.empty()) return false;

    // Cut off "-rc1" / "+build" so the numeric comparison still works.
    const std::size_t cut = s.find_first_of("-+");
    if (cut != std::string::npos) s = s.substr(0, cut);

    const std::vector<std::string> parts = split(s, '.', false);
    if (parts.empty()) return false;

    const auto to_int = [](const std::string& piece, int& value) {
        if (piece.empty()) return false;
        for (const char c : piece) {
            if (c < '0' || c > '9') return false;
        }
        value = static_cast<int>(std::strtol(piece.c_str(), nullptr, 10));
        return true;
    };

    if (!to_int(parts[0], out.major)) return false;
    if (parts.size() > 1 && !to_int(parts[1], out.minor)) return false;
    if (parts.size() > 2 && !to_int(parts[2], out.patch)) return false;
    return true;
}

int compare(const SemanticVersion& a, const SemanticVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

} // namespace

int cmd_update(const Options& options) {
    const std::string api_url = std::string("https://api.github.com/repos/") +
                                SURL_GITHUB_OWNER + "/" + SURL_GITHUB_REPO +
                                "/releases/latest";

    HttpClientConfig http_config;
    http_config.user_agent = options.user_agent;
    http_config.proxy = options.proxy;
    http_config.insecure = options.insecure;
    http_config.timeout_ms = options.timeout_ms;
    http_config.connect_timeout_ms = std::min<std::uint32_t>(options.timeout_ms, 15000);

    std::string http_error;
    std::unique_ptr<HttpClient> http = HttpClient::create(http_config, http_error);
    if (!http) {
        log().error(http_error);
        return kExitError;
    }

    Url url;
    if (!parse_url(api_url, url)) {
        log().error("could not build the release API URL");
        return kExitError;
    }

    HttpRequest request;
    request.url = url;
    request.timeout_ms = options.timeout_ms;
    request.max_body_bytes = 2 * 1024 * 1024;
    request.headers.emplace_back("Accept", "application/vnd.github+json");
    request.headers.emplace_back("X-GitHub-Api-Version", "2022-11-28");

    const HttpResponse response = http->send(request);
    if (!response.error.empty()) {
        log().error("could not reach GitHub: " + response.error);
        return kExitError;
    }
    if (response.status == 404) {
        log().error("no published releases found for " SURL_GITHUB_OWNER "/" SURL_GITHUB_REPO);
        return kExitError;
    }
    if (!response.ok()) {
        log().error("GitHub returned HTTP " + std::to_string(response.status) +
                    " while checking for updates");
        return kExitError;
    }

    Json release;
    std::string parse_error;
    if (!Json::parse(response.body, release, parse_error)) {
        log().error("could not read the release information: " + parse_error);
        return kExitError;
    }

    const std::string tag = release["tag_name"].as_string_or("");
    const std::string html_url =
        release["html_url"].as_string_or("https://github.com/" SURL_GITHUB_OWNER "/" SURL_GITHUB_REPO
                                         "/releases/latest");

    SemanticVersion latest;
    SemanticVersion current;
    if (!parse_version(tag, latest) || !parse_version(SURL_VERSION_STRING, current)) {
        log().error("could not compare version '" + tag + "' with " SURL_VERSION_STRING);
        return kExitError;
    }

    // Point at the Windows installer asset when the release publishes one.
    std::string installer_url;
    std::string portable_url;
    if (release["assets"].is_array()) {
        for (const Json& asset : release["assets"].items()) {
            const std::string name = asset["name"].as_string_or("");
            const std::string download = asset["browser_download_url"].as_string_or("");
            if (download.empty()) continue;
            if (name.find("installer") != std::string::npos) installer_url = download;
            else if (name.find("portable") != std::string::npos) portable_url = download;
        }
    }

    const int order = compare(current, latest);

    if (options.json_output) {
        Json report = Json::object();
        report.set("current", Json(std::string(SURL_VERSION_STRING)));
        report.set("latest", Json(tag));
        report.set("update_available", Json(order < 0));
        report.set("release_url", Json(html_url));
        if (!installer_url.empty()) report.set("installer_url", Json(installer_url));
        if (!portable_url.empty()) report.set("portable_url", Json(portable_url));
        log().out(report.dump(2));
        return kExitOk;
    }

    if (order >= 0) {
        log().info("surl " SURL_VERSION_STRING " is up to date"
                   + std::string(order > 0 ? " (newer than the latest release, " + tag + ")"
                                           : ""));
        return kExitOk;
    }

    log().info(log().paint(color::kBold, "A newer SURL is available."));
    log().info("");
    log().info("  installed  " SURL_VERSION_STRING);
    log().info("  latest     " + tag);
    log().info("");
    // SURL never updates itself: a CLI that rewrites its own binary behind the
    // user's back is a surprise nobody asked for.
    log().info("  Download it from:");
    log().info("    " + html_url);
    if (!installer_url.empty()) {
        log().info("");
        log().info("  Windows installer:");
        log().info("    " + installer_url);
    }
    if (!portable_url.empty()) {
        log().info("  Portable ZIP:");
        log().info("    " + portable_url);
    }
    log().info("");
    log().info("  SURL does not update itself. Run the installer when you are ready;");
    log().info("  it upgrades in place and keeps your PATH and configuration.");

    return kExitOk;
}

} // namespace surl
