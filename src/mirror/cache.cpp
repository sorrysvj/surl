#include "surl/mirror/cache.hpp"

#include "surl/util/fsutil.hpp"
#include "surl/util/json.hpp"
#include "surl/util/strings.hpp"
#include "surl/version.hpp"

#include <chrono>

namespace fs = std::filesystem;

namespace surl {
namespace {

constexpr int kManifestFormatVersion = 1;

} // namespace

std::uint64_t unix_now() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

const char* entry_state_name(EntryState state) {
    switch (state) {
    case EntryState::Done: return "done";
    case EntryState::Failed: return "failed";
    case EntryState::Skipped: return "skipped";
    }
    return "done";
}

EntryState entry_state_from_name(std::string_view name) {
    if (name == "failed") return EntryState::Failed;
    if (name == "skipped") return EntryState::Skipped;
    return EntryState::Done;
}

ResourceKind resource_kind_from_name(std::string_view name) {
    if (name == "html") return ResourceKind::Html;
    if (name == "css") return ResourceKind::Css;
    if (name == "js") return ResourceKind::JavaScript;
    if (name == "image") return ResourceKind::Image;
    if (name == "font") return ResourceKind::Font;
    if (name == "media") return ResourceKind::Media;
    if (name == "manifest") return ResourceKind::Manifest;
    return ResourceKind::Other;
}

fs::path Manifest::metadata_dir(const fs::path& root) { return root / ".surl"; }

fs::path Manifest::default_path(const fs::path& root) {
    return metadata_dir(root) / "manifest.json";
}

void Manifest::set_source(std::string source) {
    std::lock_guard<std::mutex> lock(mutex_);
    source_ = std::move(source);
}

std::optional<CacheEntry> Manifest::find(const std::string& url) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(url);
    if (it == entries_.end()) return std::nullopt;
    return it->second;
}

void Manifest::put(CacheEntry entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = entry.url;
    entries_[key] = std::move(entry);
}

void Manifest::remove(const std::string& url) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(url);
}

std::size_t Manifest::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::vector<CacheEntry> Manifest::entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CacheEntry> out;
    out.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) out.push_back(entry);
    return out;
}

std::size_t Manifest::count_state(EntryState state) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t count = 0;
    for (const auto& [key, entry] : entries_) {
        if (entry.state == state) ++count;
    }
    return count;
}

std::uint64_t Manifest::total_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::uint64_t total = 0;
    for (const auto& [key, entry] : entries_) {
        if (entry.state == EntryState::Done) total += entry.size;
    }
    return total;
}

bool Manifest::load(const fs::path& file, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    loaded_from_disk_ = false;

    std::error_code ec;
    if (!fs::exists(file, ec)) {
        // Not an error: a first run simply has no manifest yet.
        return true;
    }

    std::string text;
    if (!read_file(file, text)) {
        error = "could not read manifest at " + path_to_utf8(file);
        return false;
    }

    Json root;
    std::string parse_error;
    if (!Json::parse(text, root, parse_error)) {
        error = "manifest at " + path_to_utf8(file) + " is not valid JSON: " + parse_error;
        return false;
    }
    if (!root.is_object()) {
        error = "manifest at " + path_to_utf8(file) + " is not a JSON object";
        return false;
    }

    const std::int64_t format = root["format"].as_int(0);
    if (format > kManifestFormatVersion) {
        error = "manifest was written by a newer SURL (format " + std::to_string(format) +
                "); refusing to downgrade it";
        return false;
    }

    source_ = root["source"].as_string_or("");
    written_by_ = root["surl_version"].as_string_or("");
    created_at_ = root["created_at"].as_uint(0);
    updated_at_ = root["updated_at"].as_uint(0);

    const Json& entries = root["entries"];
    if (entries.is_object()) {
        for (const auto& [url, value] : entries.fields()) {
            if (!value.is_object()) continue;
            CacheEntry entry;
            entry.url = url;
            entry.path = value["path"].as_string_or("");
            entry.etag = value["etag"].as_string_or("");
            entry.last_modified = value["last_modified"].as_string_or("");
            entry.content_type = value["content_type"].as_string_or("");
            entry.sha256 = value["sha256"].as_string_or("");
            entry.note = value["note"].as_string_or("");
            entry.status = static_cast<int>(value["status"].as_int(0));
            entry.size = value["size"].as_uint(0);
            entry.fetched_at = value["fetched_at"].as_uint(0);
            entry.depth = static_cast<int>(value["depth"].as_int(0));
            entry.kind = resource_kind_from_name(value["kind"].as_string_or("other"));
            entry.state = entry_state_from_name(value["state"].as_string_or("done"));
            entries_[url] = std::move(entry);
        }
    }

    loaded_from_disk_ = true;
    return true;
}

bool Manifest::save(const fs::path& file, std::string& error) const {
    std::lock_guard<std::mutex> lock(mutex_);

    Json entries = Json::object();
    for (const auto& [url, entry] : entries_) {
        Json value = Json::object();
        value.set("path", Json(entry.path));
        value.set("status", Json(static_cast<std::int64_t>(entry.status)));
        value.set("size", Json(entry.size));
        value.set("kind", Json(std::string(kind_name(entry.kind))));
        value.set("state", Json(std::string(entry_state_name(entry.state))));
        value.set("depth", Json(static_cast<std::int64_t>(entry.depth)));
        value.set("fetched_at", Json(entry.fetched_at));
        if (!entry.etag.empty()) value.set("etag", Json(entry.etag));
        if (!entry.last_modified.empty()) value.set("last_modified", Json(entry.last_modified));
        if (!entry.content_type.empty()) value.set("content_type", Json(entry.content_type));
        if (!entry.sha256.empty()) value.set("sha256", Json(entry.sha256));
        if (!entry.note.empty()) value.set("note", Json(entry.note));
        entries.set(url, std::move(value));
    }

    Json root = Json::object();
    root.set("format", Json(static_cast<std::int64_t>(kManifestFormatVersion)));
    root.set("surl_version", Json(std::string(SURL_VERSION_STRING)));
    root.set("source", Json(source_));
    root.set("created_at", Json(created_at_ != 0 ? created_at_ : unix_now()));
    root.set("updated_at", Json(unix_now()));
    root.set("entries", std::move(entries));

    if (!ensure_directory(file.parent_path(), error)) return false;
    return write_file_atomic(file, root.dump(2), error);
}

} // namespace surl
