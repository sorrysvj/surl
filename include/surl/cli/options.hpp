#pragma once

#include "surl/net/http_client.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace surl {

enum class Command {
    Clone,     ///< default: one page plus the assets it needs
    Mirror,    ///< recursive, same-origin site copy
    Serve,     ///< serve a mirrored directory over HTTP
    Inspect,   ///< summarise a mirror's manifest
    Check,     ///< verify a mirror's integrity
    Clean,     ///< remove SURL metadata (or the whole mirror)
    Doctor,    ///< environment diagnostics
    Config,    ///< show or edit user configuration
    Install,   ///< add this executable's directory to PATH
    Uninstall, ///< remove it again
    Update,    ///< check GitHub releases for a newer version
    Version,
    Help
};

/// Everything a run of SURL needs, after merging the config file, the command
/// line and the built-in defaults (in increasing order of precedence).
struct Options {
    Command command = Command::Clone;
    std::string command_name = "clone";

    /// Target URL for clone/mirror.
    std::string url;
    /// Directory argument for serve/inspect/check/clean, and -d for clone.
    std::filesystem::path directory;
    /// Extra positional arguments (config get/set, for instance).
    std::vector<std::string> positional;

    // --- crawl shape ---
    bool recursive = false;
    int max_depth = 5;
    bool same_origin = true;
    bool external_assets = true;
    bool download_assets = true;
    std::vector<std::string> include;
    std::vector<std::string> exclude;

    // --- transfer ---
    int concurrency = 8;
    std::uint32_t timeout_ms = 30000;
    int retries = 2;
    std::uint64_t delay_ms = 0;
    std::uint64_t rate_limit_bytes_per_sec = 0; ///< 0 = unlimited
    std::string proxy;
    std::string user_agent;
    HeaderList headers;
    std::vector<std::string> cookies;
    bool insecure = false;
    bool respect_robots = true;

    // --- budgets ---
    std::uint64_t max_file_size = 64ULL * 1024 * 1024;
    std::uint64_t max_total_size = 2ULL * 1024 * 1024 * 1024;
    std::uint64_t max_files = 5000;

    // --- behaviour ---
    bool rewrite_links = true;
    bool resume = false;
    bool update_mode = false;
    bool force = false;
    bool dry_run = false;
    bool list_only = false;
    bool json_output = false;
    bool check_only = false; ///< `update --check` / `version --check`
    bool allow_private_hosts = false;

    // --- output ---
    /// 0 quiet, 1 normal, 2 verbose, 3 debug; maps onto LogLevel.
    int verbosity = 1;
    bool color = true;
    bool ci = false;

    // --- serve ---
    std::uint16_t port = 8080;
    std::string bind_address = "127.0.0.1";
    bool open_browser = false;

    // --- clean ---
    bool clean_all = false; ///< remove the mirrored files too, not just metadata

    bool show_help = false;
    bool show_version = false;

    /// Only set when the user explicitly passed the flag, so that a config file
    /// value is not silently overridden by a default.
    struct Explicit {
        bool concurrency = false;
        bool timeout = false;
        bool user_agent = false;
        bool max_depth = false;
        bool same_origin = false;
        bool recursive = false;
    } given;
};

/// Fills in defaults that depend on the running machine (thread count, the
/// version-stamped User-Agent). Call this before apply_config_file().
void apply_builtin_defaults(Options& out);

/// Parses the command line. On failure returns false with a human-readable
/// message in @p error; the caller prints usage and exits with status 2.
bool parse_command_line(int argc, char** argv, Options& out, std::string& error);

/// Applies values from the user config file as defaults. Called before the
/// command line is parsed so explicit flags always win.
bool apply_config_file(Options& out, std::string& error);

/// The default User-Agent, including the SURL version and project URL so site
/// operators can identify and contact the tool.
std::string default_user_agent();

} // namespace surl
