#pragma once

#include "surl/util/mime.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace surl {

/// What a discovered reference is for. The crawler treats navigation links and
/// assets differently: assets are always fetched, navigation links only when
/// recursion is enabled and the depth budget allows.
enum class LinkRole {
    Navigation,  ///< a[href], area[href], iframe/frame[src], form[action]
    Asset,       ///< img, script, link, video, audio, object, embed, ...
    SrcSet,      ///< srcset / imagesrcset: a comma-separated candidate list
    MetaRefresh, ///< <meta http-equiv=refresh content="5; url=...">
    InlineStyle, ///< style="..." attribute holding CSS declarations
    StyleBlock,  ///< the text inside a <style> element
    Base         ///< <base href>: changes resolution, never rewritten
};

/// One reference found in an HTML document, located by byte offset so that the
/// rewriter can patch the original bytes and leave everything else untouched.
struct HtmlLink {
    std::size_t offset = 0; ///< byte offset of the raw attribute value
    std::size_t length = 0; ///< byte length of the raw attribute value
    std::string value;      ///< the value with HTML entities decoded
    LinkRole role = LinkRole::Asset;
    std::string tag;        ///< lower-cased element name
    std::string attr;       ///< lower-cased attribute name
    ResourceKind hint = ResourceKind::Other; ///< guessed from tag and rel
};

struct HtmlScan {
    std::vector<HtmlLink> links;
    /// Value of <base href> when the document declares one.
    std::string base_href;
    bool has_base = false;
    /// Byte offset of the end of <head>, used to inject a <base> when needed.
    std::size_t head_end_offset = 0;
    bool has_head_end = false;
};

/// Scans an HTML document for every reference SURL knows how to mirror.
/// The scan is deliberately lenient: real-world markup is malformed constantly,
/// and a parser that gives up loses the whole page.
HtmlScan scan_html(std::string_view html);

/// Decodes the HTML entities that legitimately appear inside URLs.
std::string decode_html_entities(std::string_view text);

/// Escapes a string for use as a double-quoted attribute value.
std::string encode_html_attribute(std::string_view text);

/// A single candidate inside a srcset attribute.
struct SrcSetCandidate {
    std::string url;
    std::string descriptor; ///< "2x", "640w", or empty
};

/// Parses a srcset attribute into its candidates, preserving their order.
std::vector<SrcSetCandidate> parse_srcset(std::string_view value);

/// Rebuilds a srcset attribute value from candidates.
std::string build_srcset(const std::vector<SrcSetCandidate>& candidates);

/// Extracts the URL from a meta refresh content value ("5; url=/next").
/// Returns false when the value carries no URL. On success @p url_offset is the
/// offset of the URL relative to the start of @p content.
bool parse_meta_refresh(std::string_view content, std::string& url,
                        std::size_t& url_offset, std::size_t& url_length);

} // namespace surl
