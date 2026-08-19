#include "surl/util/mime.hpp"

#include "surl/util/strings.hpp"

#include <unordered_map>

namespace surl {
namespace {

struct MimeEntry {
    const char* extension;
    const char* content_type;
    ResourceKind kind;
};

// Ordered so that the first match for a content type is the canonical extension.
constexpr MimeEntry kMimeTable[] = {
    {".html", "text/html", ResourceKind::Html},
    {".htm", "text/html", ResourceKind::Html},
    {".xhtml", "application/xhtml+xml", ResourceKind::Html},
    {".css", "text/css", ResourceKind::Css},
    {".js", "text/javascript", ResourceKind::JavaScript},
    {".mjs", "text/javascript", ResourceKind::JavaScript},
    {".cjs", "text/javascript", ResourceKind::JavaScript},
    {".json", "application/json", ResourceKind::Other},
    {".map", "application/json", ResourceKind::Other},
    {".webmanifest", "application/manifest+json", ResourceKind::Manifest},
    {".xml", "application/xml", ResourceKind::Other},
    {".txt", "text/plain", ResourceKind::Other},
    {".svg", "image/svg+xml", ResourceKind::Image},
    {".png", "image/png", ResourceKind::Image},
    {".jpg", "image/jpeg", ResourceKind::Image},
    {".jpeg", "image/jpeg", ResourceKind::Image},
    {".gif", "image/gif", ResourceKind::Image},
    {".webp", "image/webp", ResourceKind::Image},
    {".avif", "image/avif", ResourceKind::Image},
    {".bmp", "image/bmp", ResourceKind::Image},
    {".ico", "image/x-icon", ResourceKind::Image},
    {".woff", "font/woff", ResourceKind::Font},
    {".woff2", "font/woff2", ResourceKind::Font},
    {".ttf", "font/ttf", ResourceKind::Font},
    {".otf", "font/otf", ResourceKind::Font},
    {".eot", "application/vnd.ms-fontobject", ResourceKind::Font},
    {".mp4", "video/mp4", ResourceKind::Media},
    {".webm", "video/webm", ResourceKind::Media},
    {".ogv", "video/ogg", ResourceKind::Media},
    {".mp3", "audio/mpeg", ResourceKind::Media},
    {".ogg", "audio/ogg", ResourceKind::Media},
    {".wav", "audio/wav", ResourceKind::Media},
    {".m4a", "audio/mp4", ResourceKind::Media},
    {".flac", "audio/flac", ResourceKind::Media},
    {".pdf", "application/pdf", ResourceKind::Other},
    {".zip", "application/zip", ResourceKind::Other},
    {".wasm", "application/wasm", ResourceKind::Other},
};

std::string normalize_extension(std::string_view extension) {
    std::string ext = to_lower(trim(extension));
    if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
    return ext;
}

} // namespace

std::string content_type_essence(std::string_view content_type) {
    const std::size_t semicolon = content_type.find(';');
    std::string_view head =
        (semicolon == std::string_view::npos) ? content_type : content_type.substr(0, semicolon);
    return to_lower(trim(head));
}

ResourceKind kind_from_content_type(std::string_view content_type) {
    const std::string essence = content_type_essence(content_type);
    if (essence.empty()) return ResourceKind::Other;

    if (essence == "text/html" || essence == "application/xhtml+xml") return ResourceKind::Html;
    if (essence == "text/css") return ResourceKind::Css;
    if (essence == "text/javascript" || essence == "application/javascript" ||
        essence == "application/x-javascript" || essence == "text/ecmascript" ||
        essence == "application/ecmascript") {
        return ResourceKind::JavaScript;
    }
    if (essence == "application/manifest+json") return ResourceKind::Manifest;

    if (istarts_with(essence, "image/")) return ResourceKind::Image;
    if (istarts_with(essence, "font/")) return ResourceKind::Font;
    if (istarts_with(essence, "video/") || istarts_with(essence, "audio/")) {
        return ResourceKind::Media;
    }
    if (essence == "application/font-woff" || essence == "application/x-font-ttf" ||
        essence == "application/vnd.ms-fontobject") {
        return ResourceKind::Font;
    }
    return ResourceKind::Other;
}

ResourceKind kind_from_extension(std::string_view extension) {
    const std::string ext = normalize_extension(extension);
    for (const MimeEntry& entry : kMimeTable) {
        if (ext == entry.extension) return entry.kind;
    }
    return ResourceKind::Other;
}

std::string extension_for_content_type(std::string_view content_type) {
    const std::string essence = content_type_essence(content_type);
    if (essence.empty()) return "";
    for (const MimeEntry& entry : kMimeTable) {
        if (essence == entry.content_type) return entry.extension;
    }
    // A few aliases that are common enough to be worth handling.
    if (essence == "application/javascript" || essence == "application/x-javascript") {
        return ".js";
    }
    if (essence == "image/jpg") return ".jpg";
    if (essence == "image/vnd.microsoft.icon") return ".ico";
    return "";
}

std::string content_type_for_extension(std::string_view extension) {
    const std::string ext = normalize_extension(extension);
    for (const MimeEntry& entry : kMimeTable) {
        if (ext == entry.extension) {
            const ResourceKind kind = entry.kind;
            std::string type = entry.content_type;
            // Text formats need an explicit charset or browsers guess wrong.
            if (kind == ResourceKind::Html || kind == ResourceKind::Css ||
                kind == ResourceKind::JavaScript || ext == ".json" || ext == ".txt" ||
                ext == ".xml" || ext == ".map" || ext == ".webmanifest") {
                type += "; charset=utf-8";
            }
            return type;
        }
    }
    return "application/octet-stream";
}

const char* kind_name(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Html: return "html";
    case ResourceKind::Css: return "css";
    case ResourceKind::JavaScript: return "js";
    case ResourceKind::Image: return "image";
    case ResourceKind::Font: return "font";
    case ResourceKind::Media: return "media";
    case ResourceKind::Manifest: return "manifest";
    case ResourceKind::Other: break;
    }
    return "other";
}

} // namespace surl
