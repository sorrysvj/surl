#include "surl/net/robots.hpp"

#include "surl/util/strings.hpp"

#include <cstdint>
#include <cstdlib>

namespace surl {
namespace {

/// robots.txt patterns support '*' (any sequence) and '$' (end anchor) only.
bool wildcard_match(std::string_view pattern, std::string_view text, bool anchored_end) {
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star_p = std::string_view::npos;
    std::size_t star_t = 0;

    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star_p = ++p;
            star_t = t;
            continue;
        }
        if (p < pattern.size() && pattern[p] == text[t]) {
            ++p;
            ++t;
            continue;
        }
        if (star_p != std::string_view::npos) {
            t = ++star_t;
            p = star_p;
            continue;
        }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;

    if (p != pattern.size()) return false;
    return anchored_end ? (t == text.size()) : true;
}

} // namespace

RobotsTxt RobotsTxt::allow_all() { return RobotsTxt{}; }

bool RobotsTxt::pattern_matches(std::string_view pattern, std::string_view path) {
    if (pattern.empty()) return false;

    bool anchored_end = false;
    if (pattern.back() == '$') {
        anchored_end = true;
        pattern.remove_suffix(1);
    }
    // Without '*' or '$' a robots pattern is a plain prefix match.
    if (!anchored_end && pattern.find('*') == std::string_view::npos) {
        return path.size() >= pattern.size() && path.compare(0, pattern.size(), pattern) == 0;
    }
    return wildcard_match(pattern, path, anchored_end);
}

RobotsTxt RobotsTxt::parse(std::string_view content, std::string_view user_agent_token) {
    RobotsTxt exact;   // rules from groups naming our agent
    RobotsTxt wildcard; // rules from the "*" group
    bool saw_exact = false;

    // Consecutive User-agent lines share one group of rules.
    bool group_matches_exact = false;
    bool group_matches_wildcard = false;
    bool collecting_agents = false;

    const std::string token = to_lower(user_agent_token);

    std::size_t start = 0;
    while (start <= content.size()) {
        const std::size_t newline = content.find('\n', start);
        std::string_view raw_line = (newline == std::string_view::npos)
                                        ? content.substr(start)
                                        : content.substr(start, newline - start);
        start = (newline == std::string_view::npos) ? content.size() + 1 : newline + 1;

        // Strip comments.
        const std::size_t hash = raw_line.find('#');
        if (hash != std::string_view::npos) raw_line = raw_line.substr(0, hash);

        const std::string line = trim(raw_line);
        if (line.empty()) continue;

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        const std::string field = to_lower(trim(std::string_view(line).substr(0, colon)));
        const std::string value = trim(std::string_view(line).substr(colon + 1));

        if (field == "user-agent") {
            if (!collecting_agents) {
                // A new group starts: reset which buckets receive its rules.
                group_matches_exact = false;
                group_matches_wildcard = false;
                collecting_agents = true;
            }
            const std::string agent = to_lower(value);
            if (agent == "*") {
                group_matches_wildcard = true;
            } else if (!token.empty() && (agent == token || token.find(agent) == 0)) {
                group_matches_exact = true;
                saw_exact = true;
            }
            continue;
        }

        collecting_agents = false;

        if (field == "sitemap") {
            // Sitemap lines are global, not tied to a group.
            if (!value.empty()) {
                exact.sitemaps_.push_back(value);
                wildcard.sitemaps_.push_back(value);
            }
            continue;
        }

        const bool is_allow = (field == "allow");
        const bool is_disallow = (field == "disallow");
        const bool is_delay = (field == "crawl-delay");

        if (!is_allow && !is_disallow && !is_delay) continue;

        const auto apply = [&](RobotsTxt& target) {
            if (is_delay) {
                const double seconds = std::strtod(value.c_str(), nullptr);
                if (seconds > 0) {
                    target.crawl_delay_ms_ = static_cast<std::uint64_t>(seconds * 1000.0);
                }
                return;
            }
            // "Disallow:" with an empty value means "allow everything".
            if (is_disallow && value.empty()) return;
            if (value.empty()) return;
            target.rules_.push_back(Rule{value, is_allow});
        };

        if (group_matches_exact) apply(exact);
        if (group_matches_wildcard) apply(wildcard);
    }

    return saw_exact ? exact : wildcard;
}

bool RobotsTxt::is_allowed(std::string_view path) const {
    if (rules_.empty()) return true;
    if (path.empty()) path = "/";

    std::size_t best_length = 0;
    bool best_allow = true;
    bool matched = false;

    for (const Rule& rule : rules_) {
        if (!pattern_matches(rule.pattern, path)) continue;
        // '$' does not count toward specificity; strip it for length purposes.
        const std::size_t length =
            rule.pattern.back() == '$' ? rule.pattern.size() - 1 : rule.pattern.size();
        if (!matched || length > best_length || (length == best_length && rule.allow)) {
            best_length = length;
            best_allow = rule.allow;
            matched = true;
        }
    }
    return matched ? best_allow : true;
}

} // namespace surl
