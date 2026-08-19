#include "surl/cmd/commands.hpp"

#include "surl/mirror/cache.hpp"
#include "surl/util/fsutil.hpp"
#include "surl/util/json.hpp"
#include "surl/util/log.hpp"
#include "surl/util/strings.hpp"

#include <algorithm>
#include <map>

namespace fs = std::filesystem;

namespace surl {
namespace {

std::string format_unix_time(std::uint64_t seconds) {
    if (seconds == 0) return "unknown";
    const std::time_t value = static_cast<std::time_t>(seconds);
    std::tm parts{};
#ifdef _WIN32
    if (gmtime_s(&parts, &value) != 0) return "unknown";
#else
    if (gmtime_r(&value, &parts) == nullptr) return "unknown";
#endif
    char buffer[32];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%SZ", &parts) == 0) {
        return "unknown";
    }
    return buffer;
}

} // namespace

int cmd_inspect(const Options& options) {
    fs::path root;
    std::string error;
    if (!resolve_mirror_directory(options, root, error)) {
        log().error(error);
        return kExitUsage;
    }

    Manifest manifest;
    const fs::path manifest_path = Manifest::default_path(root);
    if (!manifest.load(manifest_path, error)) {
        log().error(error);
        return kExitError;
    }

    std::error_code ec;
    const bool has_manifest = fs::exists(manifest_path, ec);

    const std::vector<CacheEntry> entries = manifest.entries();
    std::map<std::string, std::uint64_t> count_by_kind;
    std::map<std::string, std::uint64_t> bytes_by_kind;
    std::vector<const CacheEntry*> failures;

    for (const CacheEntry& entry : entries) {
        if (entry.state == EntryState::Failed) {
            failures.push_back(&entry);
            continue;
        }
        if (entry.state != EntryState::Done) continue;
        const std::string kind = kind_name(entry.kind);
        count_by_kind[kind] += 1;
        bytes_by_kind[kind] += entry.size;
    }

    const std::uint64_t on_disk_files = directory_file_count(root);
    const std::uint64_t on_disk_bytes = directory_size(root);

    if (options.json_output) {
        Json report = Json::object();
        report.set("directory", Json(path_to_utf8(root)));
        report.set("has_manifest", Json(has_manifest));
        report.set("source", Json(manifest.source()));
        report.set("written_by", Json(manifest.written_by()));
        report.set("created_at", Json(manifest.created_at()));
        report.set("updated_at", Json(manifest.updated_at()));
        report.set("entries", Json(static_cast<std::uint64_t>(entries.size())));
        report.set("files_on_disk", Json(on_disk_files));
        report.set("bytes_on_disk", Json(on_disk_bytes));
        report.set("failed", Json(static_cast<std::uint64_t>(failures.size())));
        report.set("skipped",
                   Json(static_cast<std::uint64_t>(manifest.count_state(EntryState::Skipped))));

        Json by_kind = Json::object();
        for (const auto& [kind, count] : count_by_kind) {
            Json detail = Json::object();
            detail.set("count", Json(count));
            detail.set("bytes", Json(bytes_by_kind[kind]));
            by_kind.set(kind, std::move(detail));
        }
        report.set("by_kind", std::move(by_kind));

        Json failure_list = Json::array();
        for (const CacheEntry* entry : failures) {
            Json item = Json::object();
            item.set("url", Json(entry->url));
            item.set("status", Json(static_cast<std::int64_t>(entry->status)));
            item.set("note", Json(entry->note));
            failure_list.push_back(std::move(item));
        }
        report.set("failures", std::move(failure_list));

        log().out(report.dump(2));
        return kExitOk;
    }

    log().info(log().paint(color::kBold, path_to_utf8(root)));
    if (!has_manifest) {
        log().info("");
        log().warn("no SURL manifest here: this directory was not produced by surl, "
                   "or its .surl folder was removed");
        log().info("  files on disk  " + std::to_string(on_disk_files) + "  (" +
                   human_size(on_disk_bytes) + ")");
        return kExitOk;
    }

    log().info("  source     " + (manifest.source().empty() ? "unknown" : manifest.source()));
    log().info("  created    " + format_unix_time(manifest.created_at()));
    log().info("  updated    " + format_unix_time(manifest.updated_at()));
    log().info("  written by surl " +
               (manifest.written_by().empty() ? "unknown" : manifest.written_by()));
    log().info("");

    log().info("  " + std::string("kind") + std::string(10, ' ') + "count      size");
    log().info("  " + std::string(34, '-'));
    for (const auto& [kind, count] : count_by_kind) {
        std::string row = "  " + kind;
        row.append(14 - std::min<std::size_t>(13, kind.size()), ' ');
        const std::string count_text = std::to_string(count);
        row.append(5 - std::min<std::size_t>(4, count_text.size()), ' ');
        row += count_text;
        const std::string size_text = human_size(bytes_by_kind[kind]);
        row.append(11 - std::min<std::size_t>(10, size_text.size()), ' ');
        row += size_text;
        log().info(row);
    }

    log().info("");
    log().info("  files on disk  " + std::to_string(on_disk_files) + "  (" +
               human_size(on_disk_bytes) + ")");
    log().info("  skipped        " +
               std::to_string(manifest.count_state(EntryState::Skipped)));
    log().info("  failed         " + std::to_string(failures.size()));

    if (!failures.empty()) {
        log().info("");
        log().info(log().paint(color::kYellow, "  failures"));
        std::size_t shown = 0;
        for (const CacheEntry* entry : failures) {
            if (shown++ >= 15) {
                log().info("    ... and " + std::to_string(failures.size() - 15) + " more");
                break;
            }
            log().info("    " + entry->url + "  (" + entry->note + ")");
        }
    }

    return failures.empty() ? kExitOk : kExitPartial;
}

} // namespace surl
