#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace surl {

std::string to_lower(std::string_view s);
std::string to_upper(std::string_view s);
std::string trim(std::string_view s);

bool iequals(std::string_view a, std::string_view b);
bool istarts_with(std::string_view s, std::string_view prefix);
bool iends_with(std::string_view s, std::string_view suffix);

std::vector<std::string> split(std::string_view s, char sep, bool keep_empty = true);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
std::string replace_all(std::string_view s, std::string_view from, std::string_view to);

/// Percent-encodes everything outside the unreserved set, except the bytes
/// listed in extra_safe which are passed through untouched.
std::string percent_encode(std::string_view s, std::string_view extra_safe = "");
std::string percent_decode(std::string_view s);

std::string to_hex(const std::uint8_t* data, std::size_t len);

/// Parses sizes such as "500", "10k", "10MB", "1.5G" into bytes.
bool parse_size(std::string_view text, std::uint64_t& out);
/// Parses durations such as "500ms", "30", "30s", "2m", "1h" into milliseconds.
bool parse_duration_ms(std::string_view text, std::uint64_t& out);

std::string human_size(std::uint64_t bytes);
std::string human_duration_ms(std::uint64_t ms);

/// Glob matcher supporting '*', '?' and '**' (only the latter crosses '/').
bool glob_match(std::string_view pattern, std::string_view text);

} // namespace surl
