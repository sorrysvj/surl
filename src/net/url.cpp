#include "surl/net/url.hpp"

#include "surl/util/strings.hpp"

#include <cstdlib>
#include <vector>

namespace surl {
namespace {

bool is_scheme_char(char c, bool first) {
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (first) return false;
    return (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

/// Finds the ':' that ends a scheme, or npos when the text has no scheme.
std::size_t find_scheme_end(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == ':') return i == 0 ? std::string_view::npos : i;
        if (!is_scheme_char(c, i == 0)) return std::string_view::npos;
    }
    return std::string_view::npos;
}

std::uint16_t default_port_for(std::string_view scheme) {
    if (scheme == "http") return 80;
    if (scheme == "https") return 443;
    if (scheme == "ftp") return 21;
    if (scheme == "ws") return 80;
    if (scheme == "wss") return 443;
    return 0;
}

/// Splits the part after "//" into userinfo, host, port and the remainder.
bool split_authority(std::string_view authority, Url& out) {
    const std::size_t at = authority.rfind('@');
    std::string_view host_port = authority;
    if (at != std::string_view::npos) {
        out.userinfo = std::string(authority.substr(0, at));
        host_port = authority.substr(at + 1);
    }

    if (!host_port.empty() && host_port.front() == '[') {
        const std::size_t close = host_port.find(']');
        if (close == std::string_view::npos) return false;
        out.host = to_lower(host_port.substr(1, close - 1));
        out.is_ipv6_literal = true;
        host_port.remove_prefix(close + 1);
        if (!host_port.empty()) {
            if (host_port.front() != ':') return false;
            host_port.remove_prefix(1);
            if (!host_port.empty()) {
                out.port = static_cast<std::uint16_t>(
                    std::strtoul(std::string(host_port).c_str(), nullptr, 10));
            }
        }
        return !out.host.empty();
    }

    const std::size_t colon = host_port.rfind(':');
    if (colon != std::string_view::npos) {
        const std::string_view port_text = host_port.substr(colon + 1);
        bool numeric = !port_text.empty();
        for (const char c : port_text) {
            if (c < '0' || c > '9') {
                numeric = false;
                break;
            }
        }
        if (numeric) {
            const unsigned long value = std::strtoul(std::string(port_text).c_str(), nullptr, 10);
            if (value > 65535) return false;
            out.port = static_cast<std::uint16_t>(value);
            host_port = host_port.substr(0, colon);
        } else if (port_text.empty()) {
            host_port = host_port.substr(0, colon);
        }
    }

    out.host = to_lower(host_port);
    return true;
}

/// Strips tabs and newlines, which browsers ignore inside URLs and which show
/// up regularly in minified or hand-wrapped HTML attributes.
std::string strip_url_whitespace(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\t' || c == '\n' || c == '\r') continue;
        out.push_back(c);
    }
    return trim(out);
}

} // namespace

std::string remove_dot_segments(std::string_view path) {
    std::vector<std::string_view> stack;
    const bool absolute = !path.empty() && path.front() == '/';
    const bool trailing_slash_hint =
        !path.empty() && (path.back() == '/' ||
                          iends_with(path, "/.") || iends_with(path, "/..") ||
                          path == "." || path == "..");

    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view segment = (slash == std::string_view::npos)
                                             ? path.substr(start)
                                             : path.substr(start, slash - start);
        if (segment == "..") {
            if (!stack.empty()) stack.pop_back();
        } else if (segment != "." && !segment.empty()) {
            stack.push_back(segment);
        }
        if (slash == std::string_view::npos) break;
        start = slash + 1;
    }

    std::string out;
    if (absolute) out.push_back('/');
    for (std::size_t i = 0; i < stack.size(); ++i) {
        if (i) out.push_back('/');
        out.append(stack[i]);
    }
    if (trailing_slash_hint && (out.empty() || out.back() != '/')) out.push_back('/');
    if (out.empty()) out = absolute ? "/" : "";
    return out;
}

std::uint16_t Url::effective_port() const {
    return port != 0 ? port : default_port_for(scheme);
}

std::string Url::authority() const {
    std::string out;
    if (is_ipv6_literal) {
        out.push_back('[');
        out.append(host);
        out.push_back(']');
    } else {
        out.append(host);
    }
    const std::uint16_t default_port = default_port_for(scheme);
    if (port != 0 && port != default_port) {
        out.push_back(':');
        out.append(std::to_string(port));
    }
    return out;
}

std::string Url::origin() const {
    if (host.empty()) return "";
    return scheme + "://" + authority();
}

std::string Url::to_string_no_fragment() const {
    std::string out;
    out.append(scheme);
    out.push_back(':');
    if (!host.empty()) {
        out.append("//");
        if (!userinfo.empty()) {
            out.append(userinfo);
            out.push_back('@');
        }
        out.append(authority());
    }
    out.append(path);
    if (has_query) {
        out.push_back('?');
        out.append(query);
    }
    return out;
}

std::string Url::to_string() const {
    std::string out = to_string_no_fragment();
    if (has_fragment) {
        out.push_back('#');
        out.append(fragment);
    }
    return out;
}

bool Url::same_origin_as(const Url& other) const {
    return scheme == other.scheme && host == other.host &&
           effective_port() == other.effective_port();
}

bool Url::same_site_as(const Url& other) const {
    if (host == other.host) return true;
    if (host.empty() || other.host.empty()) return false;
    if (host.size() > other.host.size()) {
        return iends_with(host, "." + other.host);
    }
    return iends_with(other.host, "." + host);
}

void normalize_url(Url& url) {
    url.scheme = to_lower(url.scheme);
    url.host = to_lower(url.host);

    if (url.port != 0 && url.port == default_port_for(url.scheme)) url.port = 0;

    if (url.is_http()) {
        if (url.path.empty()) {
            url.path = "/";
        } else if (url.path.front() != '/') {
            url.path.insert(url.path.begin(), '/');
        }
        url.path = remove_dot_segments(url.path);
        if (url.path.empty()) url.path = "/";
    }

    // A trailing '?' with nothing after it is not a distinct resource.
    if (url.has_query && url.query.empty()) url.has_query = false;
}

bool parse_url(std::string_view raw, Url& out) {
    const std::string text = strip_url_whitespace(raw);
    if (text.empty()) return false;

    const std::size_t scheme_end = find_scheme_end(text);
    if (scheme_end == std::string_view::npos) return false;

    out = Url{};
    out.scheme = to_lower(std::string_view(text).substr(0, scheme_end));

    std::string_view rest = std::string_view(text).substr(scheme_end + 1);

    // Split off the fragment first: it may legally contain '?' and '/'.
    const std::size_t hash = rest.find('#');
    if (hash != std::string_view::npos) {
        out.fragment = std::string(rest.substr(hash + 1));
        out.has_fragment = true;
        rest = rest.substr(0, hash);
    }

    const std::size_t question = rest.find('?');
    if (question != std::string_view::npos) {
        out.query = std::string(rest.substr(question + 1));
        out.has_query = true;
        rest = rest.substr(0, question);
    }

    if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/') {
        rest.remove_prefix(2);
        const std::size_t path_start = rest.find('/');
        const std::string_view authority =
            (path_start == std::string_view::npos) ? rest : rest.substr(0, path_start);
        if (!split_authority(authority, out)) return false;
        out.path = (path_start == std::string_view::npos)
                       ? std::string()
                       : std::string(rest.substr(path_start));
    } else {
        // Opaque scheme such as mailto: or data:.
        out.path = std::string(rest);
    }

    if (out.is_http() && out.host.empty()) return false;

    normalize_url(out);
    return true;
}

bool resolve_url(const Url& base, std::string_view raw_reference, Url& out) {
    const std::string reference = strip_url_whitespace(raw_reference);

    // An absolute reference stands on its own.
    if (find_scheme_end(reference) != std::string_view::npos) {
        return parse_url(reference, out);
    }

    if (!base.valid()) return false;

    out = Url{};
    out.scheme = base.scheme;

    std::string_view rest = reference;

    const std::size_t hash = rest.find('#');
    if (hash != std::string_view::npos) {
        out.fragment = std::string(rest.substr(hash + 1));
        out.has_fragment = true;
        rest = rest.substr(0, hash);
    }

    std::string_view query_part;
    bool has_query = false;
    const std::size_t question = rest.find('?');
    if (question != std::string_view::npos) {
        query_part = rest.substr(question + 1);
        has_query = true;
        rest = rest.substr(0, question);
    }

    if (reference.size() >= 2 && reference[0] == '/' && reference[1] == '/') {
        // Protocol-relative reference: keep the base scheme, take the authority.
        std::string_view authority_and_path = rest.substr(2);
        const std::size_t path_start = authority_and_path.find('/');
        const std::string_view authority = (path_start == std::string_view::npos)
                                               ? authority_and_path
                                               : authority_and_path.substr(0, path_start);
        if (!split_authority(authority, out)) return false;
        out.path = (path_start == std::string_view::npos)
                       ? "/"
                       : std::string(authority_and_path.substr(path_start));
    } else {
        out.userinfo = base.userinfo;
        out.host = base.host;
        out.port = base.port;
        out.is_ipv6_literal = base.is_ipv6_literal;

        if (rest.empty()) {
            out.path = base.path;
            if (!has_query) {
                // "#frag" or "" keeps the base query as well.
                out.query = base.query;
                has_query = base.has_query;
                query_part = out.query;
            }
        } else if (rest.front() == '/') {
            out.path = std::string(rest);
        } else {
            // Merge with the base path: everything up to and including the
            // last '/' of the base.
            const std::size_t last_slash = base.path.rfind('/');
            std::string prefix =
                (last_slash == std::string::npos) ? "/" : base.path.substr(0, last_slash + 1);
            out.path = prefix + std::string(rest);
        }
    }

    if (has_query) {
        out.query = std::string(query_part);
        out.has_query = true;
    }

    if (out.is_http() && out.host.empty()) return false;

    normalize_url(out);
    return true;
}

bool is_non_fetchable_reference(std::string_view raw) {
    const std::string reference = strip_url_whitespace(raw);
    if (reference.empty()) return true;
    if (reference.front() == '#') return true;

    static const char* const kSchemes[] = {
        "javascript:", "data:", "mailto:", "tel:", "sms:", "about:",
        "blob:", "chrome:", "file:", "ftp:", "ws:", "wss:", "magnet:",
        "intent:", "market:", "geo:", "callto:", "skype:", "viber:", "whatsapp:"};
    for (const char* scheme : kSchemes) {
        if (istarts_with(reference, scheme)) return true;
    }
    return false;
}

bool is_private_or_loopback_host(std::string_view raw_host) {
    const std::string host = to_lower(trim(raw_host));
    if (host.empty()) return true;

    if (host == "localhost" || iends_with(host, ".localhost") ||
        iends_with(host, ".local") || iends_with(host, ".internal") ||
        iends_with(host, ".home.arpa")) {
        return true;
    }

    // IPv6 loopback / unique-local / link-local.
    if (host.find(':') != std::string::npos) {
        if (host == "::1" || host == "::") return true;
        if (istarts_with(host, "fc") || istarts_with(host, "fd")) return true; // fc00::/7
        if (istarts_with(host, "fe8") || istarts_with(host, "fe9") ||
            istarts_with(host, "fea") || istarts_with(host, "feb")) {
            return true; // fe80::/10
        }
        // IPv4-mapped addresses such as ::ffff:127.0.0.1.
        const std::size_t last_colon = host.rfind(':');
        if (last_colon != std::string::npos && host.find('.') != std::string::npos) {
            return is_private_or_loopback_host(host.substr(last_colon + 1));
        }
        return false;
    }

    // Dotted-quad IPv4 literals.
    const std::vector<std::string> parts = split(host, '.');
    if (parts.size() == 4) {
        unsigned octets[4] = {0, 0, 0, 0};
        for (std::size_t i = 0; i < 4; ++i) {
            if (parts[i].empty() || parts[i].size() > 3) return false;
            for (const char c : parts[i]) {
                if (c < '0' || c > '9') return false;
            }
            const unsigned long value = std::strtoul(parts[i].c_str(), nullptr, 10);
            if (value > 255) return false;
            octets[i] = static_cast<unsigned>(value);
        }
        if (octets[0] == 127) return true;                       // loopback
        if (octets[0] == 10) return true;                        // private
        if (octets[0] == 0) return true;                         // "this network"
        if (octets[0] == 169 && octets[1] == 254) return true;   // link-local
        if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) return true;
        if (octets[0] == 192 && octets[1] == 168) return true;
        if (octets[0] == 100 && octets[1] >= 64 && octets[1] <= 127) return true; // CGNAT
        if (octets[0] >= 224) return true;                       // multicast / reserved
        return false;
    }

    // A bare hostname with no dot is almost always an intranet name.
    if (host.find('.') == std::string::npos) return true;

    return false;
}

} // namespace surl
