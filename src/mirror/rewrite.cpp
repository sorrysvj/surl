#include "surl/mirror/rewrite.hpp"

#include "surl/mirror/pathmap.hpp"
#include "surl/parse/css.hpp"
#include "surl/util/strings.hpp"

#include <algorithm>

namespace surl {
namespace {

struct Replacement {
    std::size_t offset = 0;
    std::size_t length = 0;
    std::string text;
};

/// Splices replacements into the original bytes. Replacements must not overlap.
std::string apply_replacements(std::string_view original,
                               std::vector<Replacement> replacements) {
    std::stable_sort(replacements.begin(), replacements.end(),
                     [](const Replacement& a, const Replacement& b) {
                         return a.offset < b.offset;
                     });

    std::string out;
    out.reserve(original.size() + 64);
    std::size_t cursor = 0;
    for (const Replacement& replacement : replacements) {
        if (replacement.offset < cursor) continue; // defensive: skip overlaps
        out.append(original.substr(cursor, replacement.offset - cursor));
        out.append(replacement.text);
        cursor = replacement.offset + replacement.length;
    }
    if (cursor < original.size()) out.append(original.substr(cursor));
    return out;
}

/// Appends the fragment of a resolved URL to a local link, since "#section"
/// still has to work inside the mirror.
std::string with_fragment(std::string link, const Url& url) {
    if (url.has_fragment && !url.fragment.empty()) {
        link.push_back('#');
        link.append(url.fragment);
    }
    return link;
}

/// Works out what one reference should be replaced with, or nothing at all.
/// Returns false when the reference must be left exactly as written.
bool resolve_reference(std::string_view raw_value, const Url& base,
                       std::string_view document_relative, LinkRole role,
                       ResourceKind hint, const LinkResolver& resolve,
                       std::string& out, RewriteStats& stats) {
    if (is_non_fetchable_reference(raw_value)) {
        ++stats.untouched;
        return false;
    }

    Url target;
    if (!resolve_url(base, raw_value, target) || !target.is_http()) {
        ++stats.untouched;
        return false;
    }

    const std::optional<std::string> mirrored = resolve(target, role, hint);
    if (mirrored.has_value()) {
        out = with_fragment(PathMapper::relative_link(document_relative, *mirrored), target);
        ++stats.localised;
        return true;
    }

    // Not mirrored: freeze the absolute URL so the link keeps working online
    // even though it started life as a relative reference.
    out = target.to_string();
    ++stats.absolutised;
    return true;
}

} // namespace

Url effective_base(std::string_view html, const Url& document_url) {
    const HtmlScan scan = scan_html(html);
    if (!scan.has_base || scan.base_href.empty()) return document_url;

    Url base;
    if (resolve_url(document_url, scan.base_href, base) && base.is_http()) return base;
    return document_url;
}

RewriteResult rewrite_css(std::string_view css, const Url& base_url,
                          std::string_view document_relative, const LinkResolver& resolve) {
    RewriteResult result;
    std::vector<Replacement> replacements;

    for (const CssLink& link : scan_css(css)) {
        std::string replacement;
        if (!resolve_reference(link.value, base_url, document_relative, LinkRole::Asset,
                               link.is_import ? ResourceKind::Css : ResourceKind::Other,
                               resolve, replacement, result.stats)) {
            continue;
        }
        replacements.push_back(Replacement{link.offset, link.length, escape_css_url(replacement)});
    }

    result.content = apply_replacements(css, std::move(replacements));
    return result;
}

RewriteResult rewrite_html(std::string_view html, const Url& document_url,
                           std::string_view document_relative, const LinkResolver& resolve) {
    RewriteResult result;
    const HtmlScan scan = scan_html(html);

    // <base href> changes how every relative reference in the document
    // resolves, so it must be established before anything else is rewritten.
    Url base = document_url;
    if (scan.has_base && !scan.base_href.empty()) {
        Url declared;
        if (resolve_url(document_url, scan.base_href, declared) && declared.is_http()) {
            base = declared;
        }
    }

    std::vector<Replacement> replacements;

    for (const HtmlLink& link : scan.links) {
        switch (link.role) {
        case LinkRole::Base: {
            // Point the base at the document's own directory. Leaving the
            // remote base in place would send every rewritten relative link
            // straight back to the live site.
            replacements.push_back(Replacement{link.offset, link.length, "./"});
            ++result.stats.localised;
            break;
        }

        case LinkRole::StyleBlock: {
            const RewriteResult nested =
                rewrite_css(link.value, base, document_relative, resolve);
            result.stats.localised += nested.stats.localised;
            result.stats.absolutised += nested.stats.absolutised;
            result.stats.untouched += nested.stats.untouched;
            if (nested.content != link.value) {
                replacements.push_back(Replacement{link.offset, link.length, nested.content});
            }
            break;
        }

        case LinkRole::InlineStyle: {
            const RewriteResult nested =
                rewrite_css(link.value, base, document_relative, resolve);
            result.stats.localised += nested.stats.localised;
            result.stats.absolutised += nested.stats.absolutised;
            result.stats.untouched += nested.stats.untouched;
            if (nested.content != link.value) {
                replacements.push_back(
                    Replacement{link.offset, link.length, encode_html_attribute(nested.content)});
            }
            break;
        }

        case LinkRole::SrcSet: {
            std::vector<SrcSetCandidate> candidates = parse_srcset(link.value);
            bool changed = false;
            for (SrcSetCandidate& candidate : candidates) {
                std::string replacement;
                if (resolve_reference(candidate.url, base, document_relative, LinkRole::Asset,
                                      link.hint, resolve, replacement, result.stats)) {
                    candidate.url = replacement;
                    changed = true;
                }
            }
            if (changed) {
                replacements.push_back(Replacement{
                    link.offset, link.length, encode_html_attribute(build_srcset(candidates))});
            }
            break;
        }

        case LinkRole::MetaRefresh:
        case LinkRole::Navigation:
        case LinkRole::Asset: {
            std::string replacement;
            if (!resolve_reference(link.value, base, document_relative, link.role, link.hint,
                                   resolve, replacement, result.stats)) {
                break;
            }
            replacements.push_back(
                Replacement{link.offset, link.length, encode_html_attribute(replacement)});
            break;
        }
        }
    }

    result.content = apply_replacements(html, std::move(replacements));
    return result;
}

std::vector<DiscoveredLink> discover_html_links(std::string_view html, const Url& document_url) {
    std::vector<DiscoveredLink> out;
    const HtmlScan scan = scan_html(html);

    Url base = document_url;
    if (scan.has_base && !scan.base_href.empty()) {
        Url declared;
        if (resolve_url(document_url, scan.base_href, declared) && declared.is_http()) {
            base = declared;
        }
    }

    const auto add = [&](std::string_view raw, LinkRole role, ResourceKind hint) {
        if (is_non_fetchable_reference(raw)) return;
        Url target;
        if (!resolve_url(base, raw, target) || !target.is_http()) return;
        out.push_back(DiscoveredLink{target, role, hint});
    };

    for (const HtmlLink& link : scan.links) {
        switch (link.role) {
        case LinkRole::Base:
            break;
        case LinkRole::StyleBlock:
        case LinkRole::InlineStyle:
            for (const DiscoveredLink& nested : discover_css_links(link.value, base)) {
                out.push_back(nested);
            }
            break;
        case LinkRole::SrcSet:
            for (const SrcSetCandidate& candidate : parse_srcset(link.value)) {
                add(candidate.url, LinkRole::Asset, link.hint);
            }
            break;
        case LinkRole::MetaRefresh:
            add(link.value, LinkRole::Navigation, link.hint);
            break;
        case LinkRole::Navigation:
        case LinkRole::Asset:
            add(link.value, link.role, link.hint);
            break;
        }
    }
    return out;
}

std::vector<DiscoveredLink> discover_css_links(std::string_view css, const Url& base_url) {
    std::vector<DiscoveredLink> out;
    for (const CssLink& link : scan_css(css)) {
        if (is_non_fetchable_reference(link.value)) continue;
        Url target;
        if (!resolve_url(base_url, link.value, target) || !target.is_http()) continue;
        out.push_back(DiscoveredLink{
            target, LinkRole::Asset,
            link.is_import ? ResourceKind::Css : ResourceKind::Other});
    }
    return out;
}

} // namespace surl
