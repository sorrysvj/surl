#pragma once

#include "surl/net/url.hpp"
#include "surl/util/mime.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace surl {

struct PathMapConfig {
    /// Output root. Everything the mirror writes stays inside it.
    std::filesystem::path root;
    /// The site the mirror is centred on; its files land directly under root.
    Url primary;
    /// Directory name used for resources from other hosts.
    std::string external_dir = "_external";
    /// File name used for directory-style URLs.
    std::string index_name = "index.html";
};

/// Translates URLs into stable, filesystem-safe paths under the output root.
///
/// The mapping has to be deterministic: `--resume` and `--update` re-derive
/// paths from URLs on a later run and must land on the same files. It also has
/// to be injective enough that two different URLs do not overwrite each other,
/// which is why query strings contribute a short hash.
class PathMapper {
public:
    explicit PathMapper(PathMapConfig config);

    /// Path relative to the output root, always using '/' separators.
    std::string relative_path_for(const Url& url, ResourceKind kind) const;

    /// Absolute path on disk for a URL.
    std::filesystem::path absolute_path_for(const Url& url, ResourceKind kind) const;

    /// Absolute path on disk for an already-computed relative path.
    std::filesystem::path absolute_path(std::string_view relative) const;

    const PathMapConfig& config() const { return config_; }

    /// Builds a relative href pointing from the document at @p from_relative to
    /// the file at @p to_relative. Both are root-relative, '/'-separated.
    /// The result is percent-encoded so it survives being put back into HTML.
    static std::string relative_link(std::string_view from_relative,
                                     std::string_view to_relative);

private:
    PathMapConfig config_;
};

} // namespace surl
