#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace surl {

/// A parsed absolute URL. SURL only ever mirrors http/https, but the parser
/// recognises other schemes so links such as mailto: and data: can be left
/// alone rather than mangled.
struct Url {
    std::string scheme;   ///< lower-cased, without ':'
    std::string userinfo; ///< kept for round-tripping, never written to disk
    std::string host;     ///< lower-cased, without brackets for IPv6
    std::uint16_t port = 0; ///< 0 means "scheme default"
    std::string path;     ///< always starts with '/' for http(s)
    std::string query;    ///< without '?'
    std::string fragment; ///< without '#'
    bool has_query = false;
    bool has_fragment = false;
    bool is_ipv6_literal = false;

    bool valid() const { return !scheme.empty(); }
    bool is_http() const { return scheme == "http" || scheme == "https"; }

    std::uint16_t effective_port() const;

    /// Full URL text including the fragment.
    std::string to_string() const;
    /// Full URL text with the fragment removed - the identity used for
    /// de-duplicating downloads.
    std::string to_string_no_fragment() const;
    /// "scheme://host[:port]"
    std::string origin() const;
    /// "host[:port]" as it would appear in a Host header.
    std::string authority() const;

    /// True when both URLs share scheme, host and effective port.
    bool same_origin_as(const Url& other) const;
    /// True when both hosts are equal or one is a subdomain of the other.
    bool same_site_as(const Url& other) const;
};

/// Parses an absolute URL. Returns false when there is no scheme or the input
/// is otherwise unusable.
bool parse_url(std::string_view text, Url& out);

/// Resolves a possibly relative reference against an absolute base, following
/// RFC 3986 section 5.2. Returns false when the result is not a usable URL.
bool resolve_url(const Url& base, std::string_view reference, Url& out);

/// RFC 3986 section 5.2.4 "remove_dot_segments".
std::string remove_dot_segments(std::string_view path);

/// Normalises a parsed URL in place: lower-cases scheme and host, drops a
/// default port, ensures a non-empty path, and collapses dot segments.
void normalize_url(Url& url);

/// True for references that address something other than a fetchable
/// resource: "#anchor", "javascript:...", "data:...", "mailto:...", "about:",
/// "tel:", "blob:", and the empty string.
bool is_non_fetchable_reference(std::string_view reference);

/// Best-effort check for hosts that resolve to the local machine or a private
/// network. Used to refuse SSRF-style targets unless the user opts in.
bool is_private_or_loopback_host(std::string_view host);

} // namespace surl
