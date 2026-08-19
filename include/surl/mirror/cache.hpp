#pragma once

#include "surl/util/mime.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace surl {

/// State of one mirrored resource as recorded in the manifest.
enum class EntryState {
    Done,    ///< downloaded and written successfully
    Failed,  ///< tried and gave up
    Skipped  ///< deliberately not downloaded (filters, limits, robots)
};

struct CacheEntry {
    std::string url;          ///< absolute URL without fragment
    std::string path;         ///< root-relative, '/'-separated
    std::string etag;         ///< ETag header, for conditional requests
    std::string last_modified;///< Last-Modified header
    std::string content_type;
    std::string sha256;       ///< digest of the stored bytes
    std::string note;         ///< failure reason or skip reason
    int status = 0;
    std::uint64_t size = 0;
    std::uint64_t fetched_at = 0; ///< Unix seconds
    int depth = 0;
    ResourceKind kind = ResourceKind::Other;
    EntryState state = EntryState::Done;
};

/// The on-disk record of a mirror, written to <root>/.surl/manifest.json.
///
/// It is what makes `--resume` and `--update` possible: resume skips entries
/// already marked Done, and update re-requests them with If-None-Match so an
/// unchanged site costs one conditional request per resource.
class Manifest {
public:
    static std::filesystem::path default_path(const std::filesystem::path& root);
    static std::filesystem::path metadata_dir(const std::filesystem::path& root);

    /// Loads a manifest. A missing file is not an error: it yields an empty
    /// manifest, which is exactly what a first run needs.
    bool load(const std::filesystem::path& file, std::string& error);
    bool save(const std::filesystem::path& file, std::string& error) const;

    /// Looks up an entry by URL. Returns nullopt when unknown.
    std::optional<CacheEntry> find(const std::string& url) const;

    void put(CacheEntry entry);
    void remove(const std::string& url);

    std::size_t size() const;
    std::vector<CacheEntry> entries() const;

    /// Counts by state, for `surl inspect` and end-of-run summaries.
    std::size_t count_state(EntryState state) const;
    std::uint64_t total_bytes() const;

    const std::string& source() const { return source_; }
    void set_source(std::string source);

    std::uint64_t created_at() const { return created_at_; }
    std::uint64_t updated_at() const { return updated_at_; }
    const std::string& written_by() const { return written_by_; }
    bool loaded_from_disk() const { return loaded_from_disk_; }

private:
    mutable std::mutex mutex_;
    std::map<std::string, CacheEntry> entries_;
    std::string source_;
    std::string written_by_;
    std::uint64_t created_at_ = 0;
    std::uint64_t updated_at_ = 0;
    bool loaded_from_disk_ = false;
};

const char* entry_state_name(EntryState state);
EntryState entry_state_from_name(std::string_view name);
ResourceKind resource_kind_from_name(std::string_view name);

/// Current Unix time in seconds.
std::uint64_t unix_now();

} // namespace surl
