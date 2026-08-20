#include "surl/cli/help.hpp"

#include "surl/net/http_client.hpp"
#include "surl/version.hpp"

namespace surl {

std::string usage_line() {
    return "usage: surl [command] <url|directory> [options]\n"
           "       surl --help";
}

std::string help_text() {
    return
        "SURL " SURL_VERSION_STRING "\n"
        "Fast native C++ CLI tool for creating local copies of publicly\n"
        "accessible web resources.\n"
        "\n"
        "USAGE\n"
        "  surl <url> [-d <directory>] [options]\n"
        "  surl <command> [arguments] [options]\n"
        "\n"
        "COMMANDS\n"
        "  clone <url>          Download one page and the assets it needs (default)\n"
        "  mirror <url>         Recursively mirror a whole site (implies --recursive)\n"
        "  serve <directory>    Serve a mirrored directory over HTTP\n"
        "  inspect <directory>  Summarise a mirror: pages, assets, sizes, failures\n"
        "  check <directory>    Verify stored files against the manifest checksums\n"
        "  clean <directory>    Remove SURL metadata (--all also removes the files)\n"
        "  doctor               Report environment, PATH and network diagnostics\n"
        "  config               Show, get or set user configuration\n"
        "  install              Add this executable's directory to your PATH\n"
        "  uninstall            Remove it from your PATH again\n"
        "  update [--check]     Check GitHub releases for a newer SURL\n"
        "  version [--check]    Print the version\n"
        "\n"
        "OUTPUT\n"
        "  -d, --directory <dir>   Where to write the mirror (default: ./<host>)\n"
        "  -o, --output <dir>      Alias for --directory\n"
        "\n"
        "CRAWL SHAPE\n"
        "  -r, --recursive         Follow links to other pages\n"
        "      --max-depth <n>     Maximum link depth when recursing (default: 5)\n"
        "      --include <glob>    Only fetch URLs matching this pattern (repeatable)\n"
        "      --exclude <glob>    Never fetch URLs matching this pattern (repeatable)\n"
        "      --same-origin       Restrict pages to the starting origin (default)\n"
        "      --cross-origin      Allow pages on other hosts\n"
        "      --external-assets   Fetch assets from other hosts (default)\n"
        "      --no-external-assets  Leave third-party assets as absolute URLs\n"
        "      --no-assets         Fetch pages only, no images/CSS/JS\n"
        "      --no-rewrite        Store original bytes without rewriting links\n"
        "\n"
        "TRANSFER\n"
        "  -c, --concurrency <n>   Parallel downloads (default: 2x CPU cores, max 32)\n"
        "  -t, --timeout <dur>     Per-request timeout, e.g. 30s, 500ms (default: 30s)\n"
        "      --retries <n>       Retries per failed request (default: 2)\n"
        "      --delay <dur>       Pause between requests on each worker\n"
        "      --rate-limit <size> Cap total download rate, e.g. 500k, 2M\n"
        "      --proxy <url>       HTTP proxy, e.g. http://127.0.0.1:8080\n"
        "  -H, --header <h>        Extra request header 'Name: value' (repeatable)\n"
        "      --cookie <c>        Cookie 'name=value' (repeatable)\n"
        "  -A, --user-agent <ua>   Override the User-Agent\n"
        "      --insecure          Do not verify TLS certificates (dangerous)\n"
        "      --no-robots         Ignore robots.txt (only for sites you control)\n"
        "      --allow-private     Permit localhost and private-network targets\n"
        "\n"
        "BUDGETS\n"
        "      --max-file-size <size>   Skip resources larger than this (default: 64M)\n"
        "      --max-total-size <size>  Stop once the mirror reaches this (default: 2G)\n"
        "      --max-files <n>          Stop after this many files (default: 5000)\n"
        "\n"
        "RUN MODE\n"
        "      --resume            Continue an interrupted run, skipping finished files\n"
        "      --update            Re-check every resource with conditional requests\n"
        "      --force             Re-download everything, ignoring the cache\n"
        "      --dry-run           Show what would be fetched without writing anything\n"
        "      --list              List the URLs that would be fetched, then exit\n"
        "      --json              Emit machine-readable JSON on stdout\n"
        "\n"
        "SERVE\n"
        "  -p, --port <n>          Port for `surl serve` (default: 8080)\n"
        "      --bind <address>    Interface to bind (default: 127.0.0.1)\n"
        "      --open              Open the mirror in your browser once serving\n"
        "\n"
        "LOGGING\n"
        "  -v, --verbose           Log every resource\n"
        "      --debug             Log protocol-level detail\n"
        "  -q, --quiet             Errors only\n"
        "      --no-color          Disable ANSI colour\n"
        "      --ci                Non-interactive output for build logs\n"
        "\n"
        "  -h, --help              Show this help\n"
        "  -V, --version           Show the version\n"
        "\n"
        "EXAMPLES\n"
        "  surl https://example.com -d ./website\n"
        "  surl mirror https://example.com -d ./site --max-depth 3\n"
        "  surl https://example.com -d ./site --resume\n"
        "  surl https://example.com -d ./site --update\n"
        "  surl serve ./website -p 8080\n"
        "  surl inspect ./website\n"
        "\n"
        "SURL only downloads resources a normal browser could fetch anonymously.\n"
        "It honours robots.txt by default and never bypasses authentication.\n"
        "\n"
        "Documentation: " SURL_HOMEPAGE_URL "\n";
}

std::string version_text(bool verbose) {
    std::string out = "surl ";
    out += SURL_VERSION_STRING;
    if (!verbose) return out;

    out += "\n";
    out += "http backend: ";
    out += HttpClient::backend_name();
    out += "\n";

#if defined(_MSC_VER)
    out += "compiler:     MSVC " + std::to_string(_MSC_VER) + "\n";
#elif defined(__clang__)
    out += "compiler:     Clang " __clang_version__ "\n";
#elif defined(__GNUC__)
    out += "compiler:     GCC " __VERSION__ "\n";
#endif

#if defined(_WIN32)
    out += "platform:     Windows\n";
#elif defined(__APPLE__)
    out += "platform:     macOS\n";
#else
    out += "platform:     Linux\n";
#endif

    out += "repository:   " SURL_HOMEPAGE_URL "\n";
    return out;
}

} // namespace surl
