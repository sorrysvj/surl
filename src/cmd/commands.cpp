#include "surl/cmd/commands.hpp"

#include "surl/cli/help.hpp"
#include "surl/mirror/cache.hpp"
#include "surl/mirror/crawler.hpp"
#include "surl/mirror/pathmap.hpp"
#include "surl/net/http_client.hpp"
#include "surl/util/fsutil.hpp"
#include "surl/util/json.hpp"
#include "surl/util/log.hpp"
#include "surl/util/paths.hpp"
#include "surl/util/strings.hpp"
#include "surl/version.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace surl {
namespace {

/// Accepts "example.com" as shorthand for "https://example.com".
bool normalise_target(const std::string& raw, Url& out, std::string& error) {
    if (raw.empty()) {
        error = "no URL given. Try: surl https://example.com -d ./website";
        return false;
    }

    std::string text = raw;
    if (!istarts_with(text, "http://") && !istarts_with(text, "https://")) {
        if (text.find("://") != std::string::npos) {
            error = "only http and https URLs can be mirrored, got '" + raw + "'";
            return false;
        }
        text = "https://" + text;
    }

    if (!parse_url(text, out) || !out.is_http() || out.host.empty()) {
        error = "'" + raw + "' is not a usable URL";
        return false;
    }
    return true;
}

Json crawl_result_to_json(const CrawlResult& result, const Url& source,
                          const fs::path& directory) {
    Json root = Json::object();
    root.set("source", Json(source.to_string_no_fragment()));
    root.set("directory", Json(path_to_utf8(directory)));
    root.set("downloaded", Json(result.stats.downloaded));
    root.set("reused", Json(result.stats.reused));
    root.set("unchanged", Json(result.stats.unchanged));
    root.set("skipped", Json(result.stats.skipped));
    root.set("failed", Json(result.stats.failed));
    root.set("pages", Json(result.stats.pages));
    root.set("assets", Json(result.stats.assets));
    root.set("bytes", Json(result.stats.bytes));
    root.set("elapsed_ms", Json(result.stats.elapsed_ms));
    if (!result.stop_reason.empty()) root.set("stopped_early", Json(result.stop_reason));
    return root;
}

} // namespace

bool resolve_mirror_directory(const Options& options, fs::path& out, std::string& error) {
    out = options.directory.empty() ? fs::current_path() : options.directory;

    std::error_code ec;
    if (!fs::exists(out, ec)) {
        error = "no such directory: " + path_to_utf8(out);
        return false;
    }
    if (!fs::is_directory(out, ec)) {
        error = path_to_utf8(out) + " is not a directory";
        return false;
    }
    return true;
}

int cmd_clone(const Options& options) {
    Url start;
    std::string error;
    if (!normalise_target(options.url, start, error)) {
        log().error(error);
        return kExitUsage;
    }

    if (!options.allow_private_hosts && is_private_or_loopback_host(start.host)) {
        log().error("refusing to mirror '" + start.host +
                    "': it resolves to this machine or a private network. "
                    "Pass --allow-private if that is really what you want.");
        return kExitUsage;
    }

    if (options.insecure) {
        log().warn("TLS certificate verification is disabled (--insecure); "
                   "the connection can be intercepted");
    }

    const fs::path directory =
        options.directory.empty() ? default_output_directory(start) : options.directory;

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

    Manifest manifest;
    const fs::path manifest_path = Manifest::default_path(directory);
    if (!options.force) {
        if (!manifest.load(manifest_path, error)) {
            log().error(error);
            return kExitError;
        }
    }
    manifest.set_source(start.to_string_no_fragment());

    PathMapConfig map_config;
    map_config.root = directory;
    map_config.primary = start;

    if (!options.json_output && !options.list_only) {
        log().info(log().paint(color::kBold, "surl") + " " + start.to_string_no_fragment() +
                   " -> " + path_to_utf8(directory));
        if (options.dry_run) log().info("(dry run: nothing will be written)");
    }

    Crawler crawler(options, *http, manifest, PathMapper(map_config), directory);
    const CrawlResult result = crawler.run(start);

    if (!result.ok()) {
        log().error(result.fatal_error);
        return kExitError;
    }

    if (!options.dry_run) {
        std::string save_error;
        if (!manifest.save(manifest_path, save_error)) {
            log().warn("could not write the manifest: " + save_error);
        }
    }

    if (options.json_output) {
        log().out(crawl_result_to_json(result, start, directory).dump(2));
    } else if (!options.list_only) {
        const CrawlStats& stats = result.stats;
        log().info("");
        log().info(log().paint(color::kGreen, "done") + "  " +
                   std::to_string(stats.downloaded) + " downloaded, " +
                   std::to_string(stats.reused + stats.unchanged) + " cached, " +
                   std::to_string(stats.skipped) + " skipped, " +
                   std::to_string(stats.failed) + " failed");
        log().info("      " + human_size(stats.bytes) + " in " +
                   human_duration_ms(stats.elapsed_ms) + "  ->  " + path_to_utf8(directory));
        if (!result.stop_reason.empty()) {
            log().warn("stopped early: " + result.stop_reason);
        }
    }

    if (result.stats.failed > 0) return kExitPartial;
    return kExitOk;
}

// ---------------------------------------------------------------------------
// PATH integration
// ---------------------------------------------------------------------------

#ifdef _WIN32
namespace {

constexpr const wchar_t* kEnvironmentKey = L"Environment";

/// Reads the user's PATH without expanding it, so that entries written as
/// %LOCALAPPDATA%\... stay symbolic instead of being frozen.
bool read_user_path(std::wstring& value, DWORD& type, std::string& error) {
    HKEY key = nullptr;
    LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, kEnvironmentKey, 0, KEY_READ, &key);
    if (status != ERROR_SUCCESS) {
        error = "could not open HKCU\\Environment";
        return false;
    }

    DWORD size = 0;
    type = REG_SZ;
    status = RegQueryValueExW(key, L"Path", nullptr, &type, nullptr, &size);
    if (status == ERROR_FILE_NOT_FOUND) {
        value.clear();
        type = REG_EXPAND_SZ;
        RegCloseKey(key);
        return true;
    }
    if (status != ERROR_SUCCESS) {
        RegCloseKey(key);
        error = "could not read the user PATH value";
        return false;
    }

    std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, L"Path", nullptr, &type,
                              reinterpret_cast<LPBYTE>(buffer.data()), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        error = "could not read the user PATH value";
        return false;
    }
    buffer.resize(wcslen(buffer.c_str()));
    value = buffer;
    return true;
}

bool write_user_path(const std::wstring& value, DWORD type, std::string& error) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kEnvironmentKey, 0, KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS) {
        error = "could not open HKCU\\Environment for writing";
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status =
        RegSetValueExW(key, L"Path", 0, type == REG_SZ ? REG_EXPAND_SZ : type,
                       reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        error = "could not write the user PATH value";
        return false;
    }

    // Tell already-running shells that the environment moved.
    DWORD_PTR unused = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 5000,
                        &unused);
    return true;
}

std::vector<std::wstring> split_path_value(const std::wstring& value) {
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t pos = value.find(L';', start);
        std::wstring piece = (pos == std::wstring::npos) ? value.substr(start)
                                                         : value.substr(start, pos - start);
        if (!piece.empty()) parts.push_back(piece);
        if (pos == std::wstring::npos) break;
        start = pos + 1;
    }
    return parts;
}

/// Compares two PATH entries the way Windows does: case-insensitively, with
/// environment variables expanded and trailing separators ignored.
bool path_entries_equal(const std::wstring& a, const std::wstring& b) {
    const auto normalise = [](std::wstring text) {
        wchar_t expanded[32768];
        const DWORD written =
            ExpandEnvironmentStringsW(text.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
        if (written > 0 && written <= std::size(expanded)) text = expanded;
        while (!text.empty() && (text.back() == L'\\' || text.back() == L'/')) text.pop_back();
        for (wchar_t& c : text) c = towlower(c);
        return text;
    };
    return normalise(a) == normalise(b);
}

} // namespace
#endif // _WIN32

bool add_directory_to_path(const fs::path& directory, bool& already_present,
                           std::string& error) {
    already_present = false;
#ifdef _WIN32
    std::wstring value;
    DWORD type = REG_EXPAND_SZ;
    if (!read_user_path(value, type, error)) return false;

    const std::wstring target = directory.wstring();
    for (const std::wstring& entry : split_path_value(value)) {
        if (path_entries_equal(entry, target)) {
            already_present = true;
            return true;
        }
    }

    std::wstring updated = value;
    if (!updated.empty() && updated.back() != L';') updated.push_back(L';');
    updated += target;
    return write_user_path(updated, type, error);
#else
    (void)directory;
    error = "automatic PATH configuration is only implemented on Windows; "
            "add the directory to your shell profile instead";
    return false;
#endif
}

bool remove_directory_from_path(const fs::path& directory, bool& was_present,
                                std::string& error) {
    was_present = false;
#ifdef _WIN32
    std::wstring value;
    DWORD type = REG_EXPAND_SZ;
    if (!read_user_path(value, type, error)) return false;

    const std::wstring target = directory.wstring();
    std::vector<std::wstring> kept;
    for (const std::wstring& entry : split_path_value(value)) {
        if (path_entries_equal(entry, target)) {
            was_present = true;
            continue;
        }
        kept.push_back(entry);
    }
    if (!was_present) return true;

    std::wstring updated;
    for (std::size_t i = 0; i < kept.size(); ++i) {
        if (i) updated.push_back(L';');
        updated += kept[i];
    }
    return write_user_path(updated, type, error);
#else
    (void)directory;
    error = "automatic PATH configuration is only implemented on Windows";
    return false;
#endif
}

int cmd_install(const Options& options) {
    const fs::path directory = executable_dir();
    bool already_present = false;
    std::string error;

    if (!add_directory_to_path(directory, already_present, error)) {
        log().error(error);
        return kExitError;
    }

    if (already_present) {
        log().info(path_to_utf8(directory) + " is already on your PATH.");
    } else {
        log().info("Added " + path_to_utf8(directory) + " to your user PATH.");
        log().info("Open a new terminal, then run: surl --version");
    }
    (void)options;
    return kExitOk;
}

int cmd_uninstall(const Options& options) {
    const fs::path directory = executable_dir();
    bool was_present = false;
    std::string error;

    if (!remove_directory_from_path(directory, was_present, error)) {
        log().error(error);
        return kExitError;
    }

    if (was_present) {
        log().info("Removed " + path_to_utf8(directory) + " from your user PATH.");
    } else {
        log().info(path_to_utf8(directory) + " was not on your PATH.");
    }
    log().info("");
    log().info("This only changes PATH. To remove an installed copy of SURL, use");
    log().info("Settings -> Apps -> Installed apps -> SURL -> Uninstall.");
    log().info("Nothing you have downloaded with SURL is ever touched.");
    (void)options;
    return kExitOk;
}

// ---------------------------------------------------------------------------
// doctor / config
// ---------------------------------------------------------------------------

int cmd_doctor(const Options& options) {
    const fs::path exe = executable_path();
    const fs::path exe_dir = executable_dir();
    const bool on_path = path_contains(exe_dir);

    Json report = Json::object();
    report.set("version", Json(std::string(SURL_VERSION_STRING)));
    report.set("executable", Json(path_to_utf8(exe)));
    report.set("http_backend", Json(std::string(HttpClient::backend_name())));
    report.set("on_path", Json(on_path));
    report.set("config_dir", Json(path_to_utf8(config_dir())));
    report.set("cache_dir", Json(path_to_utf8(cache_dir())));
    report.set("log_dir", Json(path_to_utf8(log_dir())));

    // A live request confirms DNS, TLS and any proxy actually work.
    HttpClientConfig http_config;
    http_config.user_agent = options.user_agent;
    http_config.proxy = options.proxy;
    http_config.insecure = options.insecure;
    http_config.timeout_ms = 10000;
    http_config.connect_timeout_ms = 10000;

    std::string http_error;
    bool network_ok = false;
    std::string network_note;
    if (std::unique_ptr<HttpClient> http = HttpClient::create(http_config, http_error)) {
        Url probe;
        if (parse_url("https://example.com/", probe)) {
            HttpRequest request;
            request.url = probe;
            request.timeout_ms = 10000;
            request.head_only = true;
            const HttpResponse response = http->send(request);
            network_ok = response.error.empty() && response.status > 0;
            network_note = network_ok ? ("HTTP " + std::to_string(response.status))
                                      : response.error;
        }
    } else {
        network_note = http_error;
    }
    report.set("network_ok", Json(network_ok));
    report.set("network_note", Json(network_note));

    if (options.json_output) {
        log().out(report.dump(2));
        return network_ok ? kExitOk : kExitPartial;
    }

    const auto tick = [&](bool good) {
        return log().paint(good ? color::kGreen : color::kYellow, good ? "ok  " : "warn");
    };

    log().info(log().paint(color::kBold, "SURL " SURL_VERSION_STRING));
    log().info("");
    log().info("  " + tick(true) + "  executable    " + path_to_utf8(exe));
    log().info("  " + tick(true) + "  http backend  " + HttpClient::backend_name());
    log().info("  " + tick(on_path) + "  PATH          " +
               (on_path ? path_to_utf8(exe_dir) + " is on PATH"
                        : path_to_utf8(exe_dir) + " is NOT on PATH (run: surl install)"));
    log().info("  " + tick(network_ok) + "  network       " +
               (network_ok ? "reachable (" + network_note + ")"
                           : "unreachable: " + network_note));
    log().info("");
    log().info("  config   " + path_to_utf8(config_file()));
    log().info("  cache    " + path_to_utf8(cache_dir()));
    log().info("  logs     " + path_to_utf8(log_dir()));

    return network_ok ? kExitOk : kExitPartial;
}

int cmd_config(const Options& options) {
    const fs::path file = config_file();

    const std::string action = options.positional.empty() ? "show" : options.positional[0];

    if (action == "path") {
        log().out(path_to_utf8(file));
        return kExitOk;
    }

    Json root = Json::object();
    std::string text;
    if (read_file(file, text)) {
        std::string parse_error;
        Json parsed;
        if (!Json::parse(text, parsed, parse_error)) {
            log().error("config file at " + path_to_utf8(file) + " is not valid JSON: " +
                        parse_error);
            return kExitError;
        }
        if (parsed.is_object()) root = std::move(parsed);
    }

    if (action == "show") {
        log().out(root.dump(2));
        return kExitOk;
    }

    if (action == "get") {
        if (options.positional.size() < 2) {
            log().error("usage: surl config get <key>");
            return kExitUsage;
        }
        const std::string& key = options.positional[1];
        if (!root.contains(key)) {
            log().error("no such config key: " + key);
            return kExitError;
        }
        const Json& value = root[key];
        log().out(value.is_string() ? value.as_string() : value.dump());
        return kExitOk;
    }

    if (action == "set") {
        if (options.positional.size() < 3) {
            log().error("usage: surl config set <key> <value>");
            return kExitUsage;
        }
        const std::string& key = options.positional[1];
        const std::string& raw = options.positional[2];

        // Store numbers and booleans with their real types so the loader can
        // read them back without guessing.
        Json value;
        if (raw == "true" || raw == "false") {
            value = Json(raw == "true");
        } else {
            std::uint64_t number = 0;
            bool numeric = !raw.empty();
            for (const char c : raw) {
                if (c < '0' || c > '9') {
                    numeric = false;
                    break;
                }
            }
            if (numeric && parse_size(raw, number)) {
                value = Json(number);
            } else {
                value = Json(raw);
            }
        }
        root.set(key, std::move(value));

        std::string write_error;
        if (!ensure_directory(file.parent_path(), write_error) ||
            !write_file_atomic(file, root.dump(2), write_error)) {
            log().error(write_error);
            return kExitError;
        }
        log().info("Set " + key + " in " + path_to_utf8(file));
        return kExitOk;
    }

    if (action == "unset") {
        if (options.positional.size() < 2) {
            log().error("usage: surl config unset <key>");
            return kExitUsage;
        }
        Json rebuilt = Json::object();
        for (const auto& [key, value] : root.fields()) {
            if (key == options.positional[1]) continue;
            rebuilt.set(key, value);
        }
        std::string write_error;
        if (!ensure_directory(file.parent_path(), write_error) ||
            !write_file_atomic(file, rebuilt.dump(2), write_error)) {
            log().error(write_error);
            return kExitError;
        }
        log().info("Removed " + options.positional[1] + " from " + path_to_utf8(file));
        return kExitOk;
    }

    log().error("unknown config action '" + action +
                "'. Use: show, get, set, unset, path");
    return kExitUsage;
}

// ---------------------------------------------------------------------------

int run_command(const Options& options) {
    switch (options.command) {
    case Command::Clone:
    case Command::Mirror:
        return cmd_clone(options);
    case Command::Serve:
        return cmd_serve(options);
    case Command::Inspect:
        return cmd_inspect(options);
    case Command::Check:
        return cmd_check(options);
    case Command::Clean:
        return cmd_clean(options);
    case Command::Doctor:
        return cmd_doctor(options);
    case Command::Config:
        return cmd_config(options);
    case Command::Install:
        return cmd_install(options);
    case Command::Uninstall:
        return cmd_uninstall(options);
    case Command::Update:
        return cmd_update(options);
    case Command::Version:
        if (options.check_only) return cmd_update(options);
        log().out(version_text(options.verbosity >= 2));
        return kExitOk;
    case Command::Help:
        log().out(help_text());
        return kExitOk;
    }
    return kExitUsage;
}

} // namespace surl
