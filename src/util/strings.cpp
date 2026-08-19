#include "surl/util/strings.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace surl {
namespace {

inline char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}
inline char upper_ascii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}
inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
inline int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // namespace

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower_ascii(c);
    return out;
}

std::string to_upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = upper_ascii(c);
    return out;
}

std::string trim(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_space(s[b])) ++b;
    while (e > b && is_space(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_ascii(a[i]) != lower_ascii(b[i])) return false;
    }
    return true;
}

bool istarts_with(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    return iequals(s.substr(0, prefix.size()), prefix);
}

bool iends_with(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    return iequals(s.substr(s.size() - suffix.size()), suffix);
}

std::vector<std::string> split(std::string_view s, char sep, bool keep_empty) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        const std::string_view piece = (pos == std::string_view::npos)
                                           ? s.substr(start)
                                           : s.substr(start, pos - start);
        if (keep_empty || !piece.empty()) out.emplace_back(piece);
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out.append(sep);
        out.append(parts[i]);
    }
    return out;
}

std::string replace_all(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(s);
    std::string out;
    out.reserve(s.size());
    std::size_t pos = 0;
    while (true) {
        const std::size_t hit = s.find(from, pos);
        if (hit == std::string_view::npos) {
            out.append(s.substr(pos));
            break;
        }
        out.append(s.substr(pos, hit - pos));
        out.append(to);
        pos = hit + from.size();
    }
    return out;
}

std::string percent_encode(std::string_view s, std::string_view extra_safe) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                                c == '.' || c == '~';
        if (unreserved || extra_safe.find(static_cast<char>(c)) != std::string_view::npos) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string percent_decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hex_value(s[i + 1]);
            const int lo = hex_value(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string to_hex(const std::uint8_t* data, std::size_t len) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kHex[data[i] >> 4]);
        out.push_back(kHex[data[i] & 0x0F]);
    }
    return out;
}

bool parse_size(std::string_view text, std::uint64_t& out) {
    const std::string s = trim(text);
    if (s.empty()) return false;

    std::size_t idx = 0;
    while (idx < s.size() && (std::isdigit(static_cast<unsigned char>(s[idx])) || s[idx] == '.')) {
        ++idx;
    }
    if (idx == 0) return false;

    const double value = std::strtod(s.substr(0, idx).c_str(), nullptr);
    if (value < 0) return false;

    const std::string unit = to_lower(trim(std::string_view(s).substr(idx)));
    double multiplier;
    if (unit.empty() || unit == "b") multiplier = 1.0;
    else if (unit == "k" || unit == "kb" || unit == "kib") multiplier = 1024.0;
    else if (unit == "m" || unit == "mb" || unit == "mib") multiplier = 1048576.0;
    else if (unit == "g" || unit == "gb" || unit == "gib") multiplier = 1073741824.0;
    else if (unit == "t" || unit == "tb" || unit == "tib") multiplier = 1099511627776.0;
    else return false;

    out = static_cast<std::uint64_t>(value * multiplier);
    return true;
}

bool parse_duration_ms(std::string_view text, std::uint64_t& out) {
    const std::string s = trim(text);
    if (s.empty()) return false;

    std::size_t idx = 0;
    while (idx < s.size() && (std::isdigit(static_cast<unsigned char>(s[idx])) || s[idx] == '.')) {
        ++idx;
    }
    if (idx == 0) return false;

    const double value = std::strtod(s.substr(0, idx).c_str(), nullptr);
    if (value < 0) return false;

    const std::string unit = to_lower(trim(std::string_view(s).substr(idx)));
    double multiplier;
    if (unit == "ms") multiplier = 1.0;
    else if (unit.empty() || unit == "s" || unit == "sec") multiplier = 1000.0;
    else if (unit == "m" || unit == "min") multiplier = 60000.0;
    else if (unit == "h" || unit == "hr") multiplier = 3600000.0;
    else return false;

    out = static_cast<std::uint64_t>(value * multiplier);
    return true;
}

std::string human_size(std::uint64_t bytes) {
    static const char* const kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    char buf[64];
    if (unit == 0) {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, kUnits[unit]);
    }
    return buf;
}

std::string human_duration_ms(std::uint64_t ms) {
    char buf[64];
    if (ms < 1000) {
        std::snprintf(buf, sizeof(buf), "%llums", static_cast<unsigned long long>(ms));
    } else if (ms < 60000ULL) {
        std::snprintf(buf, sizeof(buf), "%.1fs", static_cast<double>(ms) / 1000.0);
    } else {
        const unsigned long long total_s = ms / 1000;
        std::snprintf(buf, sizeof(buf), "%llum %llus", total_s / 60, total_s % 60);
    }
    return buf;
}

namespace {

bool glob_match_impl(std::string_view pat, std::string_view txt) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star_p = std::string_view::npos;
    std::size_t star_t = 0;
    bool star_crosses_slash = false;

    while (t < txt.size()) {
        if (p < pat.size() && pat[p] == '*') {
            const bool doubled = (p + 1 < pat.size() && pat[p + 1] == '*');
            star_crosses_slash = doubled;
            p += doubled ? 2 : 1;
            // "**/" is also allowed to match zero directory components.
            if (doubled && p < pat.size() && pat[p] == '/') {
                if (glob_match_impl(pat.substr(p + 1), txt.substr(t))) return true;
            }
            star_p = p;
            star_t = t;
            continue;
        }
        if (p < pat.size() && (pat[p] == '?' || pat[p] == txt[t])) {
            ++p;
            ++t;
            continue;
        }
        if (star_p != std::string_view::npos) {
            if (!star_crosses_slash && txt[star_t] == '/') return false;
            ++star_t;
            t = star_t;
            p = star_p;
            continue;
        }
        return false;
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

} // namespace

bool glob_match(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) return false;
    return glob_match_impl(pattern, text);
}

} // namespace surl
