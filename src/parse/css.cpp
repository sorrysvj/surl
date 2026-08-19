#include "surl/parse/css.hpp"

#include "surl/util/strings.hpp"

#include <cctype>

namespace surl {
namespace {

inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/// Removes CSS backslash escapes from a URL token.
std::string unescape_css(std::string_view text) {
    if (text.find('\\') == std::string_view::npos) return std::string(text);
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            ++i;
            out.push_back(text[i]);
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

/// Skips a /* comment */ starting at @p i, returning the position after it.
std::size_t skip_comment(std::string_view css, std::size_t i) {
    const std::size_t end = css.find("*/", i + 2);
    return (end == std::string_view::npos) ? css.size() : end + 2;
}

} // namespace

std::string escape_css_url(std::string_view url) {
    std::string out;
    out.reserve(url.size() + 4);
    for (const char c : url) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::vector<CssLink> scan_css(std::string_view css) {
    std::vector<CssLink> links;
    const std::size_t n = css.size();
    std::size_t i = 0;

    while (i < n) {
        const char c = css[i];

        if (c == '/' && i + 1 < n && css[i + 1] == '*') {
            i = skip_comment(css, i);
            continue;
        }

        // @import "url"; or @import url("...");
        if (c == '@' && istarts_with(css.substr(i), "@import")) {
            std::size_t j = i + 7;
            while (j < n && is_space(css[j])) ++j;

            // @import url(...) is handled by the url( branch below; only the
            // bare-string form needs handling here.
            if (j < n && (css[j] == '"' || css[j] == '\'')) {
                const char quote = css[j];
                ++j;
                const std::size_t start = j;
                while (j < n && css[j] != quote) {
                    if (css[j] == '\\' && j + 1 < n) ++j;
                    ++j;
                }
                CssLink link;
                link.offset = start;
                link.length = j - start;
                link.value = unescape_css(css.substr(start, link.length));
                link.is_import = true;
                if (link.length > 0) links.push_back(std::move(link));
                i = (j < n) ? j + 1 : n;
                continue;
            }
            i += 7;
            continue;
        }

        // url( ... )
        if ((c == 'u' || c == 'U') && i + 4 <= n && istarts_with(css.substr(i), "url")) {
            // Make sure this is a function token, not part of an identifier.
            const bool preceded_by_ident =
                i > 0 && (std::isalnum(static_cast<unsigned char>(css[i - 1])) ||
                          css[i - 1] == '-' || css[i - 1] == '_');
            std::size_t j = i + 3;
            while (j < n && is_space(css[j])) ++j;
            if (preceded_by_ident || j >= n || css[j] != '(') {
                ++i;
                continue;
            }
            ++j; // consume '('
            while (j < n && is_space(css[j])) ++j;

            if (j < n && (css[j] == '"' || css[j] == '\'')) {
                const char quote = css[j];
                ++j;
                const std::size_t start = j;
                while (j < n && css[j] != quote) {
                    if (css[j] == '\\' && j + 1 < n) ++j;
                    ++j;
                }
                CssLink link;
                link.offset = start;
                link.length = j - start;
                link.value = unescape_css(css.substr(start, link.length));
                if (link.length > 0) links.push_back(std::move(link));
                i = (j < n) ? j + 1 : n;
            } else {
                const std::size_t start = j;
                while (j < n && css[j] != ')' && !is_space(css[j])) {
                    if (css[j] == '\\' && j + 1 < n) ++j;
                    ++j;
                }
                CssLink link;
                link.offset = start;
                link.length = j - start;
                link.value = trim(unescape_css(css.substr(start, link.length)));
                if (link.length > 0) links.push_back(std::move(link));
                i = j;
            }
            continue;
        }

        // Skip over string literals so their contents are never mistaken for
        // a url() token.
        if (c == '"' || c == '\'') {
            const char quote = c;
            ++i;
            while (i < n && css[i] != quote) {
                if (css[i] == '\\' && i + 1 < n) ++i;
                ++i;
            }
            if (i < n) ++i;
            continue;
        }

        ++i;
    }
    return links;
}

} // namespace surl
