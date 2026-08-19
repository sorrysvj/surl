#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace surl {

/// A parsed robots.txt, evaluated for one specific user-agent token.
///
/// SURL honours robots.txt by default because it fetches many URLs quickly and
/// a polite crawler is the difference between a useful tool and an abusive one.
/// `--no-robots` exists for mirroring sites you control.
class RobotsTxt {
public:
    /// Parses robots.txt content, keeping only the rules that apply to
    /// @p user_agent_token (matched case-insensitively) or to "*". A
    /// group naming the agent explicitly wins over the wildcard group.
    static RobotsTxt parse(std::string_view content, std::string_view user_agent_token);

    /// An empty policy that allows everything, used when robots.txt is missing
    /// or returns an error status.
    static RobotsTxt allow_all();

    /// Longest-match evaluation as described by the REP draft: the rule with
    /// the longest matching pattern wins, Allow wins a tie.
    bool is_allowed(std::string_view path) const;

    /// Crawl-delay in milliseconds, or 0 when the file did not specify one.
    std::uint64_t crawl_delay_ms() const { return crawl_delay_ms_; }

    const std::vector<std::string>& sitemaps() const { return sitemaps_; }

    bool empty() const { return rules_.empty(); }

private:
    struct Rule {
        std::string pattern;
        bool allow = false;
    };

    static bool pattern_matches(std::string_view pattern, std::string_view path);

    std::vector<Rule> rules_;
    std::vector<std::string> sitemaps_;
    std::uint64_t crawl_delay_ms_ = 0;
};

} // namespace surl
