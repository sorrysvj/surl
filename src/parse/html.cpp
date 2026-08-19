#include "surl/parse/html.hpp"

#include "surl/util/strings.hpp"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>

namespace surl {
namespace {

inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

inline bool is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
}

/// Attribute names that hold exactly one URL, keyed by element.
struct AttrRule {
    const char* tag;   // nullptr means "any element"
    const char* attr;
    LinkRole role;
    ResourceKind hint;
};

constexpr AttrRule kAttrRules[] = {
    // Navigation
    {"a", "href", LinkRole::Navigation, ResourceKind::Html},
    {"area", "href", LinkRole::Navigation, ResourceKind::Html},
    {"iframe", "src", LinkRole::Navigation, ResourceKind::Html},
    {"frame", "src", LinkRole::Navigation, ResourceKind::Html},
    {"form", "action", LinkRole::Navigation, ResourceKind::Html},

    // Assets
    {"img", "src", LinkRole::Asset, ResourceKind::Image},
    {"img", "lowsrc", LinkRole::Asset, ResourceKind::Image},
    {"image", "href", LinkRole::Asset, ResourceKind::Image},
    {"input", "src", LinkRole::Asset, ResourceKind::Image},
    {"script", "src", LinkRole::Asset, ResourceKind::JavaScript},
    {"embed", "src", LinkRole::Asset, ResourceKind::Other},
    {"object", "data", LinkRole::Asset, ResourceKind::Other},
    {"source", "src", LinkRole::Asset, ResourceKind::Media},
    {"track", "src", LinkRole::Asset, ResourceKind::Other},
    {"video", "src", LinkRole::Asset, ResourceKind::Media},
    {"video", "poster", LinkRole::Asset, ResourceKind::Image},
    {"audio", "src", LinkRole::Asset, ResourceKind::Media},
    {"use", "href", LinkRole::Asset, ResourceKind::Image},
    {"use", "xlink:href", LinkRole::Asset, ResourceKind::Image},
    {"image", "xlink:href", LinkRole::Asset, ResourceKind::Image},
    {"body", "background", LinkRole::Asset, ResourceKind::Image},
    {"table", "background", LinkRole::Asset, ResourceKind::Image},
    {"td", "background", LinkRole::Asset, ResourceKind::Image},
};

constexpr AttrRule kSrcSetRules[] = {
    {"img", "srcset", LinkRole::SrcSet, ResourceKind::Image},
    {"source", "srcset", LinkRole::SrcSet, ResourceKind::Image},
    {"link", "imagesrcset", LinkRole::SrcSet, ResourceKind::Image},
};

/// Finds the "</tag" that closes a raw-text element, matching the name
/// case-insensitively. HTML tag names are case-insensitive and legacy markup
/// is full of uppercase ones; a case-sensitive search here would treat the
/// rest of the document as text and lose every link in it.
std::size_t find_closing_tag(std::string_view html, std::string_view tag, std::size_t from) {
    while (from < html.size()) {
        const std::size_t lt = html.find("</", from);
        if (lt == std::string_view::npos) return std::string_view::npos;

        const std::size_t name_start = lt + 2;
        if (name_start + tag.size() <= html.size() &&
            iequals(html.substr(name_start, tag.size()), tag)) {
            const std::size_t after = name_start + tag.size();
            if (after >= html.size() || is_space(html[after]) || html[after] == '>' ||
                html[after] == '/') {
                return lt;
            }
        }
        from = lt + 2;
    }
    return std::string_view::npos;
}

/// Elements whose content is raw text and must not be scanned as markup.
bool is_raw_text_element(std::string_view tag) {
    return tag == "script" || tag == "style" || tag == "textarea" || tag == "title" ||
           tag == "xmp" || tag == "noframes" || tag == "iframe";
}

/// Maps a <link rel> token to the kind of resource it points at, and says
/// whether SURL should download it at all.
ResourceKind kind_for_link_rel(std::string_view rel_value, bool& should_fetch) {
    const std::string rel = to_lower(rel_value);
    should_fetch = false;

    // rel can carry several space-separated tokens.
    for (const std::string& token : split(rel, ' ', false)) {
        if (token == "stylesheet") {
            should_fetch = true;
            return ResourceKind::Css;
        }
        if (token == "icon" || token == "shortcut" || token == "apple-touch-icon" ||
            token == "apple-touch-icon-precomposed" || token == "mask-icon" ||
            token == "apple-touch-startup-image") {
            should_fetch = true;
            return ResourceKind::Image;
        }
        if (token == "manifest") {
            should_fetch = true;
            return ResourceKind::Manifest;
        }
        if (token == "preload" || token == "prefetch" || token == "modulepreload") {
            should_fetch = true;
            return ResourceKind::Other;
        }
        if (token == "canonical" || token == "alternate" || token == "next" ||
            token == "prev" || token == "author" || token == "help" ||
            token == "license" || token == "search") {
            // Rewritten so the mirror stays self-consistent, but not fetched
            // as an asset; the crawler decides via the Navigation role.
            should_fetch = false;
            return ResourceKind::Html;
        }
    }
    return ResourceKind::Other;
}

const std::unordered_map<std::string, const char*>& named_entities() {
    static const std::unordered_map<std::string, const char*> kEntities = {
        {"amp", "&"},   {"lt", "<"},    {"gt", ">"},    {"quot", "\""},
        {"apos", "'"},  {"nbsp", "\xC2\xA0"}, {"sol", "/"},   {"colon", ":"},
        {"equals", "="}, {"quest", "?"}, {"num", "#"},   {"lpar", "("},
        {"rpar", ")"},  {"comma", ","}, {"semi", ";"},  {"period", "."},
    };
    return kEntities;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp == 0 || cp > 0x10FFFF) return;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

struct Attribute {
    std::string name;       // lower-cased
    std::string raw_value;  // exactly as written, without quotes
    std::size_t value_offset = 0;
    std::size_t value_length = 0;
    bool has_value = false;
};

} // namespace

std::string decode_html_entities(std::string_view text) {
    if (text.find('&') == std::string_view::npos) return std::string(text);

    std::string out;
    out.reserve(text.size());

    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }

        // Entities are at most a handful of characters; anything longer is a
        // literal ampersand.
        const std::size_t limit = std::min<std::size_t>(text.size(), i + 12);
        std::size_t end = std::string_view::npos;
        for (std::size_t j = i + 1; j < limit; ++j) {
            if (text[j] == ';') {
                end = j;
                break;
            }
            if (is_space(text[j]) || text[j] == '&') break;
        }
        if (end == std::string_view::npos) {
            out.push_back('&');
            continue;
        }

        const std::string_view body = text.substr(i + 1, end - i - 1);
        if (body.empty()) {
            out.push_back('&');
            continue;
        }

        if (body.front() == '#') {
            std::uint32_t cp = 0;
            if (body.size() > 2 && (body[1] == 'x' || body[1] == 'X')) {
                cp = static_cast<std::uint32_t>(
                    std::strtoul(std::string(body.substr(2)).c_str(), nullptr, 16));
            } else if (body.size() > 1) {
                cp = static_cast<std::uint32_t>(
                    std::strtoul(std::string(body.substr(1)).c_str(), nullptr, 10));
            }
            if (cp != 0) {
                append_utf8(out, cp);
                i = end;
                continue;
            }
            out.push_back('&');
            continue;
        }

        const auto it = named_entities().find(to_lower(body));
        if (it != named_entities().end()) {
            out.append(it->second);
            i = end;
            continue;
        }
        out.push_back('&');
    }
    return out;
}

std::string encode_html_attribute(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
        case '&': out.append("&amp;"); break;
        case '"': out.append("&quot;"); break;
        case '\'': out.append("&#39;"); break;
        case '<': out.append("&lt;"); break;
        case '>': out.append("&gt;"); break;
        default: out.push_back(c);
        }
    }
    return out;
}

std::vector<SrcSetCandidate> parse_srcset(std::string_view value) {
    std::vector<SrcSetCandidate> out;
    std::size_t i = 0;

    while (i < value.size()) {
        while (i < value.size() && (is_space(value[i]) || value[i] == ',')) ++i;
        if (i >= value.size()) break;

        const std::size_t url_start = i;
        while (i < value.size() && !is_space(value[i])) {
            // A comma ends the URL unless it is part of a data: URI, which the
            // caller will skip anyway.
            if (value[i] == ',') break;
            ++i;
        }
        SrcSetCandidate candidate;
        candidate.url = std::string(value.substr(url_start, i - url_start));
        if (candidate.url.empty()) continue;

        while (i < value.size() && is_space(value[i])) ++i;
        const std::size_t desc_start = i;
        while (i < value.size() && value[i] != ',') ++i;
        candidate.descriptor = trim(value.substr(desc_start, i - desc_start));
        if (i < value.size()) ++i; // skip the comma

        out.push_back(std::move(candidate));
    }
    return out;
}

std::string build_srcset(const std::vector<SrcSetCandidate>& candidates) {
    std::string out;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i) out.append(", ");
        out.append(candidates[i].url);
        if (!candidates[i].descriptor.empty()) {
            out.push_back(' ');
            out.append(candidates[i].descriptor);
        }
    }
    return out;
}

bool parse_meta_refresh(std::string_view content, std::string& url, std::size_t& url_offset,
                        std::size_t& url_length) {
    // Format: "<seconds>[; url=<target>]", with generous whitespace and casing.
    const std::size_t semicolon = content.find(';');
    if (semicolon == std::string_view::npos) return false;

    std::size_t i = semicolon + 1;
    while (i < content.size() && is_space(content[i])) ++i;
    if (!istarts_with(content.substr(i), "url")) return false;
    i += 3;
    while (i < content.size() && is_space(content[i])) ++i;
    if (i >= content.size() || content[i] != '=') return false;
    ++i;
    while (i < content.size() && is_space(content[i])) ++i;
    if (i >= content.size()) return false;

    char quote = 0;
    if (content[i] == '"' || content[i] == '\'') {
        quote = content[i];
        ++i;
    }

    const std::size_t start = i;
    while (i < content.size()) {
        if (quote != 0 && content[i] == quote) break;
        if (quote == 0 && is_space(content[i])) break;
        ++i;
    }

    url_offset = start;
    url_length = i - start;
    url = decode_html_entities(content.substr(start, url_length));
    return url_length > 0;
}

HtmlScan scan_html(std::string_view html) {
    HtmlScan scan;
    const std::size_t n = html.size();
    std::size_t i = 0;

    while (i < n) {
        const std::size_t lt = html.find('<', i);
        if (lt == std::string_view::npos) break;
        i = lt + 1;
        if (i >= n) break;

        // Comments, doctypes, CDATA.
        if (html[i] == '!') {
            if (html.compare(i, 3, "!--") == 0) {
                const std::size_t end = html.find("-->", i + 3);
                i = (end == std::string_view::npos) ? n : end + 3;
            } else {
                const std::size_t end = html.find('>', i);
                i = (end == std::string_view::npos) ? n : end + 1;
            }
            continue;
        }

        // Closing tags carry no attributes.
        if (html[i] == '/') {
            const std::size_t end = html.find('>', i);
            i = (end == std::string_view::npos) ? n : end + 1;
            continue;
        }

        if (!is_name_start(html[i])) continue;

        const std::size_t name_start = i;
        while (i < n && !is_space(html[i]) && html[i] != '>' && html[i] != '/') ++i;
        const std::string tag = to_lower(html.substr(name_start, i - name_start));

        // Collect attributes up to the closing '>'.
        std::vector<Attribute> attributes;
        bool self_closing = false;
        while (i < n) {
            while (i < n && is_space(html[i])) ++i;
            if (i >= n) break;
            if (html[i] == '>') {
                ++i;
                break;
            }
            if (html[i] == '/') {
                self_closing = true;
                ++i;
                continue;
            }

            const std::size_t attr_start = i;
            while (i < n && !is_space(html[i]) && html[i] != '=' && html[i] != '>' &&
                   html[i] != '/') {
                ++i;
            }
            Attribute attribute;
            attribute.name = to_lower(html.substr(attr_start, i - attr_start));
            if (attribute.name.empty()) {
                ++i;
                continue;
            }

            while (i < n && is_space(html[i])) ++i;
            if (i < n && html[i] == '=') {
                ++i;
                while (i < n && is_space(html[i])) ++i;
                if (i < n && (html[i] == '"' || html[i] == '\'')) {
                    const char quote = html[i];
                    ++i;
                    const std::size_t value_start = i;
                    while (i < n && html[i] != quote) ++i;
                    attribute.value_offset = value_start;
                    attribute.value_length = i - value_start;
                    attribute.raw_value = std::string(html.substr(value_start, i - value_start));
                    attribute.has_value = true;
                    if (i < n) ++i; // closing quote
                } else {
                    const std::size_t value_start = i;
                    while (i < n && !is_space(html[i]) && html[i] != '>') ++i;
                    attribute.value_offset = value_start;
                    attribute.value_length = i - value_start;
                    attribute.raw_value = std::string(html.substr(value_start, i - value_start));
                    attribute.has_value = true;
                }
            }
            attributes.push_back(std::move(attribute));
        }

        const auto find_attr = [&](std::string_view name) -> const Attribute* {
            for (const Attribute& attribute : attributes) {
                if (attribute.name == name) return &attribute;
            }
            return nullptr;
        };

        const auto emit = [&](const Attribute& attribute, LinkRole role, ResourceKind hint) {
            if (!attribute.has_value || attribute.value_length == 0) return;
            HtmlLink link;
            link.offset = attribute.value_offset;
            link.length = attribute.value_length;
            link.value = decode_html_entities(attribute.raw_value);
            link.role = role;
            link.tag = tag;
            link.attr = attribute.name;
            link.hint = hint;
            scan.links.push_back(std::move(link));
        };

        if (tag == "base") {
            if (const Attribute* href = find_attr("href"); href != nullptr && href->has_value) {
                scan.base_href = decode_html_entities(href->raw_value);
                scan.has_base = true;
                emit(*href, LinkRole::Base, ResourceKind::Other);
            }
        } else if (tag == "link") {
            const Attribute* href = find_attr("href");
            if (href != nullptr) {
                bool should_fetch = false;
                const Attribute* rel = find_attr("rel");
                const ResourceKind kind =
                    rel != nullptr ? kind_for_link_rel(rel->raw_value, should_fetch)
                                   : ResourceKind::Other;
                emit(*href, should_fetch ? LinkRole::Asset : LinkRole::Navigation, kind);
            }
        } else if (tag == "meta") {
            const Attribute* equiv = find_attr("http-equiv");
            const Attribute* content = find_attr("content");
            if (equiv != nullptr && content != nullptr && content->has_value &&
                iequals(trim(equiv->raw_value), "refresh")) {
                std::string target;
                std::size_t url_offset = 0;
                std::size_t url_length = 0;
                if (parse_meta_refresh(content->raw_value, target, url_offset, url_length)) {
                    HtmlLink link;
                    link.offset = content->value_offset + url_offset;
                    link.length = url_length;
                    link.value = target;
                    link.role = LinkRole::MetaRefresh;
                    link.tag = tag;
                    link.attr = "content";
                    link.hint = ResourceKind::Html;
                    scan.links.push_back(std::move(link));
                }
            }
            // <meta property="og:image" content="..."> and friends.
            const Attribute* property = find_attr("property");
            const Attribute* name_attr = find_attr("name");
            if (content != nullptr && content->has_value) {
                const std::string key = to_lower(
                    property != nullptr ? property->raw_value
                                        : (name_attr != nullptr ? name_attr->raw_value : ""));
                if (key == "og:image" || key == "og:image:url" || key == "twitter:image" ||
                    key == "og:video" || key == "og:audio" || key == "msapplication-tileimage") {
                    emit(*content, LinkRole::Asset, ResourceKind::Image);
                }
            }
        } else {
            for (const AttrRule& rule : kAttrRules) {
                if (tag != rule.tag) continue;
                if (const Attribute* attribute = find_attr(rule.attr); attribute != nullptr) {
                    emit(*attribute, rule.role, rule.hint);
                }
            }
            for (const AttrRule& rule : kSrcSetRules) {
                if (tag != rule.tag) continue;
                if (const Attribute* attribute = find_attr(rule.attr); attribute != nullptr) {
                    emit(*attribute, LinkRole::SrcSet, rule.hint);
                }
            }
        }

        // Inline style attributes carry CSS urls on any element.
        if (const Attribute* style = find_attr("style");
            style != nullptr && style->has_value && style->value_length > 0) {
            emit(*style, LinkRole::InlineStyle, ResourceKind::Css);
        }

        // Raw-text elements: skip their content, but capture <style> bodies.
        if (!self_closing && is_raw_text_element(tag)) {
            const std::size_t close_pos = find_closing_tag(html, tag, i);
            // An unterminated raw-text element must not swallow the whole
            // document: fall back to treating the rest as ordinary markup.
            const std::size_t content_end = (close_pos == std::string_view::npos) ? i : close_pos;

            if (tag == "style" && content_end > i) {
                HtmlLink link;
                link.offset = i;
                link.length = content_end - i;
                link.value = std::string(html.substr(i, content_end - i));
                link.role = LinkRole::StyleBlock;
                link.tag = tag;
                link.attr = "";
                link.hint = ResourceKind::Css;
                scan.links.push_back(std::move(link));
            }
            i = content_end;
        }

        if (tag == "head") {
            // Remember where </head> is so a <base> can be injected if needed.
            const std::size_t head_close = find_closing_tag(html, "head", i);
            if (head_close != std::string_view::npos) {
                scan.head_end_offset = head_close;
                scan.has_head_end = true;
            }
        }
    }

    std::stable_sort(scan.links.begin(), scan.links.end(),
                     [](const HtmlLink& a, const HtmlLink& b) { return a.offset < b.offset; });
    return scan;
}

} // namespace surl
