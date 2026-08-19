#include "test_framework.hpp"

#include "surl/parse/css.hpp"
#include "surl/parse/html.hpp"
#include "surl/util/strings.hpp"

#include <algorithm>

using namespace surl;

namespace {

std::vector<std::string> link_values(const HtmlScan& scan, LinkRole role) {
    std::vector<std::string> out;
    for (const HtmlLink& link : scan.links) {
        if (link.role == role) out.push_back(link.value);
    }
    return out;
}

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

} // namespace

// --- strings ---------------------------------------------------------------

SURL_TEST(strings, parses_sizes) {
    std::uint64_t value = 0;
    CHECK(parse_size("500", value));
    CHECK_EQ(value, 500ULL);
    CHECK(parse_size("10k", value));
    CHECK_EQ(value, 10240ULL);
    CHECK(parse_size("2MB", value));
    CHECK_EQ(value, 2ULL * 1024 * 1024);
    CHECK(parse_size("1.5G", value));
    CHECK_EQ(value, static_cast<std::uint64_t>(1.5 * 1024 * 1024 * 1024));
    CHECK_FALSE(parse_size("banana", value));
    CHECK_FALSE(parse_size("10 furlongs", value));
}

SURL_TEST(strings, parses_durations) {
    std::uint64_t value = 0;
    CHECK(parse_duration_ms("500ms", value));
    CHECK_EQ(value, 500ULL);
    CHECK(parse_duration_ms("30", value)); // bare numbers are seconds
    CHECK_EQ(value, 30000ULL);
    CHECK(parse_duration_ms("2m", value));
    CHECK_EQ(value, 120000ULL);
    CHECK(parse_duration_ms("1h", value));
    CHECK_EQ(value, 3600000ULL);
    CHECK_FALSE(parse_duration_ms("soon", value));
}

SURL_TEST(strings, percent_codec_round_trips) {
    const std::string original = "a b/c?d=e&f";
    const std::string encoded = percent_encode(original);
    CHECK_NOT_CONTAINS(encoded, " ");
    CHECK_EQ(percent_decode(encoded), original);
    CHECK_EQ(percent_decode("%E2%9C%93"), std::string("\xE2\x9C\x93"));
}

SURL_TEST(strings, glob_matching) {
    CHECK(glob_match("*.png", "logo.png"));
    CHECK_FALSE(glob_match("*.png", "logo.jpg"));
    CHECK(glob_match("https://example.com/*", "https://example.com/page"));
    // A single star must not cross a path separator.
    CHECK_FALSE(glob_match("https://example.com/*", "https://example.com/a/b"));
    CHECK(glob_match("https://example.com/**", "https://example.com/a/b"));
    CHECK(glob_match("**/admin/**", "https://example.com/x/admin/y"));
    CHECK(glob_match("?at", "cat"));
}

// --- HTML ------------------------------------------------------------------

SURL_TEST(html, finds_assets_and_navigation) {
    const std::string doc = R"(<!doctype html>
<html><head>
<link rel="stylesheet" href="/css/site.css">
<link rel="icon" href="/favicon.ico">
<link rel="manifest" href="/app.webmanifest">
<script src="/js/app.js"></script>
</head><body>
<a href="/about">About</a>
<img src="/img/logo.png" alt="logo">
<iframe src="/embed.html"></iframe>
</body></html>)";

    const HtmlScan scan = scan_html(doc);
    const std::vector<std::string> assets = link_values(scan, LinkRole::Asset);
    const std::vector<std::string> nav = link_values(scan, LinkRole::Navigation);

    CHECK(contains(assets, "/css/site.css"));
    CHECK(contains(assets, "/favicon.ico"));
    CHECK(contains(assets, "/app.webmanifest"));
    CHECK(contains(assets, "/js/app.js"));
    CHECK(contains(assets, "/img/logo.png"));
    CHECK(contains(nav, "/about"));
    CHECK(contains(nav, "/embed.html"));
}

SURL_TEST(html, records_byte_offsets_that_point_at_the_value) {
    const std::string doc = "<img src=\"/a/b.png\">";
    const HtmlScan scan = scan_html(doc);
    CHECK_EQ(scan.links.size(), static_cast<std::size_t>(1));
    const HtmlLink& link = scan.links.front();
    CHECK_EQ(doc.substr(link.offset, link.length), std::string("/a/b.png"));
}

SURL_TEST(html, decodes_entities_in_values) {
    const HtmlScan scan = scan_html("<a href=\"/s?a=1&amp;b=2\">x</a>");
    CHECK_EQ(scan.links.size(), static_cast<std::size_t>(1));
    CHECK_EQ(scan.links.front().value, std::string("/s?a=1&b=2"));
}

SURL_TEST(html, handles_unquoted_and_single_quoted_attributes) {
    const HtmlScan scan = scan_html("<img src=/a.png><img src='/b.png'>");
    CHECK_EQ(scan.links.size(), static_cast<std::size_t>(2));
    CHECK_EQ(scan.links[0].value, std::string("/a.png"));
    CHECK_EQ(scan.links[1].value, std::string("/b.png"));
}

SURL_TEST(html, ignores_markup_inside_script_bodies) {
    const HtmlScan scan =
        scan_html("<script>var s = '<img src=\"/should-not-match.png\">';</script>"
                  "<img src=\"/real.png\">");
    const std::vector<std::string> assets = link_values(scan, LinkRole::Asset);
    CHECK(contains(assets, "/real.png"));
    CHECK_FALSE(contains(assets, "/should-not-match.png"));
}

SURL_TEST(html, uppercase_raw_text_elements_do_not_swallow_the_document) {
    // Legacy markup is full of uppercase tags. A case-sensitive search for the
    // closing tag would treat everything after <TITLE> as text and lose every
    // link in the page, which is exactly what info.cern.ch looks like.
    const HtmlScan scan = scan_html(
        "<HEADER><TITLE>The project</TITLE></HEADER><BODY>"
        "<A\nNAME=0 HREF=\"WhatIs.html\">hypermedia</A>"
        "<A NAME=24 HREF=\"Summary.html\">summary</A></BODY>");

    const std::vector<std::string> nav = link_values(scan, LinkRole::Navigation);
    CHECK_EQ(nav.size(), static_cast<std::size_t>(2));
    CHECK(contains(nav, "WhatIs.html"));
    CHECK(contains(nav, "Summary.html"));
}

SURL_TEST(html, uppercase_script_and_style_are_still_treated_as_raw_text) {
    const HtmlScan scan =
        scan_html("<SCRIPT>var s='<img src=\"/nope.png\">';</SCRIPT><IMG SRC=\"/yes.png\">");
    const std::vector<std::string> assets = link_values(scan, LinkRole::Asset);
    CHECK_EQ(assets.size(), static_cast<std::size_t>(1));
    CHECK_EQ(assets.front(), std::string("/yes.png"));
}

SURL_TEST(html, an_unterminated_raw_text_element_does_not_lose_the_rest) {
    // A truncated or malformed page should still yield the links it contains.
    const HtmlScan scan = scan_html("<title>no closing tag<img src=\"/still-found.png\">");
    const std::vector<std::string> assets = link_values(scan, LinkRole::Asset);
    CHECK(contains(assets, "/still-found.png"));
}

SURL_TEST(html, skips_comments) {
    const HtmlScan scan = scan_html("<!-- <img src=\"/hidden.png\"> --><img src=\"/shown.png\">");
    const std::vector<std::string> assets = link_values(scan, LinkRole::Asset);
    CHECK_EQ(assets.size(), static_cast<std::size_t>(1));
    CHECK_EQ(assets.front(), std::string("/shown.png"));
}

SURL_TEST(html, captures_base_href) {
    const HtmlScan scan = scan_html("<head><base href=\"https://cdn.test/root/\"></head>");
    CHECK(scan.has_base);
    CHECK_EQ(scan.base_href, std::string("https://cdn.test/root/"));
}

SURL_TEST(html, parses_srcset) {
    const std::vector<SrcSetCandidate> candidates =
        parse_srcset("/a.png 1x, /b.png 2x , /c.png 640w");
    CHECK_EQ(candidates.size(), static_cast<std::size_t>(3));
    CHECK_EQ(candidates[0].url, std::string("/a.png"));
    CHECK_EQ(candidates[0].descriptor, std::string("1x"));
    CHECK_EQ(candidates[2].url, std::string("/c.png"));
    CHECK_EQ(candidates[2].descriptor, std::string("640w"));
    CHECK_EQ(build_srcset(candidates), std::string("/a.png 1x, /b.png 2x, /c.png 640w"));
}

SURL_TEST(html, parses_meta_refresh) {
    std::string url;
    std::size_t offset = 0;
    std::size_t length = 0;
    CHECK(parse_meta_refresh("5; url=/next.html", url, offset, length));
    CHECK_EQ(url, std::string("/next.html"));
    CHECK(parse_meta_refresh("0;URL='/quoted.html'", url, offset, length));
    CHECK_EQ(url, std::string("/quoted.html"));
    CHECK_FALSE(parse_meta_refresh("5", url, offset, length));
}

SURL_TEST(html, finds_style_attributes_and_blocks) {
    const HtmlScan scan =
        scan_html("<style>body{background:url(/bg.png)}</style>"
                  "<div style=\"background:url('/inline.png')\"></div>");
    bool saw_block = false;
    bool saw_inline = false;
    for (const HtmlLink& link : scan.links) {
        if (link.role == LinkRole::StyleBlock) saw_block = true;
        if (link.role == LinkRole::InlineStyle) saw_inline = true;
    }
    CHECK(saw_block);
    CHECK(saw_inline);
}

SURL_TEST(html, escapes_attribute_values) {
    CHECK_EQ(encode_html_attribute("a&b\"c'd<e"),
             std::string("a&amp;b&quot;c&#39;d&lt;e"));
}

// --- CSS -------------------------------------------------------------------

SURL_TEST(css, finds_urls_in_every_form) {
    const std::string sheet = R"(
@import "base.css";
@import url("theme.css");
body { background: url(/img/bg.png) no-repeat; }
.a { background-image: url('/img/a.png'); }
.b { background-image: url( "/img/b.png" ); }
)";
    const std::vector<CssLink> links = scan_css(sheet);

    std::vector<std::string> values;
    for (const CssLink& link : links) values.push_back(link.value);

    CHECK(contains(values, "base.css"));
    CHECK(contains(values, "theme.css"));
    CHECK(contains(values, "/img/bg.png"));
    CHECK(contains(values, "/img/a.png"));
    CHECK(contains(values, "/img/b.png"));
}

SURL_TEST(css, offsets_point_at_the_url_text) {
    const std::string sheet = "a{background:url(/x/y.png)}";
    const std::vector<CssLink> links = scan_css(sheet);
    CHECK_EQ(links.size(), static_cast<std::size_t>(1));
    CHECK_EQ(sheet.substr(links[0].offset, links[0].length), std::string("/x/y.png"));
}

SURL_TEST(css, ignores_urls_in_comments) {
    const std::vector<CssLink> links = scan_css("/* url(/commented.png) */ a{color:red}");
    CHECK_EQ(links.size(), static_cast<std::size_t>(0));
}

SURL_TEST(css, does_not_match_identifiers_ending_in_url) {
    const std::vector<CssLink> links = scan_css(".my-url(x) { }");
    CHECK_EQ(links.size(), static_cast<std::size_t>(0));
}
