#pragma once

#include "surl/cli/options.hpp"
#include "surl/mirror/cache.hpp"
#include "surl/mirror/pathmap.hpp"
#include "surl/net/http_client.hpp"
#include "surl/net/url.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace surl {

struct CrawlStats {
    std::uint64_t downloaded = 0;  ///< resources fetched over the network
    std::uint64_t reused = 0;      ///< served from the previous run's cache
    std::uint64_t unchanged = 0;   ///< conditional request answered 304
    std::uint64_t skipped = 0;     ///< filtered out, or over budget
    std::uint64_t failed = 0;
    std::uint64_t bytes = 0;
    std::uint64_t pages = 0;
    std::uint64_t assets = 0;
    std::uint64_t rewritten = 0;   ///< documents whose links were localised
    std::uint64_t elapsed_ms = 0;
};

struct CrawlResult {
    CrawlStats stats;
    /// Empty when the crawl finished naturally; otherwise why it stopped early.
    std::string stop_reason;
    /// Set when the run could not start at all.
    std::string fatal_error;

    bool ok() const { return fatal_error.empty(); }
};

/// Downloads a site into a local directory and rewrites its links.
///
/// The crawl runs in two phases. Phase one fetches everything, writing binary
/// assets straight to their final location and keeping raw copies of HTML and
/// CSS under .surl/raw. Phase two rewrites those raw copies once the full set
/// of URL-to-path mappings is known, which is the only way a link to a page
/// discovered later can be localised correctly.
class Crawler {
public:
    Crawler(const Options& options, HttpClient& http, Manifest& manifest,
            PathMapper mapper, std::filesystem::path root);
    ~Crawler();

    Crawler(const Crawler&) = delete;
    Crawler& operator=(const Crawler&) = delete;

    CrawlResult run(const Url& start);

    /// Asks a running crawl to stop at the next opportunity.
    void request_stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Chooses the default output directory for a URL: ./<host>.
std::filesystem::path default_output_directory(const Url& url);

} // namespace surl
