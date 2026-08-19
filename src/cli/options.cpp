#include "surl/cli/options.hpp"

#include "surl/util/fsutil.hpp"
#include "surl/util/json.hpp"
#include "surl/util/paths.hpp"
#include "surl/util/strings.hpp"
#include "surl/version.hpp"

#include <cstdlib>
#include <thread>
#include <type_traits>

namespace fs = std::filesystem;

namespace surl {
namespace {

struct CommandEntry {
    const char* name;
    Command command;
};

constexpr CommandEntry kCommands[] = {
    {"clone", Command::Clone},       {"mirror", Command::Mirror},
    {"serve", Command::Serve},       {"inspect", Command::Inspect},
    {"check", Command::Check},       {"clean", Command::Clean},
    {"doctor", Command::Doctor},     {"config", Command::Config},
    {"install", Command::Install},   {"uninstall", Command::Uninstall},
    {"update", Command::Update},     {"version", Command::Version},
    {"help", Command::Help},
};

bool lookup_command(std::string_view name, Command& out) {
    for (const CommandEntry& entry : kCommands) {
        if (name == entry.name) {
            out = entry.command;
            return true;
        }
    }
    return false;
}

/// True when an argument looks like a URL rather than a subcommand.
bool looks_like_url(std::string_view text) {
    if (istarts_with(text, "http://") || istarts_with(text, "https://")) return true;
    // Bare hosts such as "example.com/path" are accepted for convenience.
    if (text.find('.') != std::string_view::npos && text.find(' ') == std::string_view::npos &&
        !text.empty() && text.front() != '-' && text.front() != '.' && text.front() != '/') {
        return true;
    }
    return false;
}

int default_concurrency() {
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) return 8;
    const unsigned value = hardware * 2;
    return static_cast<int>(value > 32 ? 32 : (value < 4 ? 4 : value));
}

/// Reads NAME=value or NAME: value into a header pair.
bool parse_header_argument(std::string_view text, std::string& name, std::string& value) {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    name = trim(text.substr(0, colon));
    value = trim(text.substr(colon + 1));
    return !name.empty();
}

bool parse_int(std::string_view text, int& out) {
    const std::string s = trim(text);
    if (s.empty()) return false;
    char* end = nullptr;
    const long value = std::strtol(s.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') return false;
    out = static_cast<int>(value);
    return true;
}

} // namespace

std::string default_user_agent() {
    return std::string("surl/") + SURL_VERSION_STRING + " (+" + SURL_HOMEPAGE_URL + ")";
}

bool apply_config_file(Options& out, std::string& error) {
    const fs::path file = config_file();
    std::error_code ec;
    if (!fs::exists(file, ec)) return true;

    std::string text;
    if (!read_file(file, text)) {
        error = "could not read config file at " + path_to_utf8(file);
        return false;
    }

    Json root;
    std::string parse_error;
    if (!Json::parse(text, root, parse_error)) {
        error = "config file at " + path_to_utf8(file) + " is not valid JSON: " + parse_error;
        return false;
    }
    if (!root.is_object()) {
        error = "config file at " + path_to_utf8(file) + " must contain a JSON object";
        return false;
    }

    const auto number = [&](const char* key, auto& target) {
        if (root.contains(key) && root[key].is_number()) {
            target = static_cast<std::remove_reference_t<decltype(target)>>(
                root[key].as_uint(static_cast<std::uint64_t>(target)));
        }
    };
    const auto boolean = [&](const char* key, bool& target) {
        if (root.contains(key) && root[key].is_bool()) target = root[key].as_bool(target);
    };
    const auto text_value = [&](const char* key, std::string& target) {
        if (root.contains(key) && root[key].is_string()) target = root[key].as_string();
    };

    number("concurrency", out.concurrency);
    number("timeout_ms", out.timeout_ms);
    number("retries", out.retries);
    number("delay_ms", out.delay_ms);
    number("max_depth", out.max_depth);
    number("max_files", out.max_files);
    number("max_file_size", out.max_file_size);
    number("max_total_size", out.max_total_size);
    number("rate_limit", out.rate_limit_bytes_per_sec);
    number("port", out.port);

    boolean("recursive", out.recursive);
    boolean("same_origin", out.same_origin);
    boolean("external_assets", out.external_assets);
    boolean("respect_robots", out.respect_robots);
    boolean("color", out.color);
    boolean("rewrite_links", out.rewrite_links);

    text_value("user_agent", out.user_agent);
    text_value("proxy", out.proxy);
    text_value("bind_address", out.bind_address);

    if (root.contains("headers") && root["headers"].is_object()) {
        for (const auto& [name, value] : root["headers"].fields()) {
            if (value.is_string()) out.headers.emplace_back(name, value.as_string());
        }
    }
    return true;
}

void apply_builtin_defaults(Options& out) {
    out.concurrency = default_concurrency();
    out.user_agent = default_user_agent();
}

bool parse_command_line(int argc, char** argv, Options& out, std::string& error) {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 1 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);

    if (args.empty()) {
        out.show_help = true;
        out.command = Command::Help;
        return true;
    }

    // A leading subcommand is optional: `surl https://example.com` is the
    // common case and means `surl clone https://example.com`.
    std::size_t index = 0;
    Command command = Command::Clone;
    if (!args[0].empty() && args[0].front() != '-' && !looks_like_url(args[0])) {
        if (!lookup_command(args[0], command)) {
            error = "unknown command '" + args[0] +
                    "'. Run 'surl --help' to see the available commands.";
            return false;
        }
        out.command_name = args[0];
        index = 1;
    }
    out.command = command;

    // Sensible per-command defaults before flags are applied.
    if (command == Command::Mirror) {
        out.recursive = true;
        out.same_origin = true;
    }

    std::vector<std::string> positional;

    const auto need_value = [&](const std::string& flag, std::size_t& i,
                                std::string& value) -> bool {
        // Support both "--flag value" and "--flag=value".
        const std::size_t equals = flag.find('=');
        if (equals != std::string::npos) {
            value = flag.substr(equals + 1);
            return true;
        }
        if (i + 1 >= args.size()) {
            error = "option '" + flag + "' requires a value";
            return false;
        }
        value = args[++i];
        return true;
    };

    for (std::size_t i = index; i < args.size(); ++i) {
        const std::string& raw = args[i];

        if (raw == "--") {
            for (std::size_t j = i + 1; j < args.size(); ++j) positional.push_back(args[j]);
            break;
        }

        if (raw.empty() || raw.front() != '-' || raw == "-") {
            positional.push_back(raw);
            continue;
        }

        // Split "--name=value" into the name used for matching.
        const std::size_t equals = raw.find('=');
        const std::string name = (equals == std::string::npos) ? raw : raw.substr(0, equals);
        std::string value;

        if (name == "-h" || name == "--help") {
            out.show_help = true;
        } else if (name == "--version" || name == "-V") {
            out.show_version = true;
        } else if (name == "-d" || name == "--directory" || name == "-o" || name == "--output") {
            if (!need_value(raw, i, value)) return false;
            out.directory = utf8_to_path(value);
        } else if (name == "-c" || name == "--concurrency") {
            if (!need_value(raw, i, value)) return false;
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed < 1 || parsed > 256) {
                error = "--concurrency must be between 1 and 256";
                return false;
            }
            out.concurrency = parsed;
            out.given.concurrency = true;
        } else if (name == "-t" || name == "--timeout") {
            if (!need_value(raw, i, value)) return false;
            std::uint64_t ms = 0;
            if (!parse_duration_ms(value, ms) || ms == 0) {
                error = "--timeout expects a duration such as 30s, 500ms or 2m";
                return false;
            }
            out.timeout_ms = static_cast<std::uint32_t>(ms);
            out.given.timeout = true;
        } else if (name == "-r" || name == "--recursive") {
            out.recursive = true;
            out.given.recursive = true;
        } else if (name == "--max-depth") {
            if (!need_value(raw, i, value)) return false;
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed < 0) {
                error = "--max-depth expects a non-negative number";
                return false;
            }
            out.max_depth = parsed;
            out.given.max_depth = true;
        } else if (name == "--include") {
            if (!need_value(raw, i, value)) return false;
            out.include.push_back(value);
        } else if (name == "--exclude") {
            if (!need_value(raw, i, value)) return false;
            out.exclude.push_back(value);
        } else if (name == "--same-origin") {
            out.same_origin = true;
            out.given.same_origin = true;
        } else if (name == "--no-same-origin" || name == "--cross-origin") {
            out.same_origin = false;
            out.given.same_origin = true;
        } else if (name == "--external-assets") {
            out.external_assets = true;
        } else if (name == "--no-external-assets") {
            out.external_assets = false;
        } else if (name == "--no-assets") {
            out.download_assets = false;
        } else if (name == "--no-rewrite") {
            out.rewrite_links = false;
        } else if (name == "--resume") {
            out.resume = true;
        } else if (name == "--update") {
            out.update_mode = true;
        } else if (name == "--force") {
            out.force = true;
        } else if (name == "--dry-run") {
            out.dry_run = true;
        } else if (name == "--list") {
            out.list_only = true;
            out.dry_run = true;
        } else if (name == "--json") {
            out.json_output = true;
        } else if (name == "--check") {
            out.check_only = true;
        } else if (name == "-v" || name == "--verbose") {
            out.verbosity = 2;
        } else if (name == "--debug") {
            out.verbosity = 3;
        } else if (name == "-q" || name == "--quiet") {
            out.verbosity = 0;
        } else if (name == "--proxy") {
            if (!need_value(raw, i, value)) return false;
            out.proxy = value;
        } else if (name == "-H" || name == "--header") {
            if (!need_value(raw, i, value)) return false;
            std::string header_name;
            std::string header_value;
            if (!parse_header_argument(value, header_name, header_value)) {
                error = "--header expects 'Name: value', got '" + value + "'";
                return false;
            }
            out.headers.emplace_back(std::move(header_name), std::move(header_value));
        } else if (name == "--cookie") {
            if (!need_value(raw, i, value)) return false;
            out.cookies.push_back(value);
        } else if (name == "-A" || name == "--user-agent") {
            if (!need_value(raw, i, value)) return false;
            out.user_agent = value;
            out.given.user_agent = true;
        } else if (name == "--retries") {
            if (!need_value(raw, i, value)) return false;
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed < 0 || parsed > 20) {
                error = "--retries must be between 0 and 20";
                return false;
            }
            out.retries = parsed;
        } else if (name == "--delay") {
            if (!need_value(raw, i, value)) return false;
            if (!parse_duration_ms(value, out.delay_ms)) {
                error = "--delay expects a duration such as 200ms or 1s";
                return false;
            }
        } else if (name == "--rate-limit") {
            if (!need_value(raw, i, value)) return false;
            if (!parse_size(value, out.rate_limit_bytes_per_sec)) {
                error = "--rate-limit expects a size per second such as 500k or 2M";
                return false;
            }
        } else if (name == "--max-file-size") {
            if (!need_value(raw, i, value)) return false;
            if (!parse_size(value, out.max_file_size)) {
                error = "--max-file-size expects a size such as 10M";
                return false;
            }
        } else if (name == "--max-total-size") {
            if (!need_value(raw, i, value)) return false;
            if (!parse_size(value, out.max_total_size)) {
                error = "--max-total-size expects a size such as 2G";
                return false;
            }
        } else if (name == "--max-files") {
            if (!need_value(raw, i, value)) return false;
            std::uint64_t parsed = 0;
            if (!parse_size(value, parsed) || parsed == 0) {
                error = "--max-files expects a positive number";
                return false;
            }
            out.max_files = parsed;
        } else if (name == "--no-robots") {
            out.respect_robots = false;
        } else if (name == "--insecure") {
            out.insecure = true;
        } else if (name == "--allow-private") {
            out.allow_private_hosts = true;
        } else if (name == "--no-color" || name == "--no-colour") {
            out.color = false;
        } else if (name == "--ci") {
            out.ci = true;
            out.color = false;
        } else if (name == "--port" || name == "-p") {
            if (!need_value(raw, i, value)) return false;
            int parsed = 0;
            if (!parse_int(value, parsed) || parsed < 1 || parsed > 65535) {
                error = "--port must be between 1 and 65535";
                return false;
            }
            out.port = static_cast<std::uint16_t>(parsed);
        } else if (name == "--bind") {
            if (!need_value(raw, i, value)) return false;
            out.bind_address = value;
        } else if (name == "--open") {
            out.open_browser = true;
        } else if (name == "--all") {
            out.clean_all = true;
        } else {
            error = "unknown option '" + name + "'. Run 'surl --help' for the full list.";
            return false;
        }
    }

    // Distribute positional arguments according to the command.
    switch (out.command) {
    case Command::Clone:
    case Command::Mirror:
        if (!positional.empty()) {
            out.url = positional.front();
            positional.erase(positional.begin());
        }
        break;
    case Command::Serve:
    case Command::Inspect:
    case Command::Check:
    case Command::Clean:
        if (!positional.empty()) {
            out.directory = utf8_to_path(positional.front());
            positional.erase(positional.begin());
        }
        break;
    default:
        break;
    }
    out.positional = std::move(positional);

    if (out.ci) out.color = false;
    return true;
}

} // namespace surl
