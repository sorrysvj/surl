#include "surl/cmd/commands.hpp"

#include "surl/mirror/cache.hpp"
#include "surl/util/fsutil.hpp"
#include "surl/util/hash.hpp"
#include "surl/util/json.hpp"
#include "surl/util/log.hpp"
#include "surl/util/strings.hpp"

namespace fs = std::filesystem;

namespace surl {

int cmd_check(const Options& options) {
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
    if (!fs::exists(manifest_path, ec)) {
        log().error("no SURL manifest in " + path_to_utf8(root) +
                    "; there is nothing to check against");
        return kExitError;
    }

    std::vector<std::string> missing;
    std::vector<std::string> corrupted;
    std::uint64_t verified = 0;
    std::uint64_t unverifiable = 0;

    for (const CacheEntry& entry : manifest.entries()) {
        if (entry.state != EntryState::Done || entry.path.empty()) continue;

        fs::path file = root;
        for (const std::string& component : split(entry.path, '/', false)) {
            file /= utf8_to_path(component);
        }

        if (!fs::exists(file, ec)) {
            missing.push_back(entry.path);
            continue;
        }
        if (entry.sha256.empty()) {
            ++unverifiable;
            continue;
        }

        std::string digest;
        if (!sha256_file(file, digest)) {
            missing.push_back(entry.path);
            continue;
        }
        if (digest != entry.sha256) {
            corrupted.push_back(entry.path);
            continue;
        }
        ++verified;
    }

    if (options.json_output) {
        Json report = Json::object();
        report.set("directory", Json(path_to_utf8(root)));
        report.set("verified", Json(verified));
        report.set("unverifiable", Json(unverifiable));

        Json missing_list = Json::array();
        for (const std::string& path : missing) missing_list.push_back(Json(path));
        report.set("missing", std::move(missing_list));

        Json corrupted_list = Json::array();
        for (const std::string& path : corrupted) corrupted_list.push_back(Json(path));
        report.set("corrupted", std::move(corrupted_list));

        report.set("ok", Json(missing.empty() && corrupted.empty()));
        log().out(report.dump(2));
        return (missing.empty() && corrupted.empty()) ? kExitOk : kExitPartial;
    }

    log().info(log().paint(color::kBold, path_to_utf8(root)));
    log().info("  verified      " + std::to_string(verified));
    if (unverifiable > 0) {
        log().info("  no checksum   " + std::to_string(unverifiable));
    }

    if (!missing.empty()) {
        log().info("");
        log().info(log().paint(color::kRed, "  missing (" + std::to_string(missing.size()) +
                                                ")"));
        for (std::size_t i = 0; i < missing.size() && i < 20; ++i) {
            log().info("    " + missing[i]);
        }
        if (missing.size() > 20) {
            log().info("    ... and " + std::to_string(missing.size() - 20) + " more");
        }
    }

    if (!corrupted.empty()) {
        log().info("");
        log().info(log().paint(color::kRed,
                               "  checksum mismatch (" + std::to_string(corrupted.size()) + ")"));
        for (std::size_t i = 0; i < corrupted.size() && i < 20; ++i) {
            log().info("    " + corrupted[i]);
        }
        if (corrupted.size() > 20) {
            log().info("    ... and " + std::to_string(corrupted.size() - 20) + " more");
        }
    }

    if (missing.empty() && corrupted.empty()) {
        log().info("");
        log().info(log().paint(color::kGreen, "  mirror is intact"));
        return kExitOk;
    }

    log().info("");
    log().info("  Re-run with --resume to fetch what is missing, or --force to start over.");
    return kExitPartial;
}

int cmd_clean(const Options& options) {
    fs::path root;
    std::string error;
    if (!resolve_mirror_directory(options, root, error)) {
        log().error(error);
        return kExitUsage;
    }

    const fs::path metadata = Manifest::metadata_dir(root);
    std::error_code ec;

    if (!options.clean_all) {
        // The default is deliberately conservative: only SURL's own bookkeeping
        // goes away, and the mirrored site is left exactly as it is.
        if (!fs::exists(metadata, ec)) {
            log().info("Nothing to clean: " + path_to_utf8(metadata) + " does not exist.");
            return kExitOk;
        }
        const std::uint64_t freed = directory_size(metadata);
        if (options.dry_run) {
            log().info("Would remove " + path_to_utf8(metadata) + " (" + human_size(freed) +
                       ").");
            return kExitOk;
        }
        if (!remove_tree(metadata, error)) {
            log().error(error);
            return kExitError;
        }
        log().info("Removed SURL metadata from " + path_to_utf8(root) + " (" +
                   human_size(freed) + " freed).");
        log().info("The mirrored files are untouched; --resume and --update will no longer "
                   "be able to reuse this run.");
        return kExitOk;
    }

    // --all removes the mirror itself, so it demands an explicit --force.
    if (!fs::exists(Manifest::default_path(root), ec) && !options.force) {
        log().error(path_to_utf8(root) +
                    " has no SURL manifest, so it may not be a mirror. Refusing to delete it. "
                    "Pass --force if you are sure.");
        return kExitUsage;
    }

    const std::uint64_t freed = directory_size(root);
    const std::uint64_t files = directory_file_count(root);

    if (options.dry_run) {
        log().info("Would remove " + std::to_string(files) + " files (" + human_size(freed) +
                   ") from " + path_to_utf8(root) + ".");
        return kExitOk;
    }

    if (!options.force) {
        log().error("Refusing to delete " + std::to_string(files) + " files from " +
                    path_to_utf8(root) + " without --force.");
        return kExitUsage;
    }

    if (!remove_tree(root, error)) {
        log().error(error);
        return kExitError;
    }
    log().info("Removed " + path_to_utf8(root) + " (" + std::to_string(files) + " files, " +
               human_size(freed) + ").");
    return kExitOk;
}

} // namespace surl
