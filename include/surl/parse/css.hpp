#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace surl {

/// One URL reference inside a stylesheet, located by byte offset so the
/// rewriter can patch it in place.
struct CssLink {
    std::size_t offset = 0; ///< offset of the URL text itself, without quotes
    std::size_t length = 0;
    std::string value;      ///< the URL as written, unescaped
    bool is_import = false; ///< came from an @import rule
};

/// Finds every url(...) and @import reference in a stylesheet. Comments and
/// string contents that are not URLs are skipped.
std::vector<CssLink> scan_css(std::string_view css);

/// Escapes a URL for embedding inside url("...") in CSS.
std::string escape_css_url(std::string_view url);

} // namespace surl
