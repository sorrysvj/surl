#pragma once

#include "surl/net/url.hpp"
#include "surl/parse/html.hpp"
#include "surl/util/mime.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace surl {

/// Decides what a discovered reference should become in the mirrored copy.
///
/// Returning a root-relative path means "this resource is part of the mirror";
/// the rewriter turns it into a path relative to the document being written.
/// Returning nullopt means "not mirrored"; the rewriter then substitutes the
/// absolute URL, so a link that was relative in the original still works when
/// the mirror is opened offline.
using LinkResolver =
    std::function<std::optional<std::string>(const Url& target, LinkRole role,
                                             ResourceKind hint)>;

struct RewriteStats {
    std::size_t localised = 0;   ///< references pointed at mirrored files
    std::size_t absolutised = 0; ///< references replaced with absolute URLs
    std::size_t untouched = 0;   ///< anchors, data: URIs, mailto: and friends
};

struct RewriteResult {
    std::string content;
    RewriteStats stats;
};

/// Rewrites every reference in an HTML document.
///
/// @param html               the original bytes, returned unchanged apart from
///                           the reference substrings
/// @param base_url           the URL the document was fetched from, after
///                           redirects; relative references resolve against it
/// @param document_relative  the document's own root-relative path, used to
///                           compute relative links
RewriteResult rewrite_html(std::string_view html, const Url& base_url,
                           std::string_view document_relative,
                           const LinkResolver& resolve);

/// Rewrites every url() and @import in a stylesheet.
RewriteResult rewrite_css(std::string_view css, const Url& base_url,
                          std::string_view document_relative,
                          const LinkResolver& resolve);

/// Collects every reference an HTML document points at, resolved against
/// @p base_url. Used by the crawler to decide what to fetch next.
struct DiscoveredLink {
    Url url;
    LinkRole role;
    ResourceKind hint;
};

std::vector<DiscoveredLink> discover_html_links(std::string_view html, const Url& base_url);
std::vector<DiscoveredLink> discover_css_links(std::string_view css, const Url& base_url);

/// Resolves the effective base URL of a document, honouring <base href>.
Url effective_base(std::string_view html, const Url& document_url);

} // namespace surl
