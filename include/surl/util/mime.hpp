#pragma once

#include <string>
#include <string_view>

namespace surl {

/// Broad classification of a downloaded resource, used to decide whether the
/// body needs link rewriting and which crawl limits apply.
enum class ResourceKind {
    Html,
    Css,
    JavaScript,
    Image,
    Font,
    Media,
    Manifest,
    Other
};

/// Maps a Content-Type header (parameters tolerated) to a resource kind.
ResourceKind kind_from_content_type(std::string_view content_type);

/// Maps a file extension (with or without the dot) to a resource kind.
ResourceKind kind_from_extension(std::string_view extension);

/// Strips parameters from a Content-Type header: "text/html; charset=utf-8".
std::string content_type_essence(std::string_view content_type);

/// Preferred file extension (including the dot) for a Content-Type, or "" when
/// nothing sensible can be inferred.
std::string extension_for_content_type(std::string_view content_type);

/// Content-Type to serve for a given file extension, used by `surl serve`.
std::string content_type_for_extension(std::string_view extension);

const char* kind_name(ResourceKind kind);

} // namespace surl
