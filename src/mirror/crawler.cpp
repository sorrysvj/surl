#include "surl/mirror/crawler.hpp"

#include "surl/mirror/rewrite.hpp"
#include "surl/net/robots.hpp"
#include "surl/util/fsutil.hpp"
#include "surl/util/hash.hpp"
#include "surl/util/log.hpp"
#include "surl/util/strings.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace surl {
namespace {

/// Shared token bucket so --rate-limit caps the whole run, not each worker.
class RateLimiter {
public:
    explicit RateLimiter(std::uint64_t bytes_per_second)
        : bytes_per_second_(bytes_per_second), window_start_(std::chrono::steady_clock::now()) {}

    void consume(std::uint64_t bytes) {
        if (bytes_per_second_ == 0) return;

        std::unique_lock<std::mutex> lock(mutex_);
        consumed_ += bytes;

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - window_start_).count();

        // How long this much data *should* have taken at the target rate.
        const auto owed_ms =
            static_cast<long long>((consumed_ * 1000ULL) / bytes_per_second_);
        if (owed_ms > elapsed) {
            const auto sleep_for = std::chrono::milliseconds(owed_ms - elapsed);
            lock.unlock();
            std::this_thread::sleep_for(sleep_for);
            lock.lock();
        }

        // Reset the window every few seconds so a slow start does not grant an
        // unbounded burst later.
        if (elapsed > 5000) {
            window_start_ = std::chrono::steady_clock::now();
            consumed_ = 0;
        }
    }

private:
    std::uint64_t bytes_per_second_;
    std::mutex mutex_;
    std::uint64_t consumed_ = 0;
    std::chrono::steady_clock::time_point window_start_;
};

struct QueueItem {
    Url url;
    int depth = 0;
    LinkRole role = LinkRole::Asset;
    ResourceKind hint = ResourceKind::Other;
};

/// A document whose links still have to be localised in phase two.
struct PendingDocument {
    std::string url;      ///< the URL it was requested as
    std::string base_url; ///< the URL it actually came from, after redirects
    std::string relative; ///< destination path, root-relative
    std::string raw_path; ///< raw copy under .surl/raw, root-relative
    ResourceKind kind = ResourceKind::Html;
};

std::string status_phrase(int status) {
    switch (status) {
    case 400: return "bad request";
    case 401: return "unauthorised";
    case 403: return "forbidden";
    case 404: return "not found";
    case 408: return "request timeout";
    case 410: return "gone";
    case 429: return "rate limited";
    case 500: return "server error";
    case 502: return "bad gateway";
    case 503: return "service unavailable";
    case 504: return "gateway timeout";
    default: break;
    }
    return "http " + std::to_string(status);
}

bool is_retryable_status(int status) {
    return status == 408 || status == 425 || status == 429 || status == 500 ||
           status == 502 || status == 503 || status == 504;
}

/// True for the resource kinds whose bodies contain links worth rewriting.
bool needs_rewriting(ResourceKind kind) {
    return kind == ResourceKind::Html || kind == ResourceKind::Css ||
           kind == ResourceKind::Manifest;
}

std::string raw_copy_relative(const Url& url, ResourceKind kind) {
    // Deterministic so a resumed run finds the same raw file.
    const std::string digest = sha256_hex(url.to_string_no_fragment()).substr(0, 32);
    const char* extension = ".bin";
    switch (kind) {
    case ResourceKind::Html: extension = ".html"; break;
    case ResourceKind::Css: extension = ".css"; break;
    case ResourceKind::Manifest: extension = ".json"; break;
    default: break;
    }
    return ".surl/raw/" + digest + extension;
}

} // namespace

fs::path default_output_directory(const Url& url) {
    std::string name = url.host.empty() ? "surl-output" : url.host;
    if (url.port != 0) name += "_" + std::to_string(url.port);
    return fs::current_path() / utf8_to_path(sanitize_path_component(name));
}

// ---------------------------------------------------------------------------

struct Crawler::Impl {
    Impl(const Options& options_in, HttpClient& http_in, Manifest& manifest_in,
         PathMapper mapper_in, fs::path root_in)
        : options(options_in),
          http(http_in),
          manifest(manifest_in),
          mapper(std::move(mapper_in)),
          root(std::move(root_in)),
          limiter(options_in.rate_limit_bytes_per_sec) {}

    const Options& options;
    HttpClient& http;
    Manifest& manifest;
    PathMapper mapper;
    fs::path root;
    RateLimiter limiter;

    Url primary;

    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::deque<QueueItem> queue;
    std::unordered_set<std::string> seen;
    int active_workers = 0;
    bool stopping = false;

    std::mutex robots_mutex;
    std::map<std::string, RobotsTxt> robots_by_origin;

    std::mutex documents_mutex;
    std::vector<PendingDocument> documents;

    std::mutex stats_mutex;
    CrawlStats stats;

    std::atomic<std::uint64_t> total_bytes{0};
    std::atomic<std::uint64_t> file_count{0};
    std::atomic<bool> stop_requested{false};
    std::mutex stop_reason_mutex;
    std::string stop_reason;

    // -- helpers ------------------------------------------------------------

    void note_stop(std::string reason) {
        {
            std::lock_guard<std::mutex> lock(stop_reason_mutex);
            if (stop_reason.empty()) stop_reason = std::move(reason);
        }
        stop_requested.store(true, std::memory_order_relaxed);
        queue_cv.notify_all();
    }

    bool enqueue(const Url& url, int depth, LinkRole role, ResourceKind hint) {
        const std::string key = url.to_string_no_fragment();
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!seen.insert(key).second) return false;
        queue.push_back(QueueItem{url, depth, role, hint});
        queue_cv.notify_one();
        return true;
    }

    bool pop(QueueItem& item) {
        std::unique_lock<std::mutex> lock(queue_mutex);
        while (true) {
            if (stopping) return false;
            if (!queue.empty()) {
                item = std::move(queue.front());
                queue.pop_front();
                ++active_workers;
                return true;
            }
            if (active_workers == 0) {
                // Nothing queued and nobody producing: the crawl is done.
                stopping = true;
                queue_cv.notify_all();
                return false;
            }
            queue_cv.wait(lock);
        }
    }

    void finish_item() {
        std::lock_guard<std::mutex> lock(queue_mutex);
        --active_workers;
        queue_cv.notify_all();
    }

    void record(const CacheEntry& entry) { manifest.put(entry); }

    void bump(std::uint64_t CrawlStats::*field, std::uint64_t amount = 1) {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.*field += amount;
    }

    const RobotsTxt& robots_for(const Url& url) {
        const std::string origin = url.origin();
        {
            std::lock_guard<std::mutex> lock(robots_mutex);
            const auto it = robots_by_origin.find(origin);
            if (it != robots_by_origin.end()) return it->second;
        }

        RobotsTxt parsed = RobotsTxt::allow_all();
        Url robots_url;
        if (parse_url(origin + "/robots.txt", robots_url)) {
            HttpRequest request;
            request.url = robots_url;
            request.timeout_ms = options.timeout_ms;
            request.max_body_bytes = 512 * 1024;
            request.headers = base_headers();

            const HttpResponse response = http.send(request);
            if (response.ok() && !response.body.empty()) {
                parsed = RobotsTxt::parse(response.body, "surl");
                log().debug("robots.txt loaded for " + origin);
            } else {
                log().debug("no usable robots.txt for " + origin + " (allowing everything)");
            }
        }

        std::lock_guard<std::mutex> lock(robots_mutex);
        return robots_by_origin.emplace(origin, std::move(parsed)).first->second;
    }

    HeaderList base_headers() const {
        HeaderList headers = options.headers;
        if (!options.cookies.empty()) {
            headers.emplace_back("Cookie", join(options.cookies, "; "));
        }
        // Ask for the encodings the backend can transparently decode.
        headers.emplace_back("Accept",
                             "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
        return headers;
    }

    /// Decides whether a discovered URL is in scope.
    bool in_scope(const Url& url, LinkRole role, int depth, std::string& reason) const {
        if (!url.is_http()) {
            reason = "unsupported scheme";
            return false;
        }
        if (!options.allow_private_hosts && is_private_or_loopback_host(url.host)) {
            reason = "private or loopback host";
            return false;
        }

        const std::string text = url.to_string_no_fragment();
        for (const std::string& pattern : options.exclude) {
            if (glob_match(pattern, text)) {
                reason = "matched --exclude " + pattern;
                return false;
            }
        }
        if (!options.include.empty()) {
            bool matched = false;
            for (const std::string& pattern : options.include) {
                if (glob_match(pattern, text)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                reason = "did not match any --include pattern";
                return false;
            }
        }

        if (role == LinkRole::Navigation) {
            if (depth > 0 && !options.recursive) {
                reason = "not recursing (use --recursive)";
                return false;
            }
            if (depth > options.max_depth) {
                reason = "beyond --max-depth";
                return false;
            }
            if (options.same_origin && !url.same_origin_as(primary)) {
                reason = "different origin";
                return false;
            }
        } else {
            if (!options.download_assets) {
                reason = "--no-assets";
                return false;
            }
            if (!options.external_assets && !url.same_origin_as(primary)) {
                reason = "third-party asset";
                return false;
            }
        }
        return true;
    }

    bool over_budget(std::string& reason) {
        if (file_count.load(std::memory_order_relaxed) >= options.max_files) {
            reason = "reached --max-files (" + std::to_string(options.max_files) + ")";
            return true;
        }
        if (total_bytes.load(std::memory_order_relaxed) >= options.max_total_size) {
            reason = "reached --max-total-size (" + human_size(options.max_total_size) + ")";
            return true;
        }
        return false;
    }

    HttpResponse fetch_with_retries(const Url& url, const CacheEntry* cached) {
        HttpRequest request;
        request.url = url;
        request.timeout_ms = options.timeout_ms;
        request.max_body_bytes = options.max_file_size;
        request.headers = base_headers();

        // Conditional request: --update asks the server whether anything moved.
        if (options.update_mode && cached != nullptr && !options.force) {
            if (!cached->etag.empty()) request.headers.emplace_back("If-None-Match", cached->etag);
            if (!cached->last_modified.empty()) {
                request.headers.emplace_back("If-Modified-Since", cached->last_modified);
            }
        }

        HttpResponse response;
        for (int attempt = 0; attempt <= options.retries; ++attempt) {
            if (attempt > 0) {
                // Exponential backoff, capped so a big site cannot stall for
                // minutes on one bad host.
                const auto delay = std::chrono::milliseconds(
                    std::min<long long>(8000, 250LL << (attempt - 1)));
                log().debug("retry " + std::to_string(attempt) + " for " +
                            url.to_string_no_fragment() + " in " +
                            human_duration_ms(static_cast<std::uint64_t>(delay.count())));
                std::this_thread::sleep_for(delay);
            }

            response = http.send(request);
            if (response.error.empty() && !is_retryable_status(response.status)) break;
            if (stop_requested.load(std::memory_order_relaxed)) break;
        }
        return response;
    }

    void process(const QueueItem& item);
    void worker();
    void rewrite_phase();
};

void Crawler::Impl::process(const QueueItem& item) {
    const std::string url_key = item.url.to_string_no_fragment();

    std::string reason;
    if (!in_scope(item.url, item.role, item.depth, reason)) {
        log().verbose("skip " + url_key + " (" + reason + ")");
        CacheEntry entry;
        entry.url = url_key;
        entry.state = EntryState::Skipped;
        entry.note = reason;
        entry.depth = item.depth;
        record(entry);
        bump(&CrawlStats::skipped);
        return;
    }

    if (options.respect_robots) {
        const RobotsTxt& robots = robots_for(item.url);
        std::string path = item.url.path;
        if (item.url.has_query) path += "?" + item.url.query;
        if (!robots.is_allowed(path)) {
            log().verbose("skip " + url_key + " (disallowed by robots.txt)");
            CacheEntry entry;
            entry.url = url_key;
            entry.state = EntryState::Skipped;
            entry.note = "disallowed by robots.txt";
            entry.depth = item.depth;
            record(entry);
            bump(&CrawlStats::skipped);
            return;
        }
    }

    if (options.list_only) {
        log().out(url_key);
        bump(&CrawlStats::skipped);
        // Still needs the body to discover further links when recursing.
        if (!options.recursive && item.depth > 0) return;
    }

    std::string budget_reason;
    if (over_budget(budget_reason)) {
        note_stop(budget_reason);
        return;
    }

    // --- cache lookup -----------------------------------------------------
    const std::optional<CacheEntry> cached_entry = manifest.find(url_key);
    const CacheEntry* cached = cached_entry.has_value() ? &cached_entry.value() : nullptr;

    if (cached != nullptr && cached->state == EntryState::Done && options.resume &&
        !options.update_mode && !options.force) {
        const fs::path stored = mapper.absolute_path(cached->path);
        std::error_code ec;
        if (fs::exists(stored, ec)) {
            log().verbose("cached " + url_key);
            bump(&CrawlStats::reused);
            file_count.fetch_add(1, std::memory_order_relaxed);
            total_bytes.fetch_add(cached->size, std::memory_order_relaxed);

            // Re-read the raw copy so link discovery continues from where the
            // previous run left off.
            if (needs_rewriting(cached->kind)) {
                const std::string raw_relative = raw_copy_relative(item.url, cached->kind);
                std::string body;
                if (read_file(mapper.absolute_path(raw_relative), body)) {
                    Url base;
                    if (!parse_url(url_key, base)) base = item.url;
                    std::vector<DiscoveredLink> links =
                        (cached->kind == ResourceKind::Css)
                            ? discover_css_links(body, base)
                            : discover_html_links(body, base);
                    for (const DiscoveredLink& link : links) {
                        const int next_depth =
                            link.role == LinkRole::Navigation ? item.depth + 1 : item.depth;
                        enqueue(link.url, next_depth, link.role, link.hint);
                    }
                    std::lock_guard<std::mutex> lock(documents_mutex);
                    documents.push_back(PendingDocument{url_key, url_key, cached->path,
                                                        raw_relative, cached->kind});
                }
            }
            return;
        }
    }

    if (options.delay_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(options.delay_ms));
    }

    // --- fetch ------------------------------------------------------------
    log().verbose("get  " + url_key);
    const HttpResponse response = fetch_with_retries(item.url, cached);

    if (!response.error.empty()) {
        log().warn("failed " + url_key + ": " + response.error);
        CacheEntry entry;
        entry.url = url_key;
        entry.state = EntryState::Failed;
        entry.note = response.error;
        entry.depth = item.depth;
        if (cached != nullptr) entry.path = cached->path;
        record(entry);
        bump(&CrawlStats::failed);
        return;
    }

    // 304: the cached copy is still current.
    if (response.status == 304 && cached != nullptr) {
        log().verbose("unchanged " + url_key);
        CacheEntry entry = *cached;
        entry.fetched_at = unix_now();
        record(entry);
        bump(&CrawlStats::unchanged);
        file_count.fetch_add(1, std::memory_order_relaxed);
        total_bytes.fetch_add(entry.size, std::memory_order_relaxed);

        if (needs_rewriting(entry.kind)) {
            const std::string raw_relative = raw_copy_relative(item.url, entry.kind);
            std::string body;
            if (read_file(mapper.absolute_path(raw_relative), body)) {
                Url base;
                if (!parse_url(url_key, base)) base = item.url;
                for (const DiscoveredLink& link :
                     (entry.kind == ResourceKind::Css ? discover_css_links(body, base)
                                                      : discover_html_links(body, base))) {
                    const int next_depth =
                        link.role == LinkRole::Navigation ? item.depth + 1 : item.depth;
                    enqueue(link.url, next_depth, link.role, link.hint);
                }
                std::lock_guard<std::mutex> lock(documents_mutex);
                documents.push_back(
                    PendingDocument{url_key, url_key, entry.path, raw_relative, entry.kind});
            }
        }
        return;
    }

    if (response.status < 200 || response.status >= 300) {
        const std::string note = status_phrase(response.status);
        log().warn("failed " + url_key + ": " + note);
        CacheEntry entry;
        entry.url = url_key;
        entry.state = EntryState::Failed;
        entry.status = response.status;
        entry.note = note;
        entry.depth = item.depth;
        record(entry);
        bump(&CrawlStats::failed);
        return;
    }

    if (response.truncated) {
        log().warn("skipped " + url_key + ": larger than --max-file-size (" +
                   human_size(options.max_file_size) + ")");
        CacheEntry entry;
        entry.url = url_key;
        entry.state = EntryState::Skipped;
        entry.status = response.status;
        entry.note = "exceeds --max-file-size";
        entry.depth = item.depth;
        record(entry);
        bump(&CrawlStats::skipped);
        return;
    }

    limiter.consume(response.body.size());

    // --- classify ---------------------------------------------------------
    ResourceKind kind = kind_from_content_type(response.content_type());
    if (kind == ResourceKind::Other) {
        // Fall back to the extension when the server is unhelpful.
        const std::string path = response.final_url.path;
        const std::size_t dot = path.rfind('.');
        if (dot != std::string::npos && path.size() - dot <= 8) {
            kind = kind_from_extension(path.substr(dot));
        }
    }
    if (kind == ResourceKind::Other && item.role == LinkRole::Navigation) {
        kind = ResourceKind::Html;
    }

    const std::string relative = mapper.relative_path_for(item.url, kind);
    const fs::path destination = mapper.absolute_path(relative);

    // Nothing a server says may place a file outside the output root.
    if (!is_inside(root, destination)) {
        log().warn("refused " + url_key + ": would write outside the output directory");
        CacheEntry entry;
        entry.url = url_key;
        entry.state = EntryState::Skipped;
        entry.note = "path escapes the output directory";
        record(entry);
        bump(&CrawlStats::skipped);
        return;
    }

    CacheEntry entry;
    entry.url = url_key;
    entry.path = relative;
    entry.status = response.status;
    entry.size = response.body.size();
    entry.content_type = content_type_essence(response.content_type());
    entry.etag = response.header("ETag");
    entry.last_modified = response.header("Last-Modified");
    entry.kind = kind;
    entry.depth = item.depth;
    entry.fetched_at = unix_now();
    entry.state = EntryState::Done;
    entry.sha256 = sha256_hex(response.body);

    // --- store ------------------------------------------------------------
    if (!options.dry_run) {
        std::string write_error;
        if (needs_rewriting(kind)) {
            // Keep the untouched bytes; phase two turns them into the final
            // file once every URL-to-path mapping is known.
            const std::string raw_relative = raw_copy_relative(item.url, kind);
            if (!write_file_atomic(mapper.absolute_path(raw_relative), response.body,
                                   write_error)) {
                log().warn("could not stage " + url_key + ": " + write_error);
                entry.state = EntryState::Failed;
                entry.note = write_error;
                record(entry);
                bump(&CrawlStats::failed);
                return;
            }
            std::lock_guard<std::mutex> lock(documents_mutex);
            documents.push_back(PendingDocument{
                url_key, response.final_url.to_string_no_fragment(), relative, raw_relative,
                kind});
        } else {
            if (!write_file_atomic(destination, response.body, write_error)) {
                log().warn("could not write " + url_key + ": " + write_error);
                entry.state = EntryState::Failed;
                entry.note = write_error;
                record(entry);
                bump(&CrawlStats::failed);
                return;
            }
        }
    }

    record(entry);
    bump(&CrawlStats::downloaded);
    bump(&CrawlStats::bytes, entry.size);
    bump(kind == ResourceKind::Html ? &CrawlStats::pages : &CrawlStats::assets);
    file_count.fetch_add(1, std::memory_order_relaxed);
    total_bytes.fetch_add(entry.size, std::memory_order_relaxed);

    if (log().level() == LogLevel::Normal && !options.list_only && !options.ci) {
        log().info("  " + std::string(kind_name(kind)) + "  " + relative + "  " +
                   human_size(entry.size));
    }

    // --- discover ---------------------------------------------------------
    if (kind == ResourceKind::Html || kind == ResourceKind::Css) {
        const Url& base = response.final_url;
        const std::vector<DiscoveredLink> links =
            (kind == ResourceKind::Css) ? discover_css_links(response.body, base)
                                        : discover_html_links(response.body, base);
        for (const DiscoveredLink& link : links) {
            const int next_depth =
                link.role == LinkRole::Navigation ? item.depth + 1 : item.depth;
            enqueue(link.url, next_depth, link.role, link.hint);
        }
    }
}

void Crawler::Impl::worker() {
    QueueItem item;
    while (pop(item)) {
        if (!stop_requested.load(std::memory_order_relaxed)) {
            process(item);
        }
        finish_item();
    }
}

void Crawler::Impl::rewrite_phase() {
    if (options.dry_run) return;

    std::vector<PendingDocument> pending;
    {
        std::lock_guard<std::mutex> lock(documents_mutex);
        pending = documents;
    }
    if (pending.empty()) return;

    // Resolve a target URL to its mirrored path, or nothing when it is not
    // part of this mirror.
    const LinkResolver resolver = [&](const Url& target, LinkRole, ResourceKind)
        -> std::optional<std::string> {
        const std::optional<CacheEntry> entry = manifest.find(target.to_string_no_fragment());
        if (!entry.has_value()) return std::nullopt;
        if (entry->state != EntryState::Done) return std::nullopt;
        if (entry->path.empty()) return std::nullopt;
        return entry->path;
    };

    for (const PendingDocument& document : pending) {
        std::string body;
        if (!read_file(mapper.absolute_path(document.raw_path), body)) {
            log().warn("could not re-read staged copy of " + document.url);
            continue;
        }

        std::string output = body;
        if (options.rewrite_links) {
            Url base;
            if (!parse_url(document.base_url, base)) {
                if (!parse_url(document.url, base)) {
                    log().warn("could not determine a base URL for " + document.url);
                    continue;
                }
            }

            RewriteResult result;
            if (document.kind == ResourceKind::Css) {
                result = rewrite_css(body, base, document.relative, resolver);
            } else if (document.kind == ResourceKind::Html) {
                result = rewrite_html(body, base, document.relative, resolver);
            } else {
                // Web app manifests are JSON; their icon URLs are handled by
                // the CSS-agnostic resolver via plain string references.
                result.content = body;
            }
            output = std::move(result.content);
            if (result.stats.localised > 0) bump(&CrawlStats::rewritten);
            log().debug("rewrote " + document.relative + ": " +
                        std::to_string(result.stats.localised) + " localised, " +
                        std::to_string(result.stats.absolutised) + " absolute");
        }

        std::string write_error;
        const fs::path destination = mapper.absolute_path(document.relative);
        if (!is_inside(root, destination)) {
            log().warn("refused to write " + document.relative + " outside the output root");
            continue;
        }
        if (!write_file_atomic(destination, output, write_error)) {
            log().warn("could not write " + document.relative + ": " + write_error);
            continue;
        }

        // The stored checksum has to describe what is actually on disk.
        if (std::optional<CacheEntry> entry = manifest.find(document.url); entry.has_value()) {
            entry->sha256 = sha256_hex(output);
            entry->size = output.size();
            manifest.put(*entry);
        }
    }
}

// ---------------------------------------------------------------------------

Crawler::Crawler(const Options& options, HttpClient& http, Manifest& manifest,
                 PathMapper mapper, fs::path root)
    : impl_(std::make_unique<Impl>(options, http, manifest, std::move(mapper),
                                   std::move(root))) {}

Crawler::~Crawler() = default;

void Crawler::request_stop() { impl_->note_stop("interrupted"); }

CrawlResult Crawler::run(const Url& start) {
    const auto started = std::chrono::steady_clock::now();
    CrawlResult result;

    impl_->primary = start;

    std::string error;
    if (!impl_->options.dry_run && !ensure_directory(impl_->root, error)) {
        result.fatal_error = error;
        return result;
    }

    impl_->enqueue(start, 0, LinkRole::Navigation, ResourceKind::Html);

    const int worker_count = std::max(1, impl_->options.concurrency);
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(worker_count));
    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back([this] { impl_->worker(); });
    }
    for (std::thread& worker : workers) worker.join();

    impl_->rewrite_phase();

    {
        std::lock_guard<std::mutex> lock(impl_->stats_mutex);
        result.stats = impl_->stats;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->stop_reason_mutex);
        result.stop_reason = impl_->stop_reason;
    }
    result.stats.elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return result;
}

} // namespace surl
