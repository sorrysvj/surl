#include "surl/mirror/pathmap.hpp"

#include "surl/util/fsutil.hpp"
#include "surl/util/hash.hpp"
#include "surl/util/strings.hpp"

#include <algorithm>
#include <cstdio>

namespace fs = std::filesystem;

namespace surl {
namespace {

/// A short, stable token derived from arbitrary text. Used to keep URLs that
/// differ only in their query string from colliding on one file.
std::string short_hash(std::string_view text) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%08llx",
                  static_cast<unsigned long long>(fnv1a64(text) & 0xFFFFFFFFULL));
    return buf;
}

bool has_html_extension(std::string_view name) {
    return iends_with(name, ".html") || iends_with(name, ".htm") ||
           iends_with(name, ".xhtml");
}

/// Splits "name.ext" into stem and extension, treating a leading dot as part
/// of the stem so that ".htaccess" is not read as an extension.
void split_extension(const std::string& name, std::string& stem, std::string& extension) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || name.size() - dot > 12) {
        stem = name;
        extension.clear();
        return;
    }
    stem = name.substr(0, dot);
    extension = name.substr(dot);
}

} // namespace

PathMapper::PathMapper(PathMapConfig config) : config_(std::move(config)) {
    if (config_.index_name.empty()) config_.index_name = "index.html";
    if (config_.external_dir.empty()) config_.external_dir = "_external";
}

std::string PathMapper::relative_path_for(const Url& url, ResourceKind kind) const {
    std::vector<std::string> components;

    // Foreign hosts are parked under a clearly-marked directory so a mirror is
    // obviously "this site plus its third-party assets".
    if (!config_.primary.host.empty() && !url.same_origin_as(config_.primary)) {
        components.push_back(sanitize_path_component(config_.external_dir));
        std::string host_dir = url.host;
        if (url.port != 0) host_dir += "_" + std::to_string(url.port);
        if (url.scheme != "https") host_dir = url.scheme + "_" + host_dir;
        components.push_back(sanitize_path_component(host_dir));
    }

    // Split the URL path, decoding percent-escapes so the mirror is browsable.
    std::string path = url.path;
    if (path.empty()) path = "/";
    const bool directory_style = path.back() == '/';

    for (const std::string& raw : split(path, '/', false)) {
        const std::string decoded = percent_decode(raw);
        const std::string safe = sanitize_path_component(decoded);
        if (!safe.empty()) components.push_back(safe);
    }

    std::string file_name;
    if (directory_style || components.empty()) {
        file_name = config_.index_name;
    } else {
        file_name = components.back();
        components.pop_back();
    }

    std::string stem;
    std::string extension;
    split_extension(file_name, stem, extension);

    // A query string makes this a different resource; fold it into the name.
    if (url.has_query && !url.query.empty()) {
        stem += "@" + short_hash(url.query);
    }

    // HTML that does not already look like HTML gets an extension so that a
    // browser opened on the mirror renders it instead of downloading it.
    if (kind == ResourceKind::Html && !has_html_extension(extension)) {
        if (extension.empty()) {
            extension = ".html";
        } else {
            stem += extension;
            extension = ".html";
        }
    }

    // If the extension is missing entirely, take a hint from the content kind.
    if (extension.empty() && kind != ResourceKind::Other) {
        switch (kind) {
        case ResourceKind::Css: extension = ".css"; break;
        case ResourceKind::JavaScript: extension = ".js"; break;
        case ResourceKind::Manifest: extension = ".webmanifest"; break;
        default: break;
        }
    }

    if (stem.empty()) stem = "index";

    std::string leaf = sanitize_path_component(stem + extension);
    components.push_back(leaf);

    std::string relative = join(components, "/");

    // Guard against pathologically deep or long URLs: past a sane budget,
    // collapse the tail into a hash while keeping the extension readable.
    constexpr std::size_t kMaxRelativeLength = 200;
    if (relative.size() > kMaxRelativeLength) {
        const std::string digest = short_hash(relative);
        std::string keep_stem;
        std::string keep_extension;
        split_extension(leaf, keep_stem, keep_extension);
        std::string prefix = relative.substr(0, 120);
        // Do not cut in the middle of a component.
        const std::size_t last_slash = prefix.rfind('/');
        if (last_slash != std::string::npos) prefix = prefix.substr(0, last_slash);
        relative = prefix + "/_long/" + digest + keep_extension;
    }

    return relative;
}

fs::path PathMapper::absolute_path(std::string_view relative) const {
    fs::path out = config_.root;
    for (const std::string& component : split(relative, '/', false)) {
        out /= utf8_to_path(component);
    }
    return out;
}

fs::path PathMapper::absolute_path_for(const Url& url, ResourceKind kind) const {
    return absolute_path(relative_path_for(url, kind));
}

std::string PathMapper::relative_link(std::string_view from_relative,
                                      std::string_view to_relative) {
    const std::vector<std::string> from = split(from_relative, '/', false);
    const std::vector<std::string> to = split(to_relative, '/', false);

    // Compare directories only: the last component of "from" is the document.
    const std::size_t from_dirs = from.empty() ? 0 : from.size() - 1;

    std::size_t common = 0;
    while (common < from_dirs && common + 1 < to.size() && from[common] == to[common]) {
        ++common;
    }

    std::vector<std::string> parts;
    for (std::size_t i = common; i < from_dirs; ++i) parts.push_back("..");
    for (std::size_t i = common; i < to.size(); ++i) {
        // Percent-encode each component so spaces and other awkward bytes are
        // legal inside an href; '/' is added by join, never encoded.
        parts.push_back(percent_encode(to[i], "!$()*+,;=:@-._~"));
    }

    if (parts.empty()) {
        // The link points at the document itself.
        return to.empty() ? "." : percent_encode(to.back(), "!$()*+,;=:@-._~");
    }
    return join(parts, "/");
}

} // namespace surl
