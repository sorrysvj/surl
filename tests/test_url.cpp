#include "test_framework.hpp"

#include "surl/net/url.hpp"

using namespace surl;

namespace {

Url parse(const char* text) {
    Url url;
    CHECK(parse_url(text, url));
    return url;
}

std::string resolve(const char* base_text, const char* reference) {
    Url base;
    CHECK(parse_url(base_text, base));
    Url out;
    CHECK(resolve_url(base, reference, out));
    return out.to_string();
}

} // namespace

SURL_TEST(url, parses_the_common_shape) {
    const Url url = parse("https://Example.COM/a/b?x=1#top");
    CHECK_EQ(url.scheme, std::string("https"));
    CHECK_EQ(url.host, std::string("example.com"));
    CHECK_EQ(url.path, std::string("/a/b"));
    CHECK_EQ(url.query, std::string("x=1"));
    CHECK_EQ(url.fragment, std::string("top"));
    CHECK(url.has_query);
    CHECK(url.has_fragment);
    CHECK_EQ(url.effective_port(), static_cast<std::uint16_t>(443));
}

SURL_TEST(url, drops_the_default_port_but_keeps_others) {
    CHECK_EQ(parse("https://example.com:443/").to_string(), std::string("https://example.com/"));
    CHECK_EQ(parse("http://example.com:80/").to_string(), std::string("http://example.com/"));
    CHECK_EQ(parse("https://example.com:8443/").to_string(),
             std::string("https://example.com:8443/"));
}

SURL_TEST(url, supplies_a_root_path) {
    CHECK_EQ(parse("https://example.com").path, std::string("/"));
}

SURL_TEST(url, handles_ipv6_literals) {
    const Url url = parse("http://[2001:db8::1]:8080/x");
    CHECK(url.is_ipv6_literal);
    CHECK_EQ(url.host, std::string("2001:db8::1"));
    CHECK_EQ(url.port, static_cast<std::uint16_t>(8080));
    CHECK_EQ(url.to_string(), std::string("http://[2001:db8::1]:8080/x"));
}

SURL_TEST(url, rejects_input_without_a_scheme) {
    Url url;
    CHECK_FALSE(parse_url("example.com/path", url));
    CHECK_FALSE(parse_url("/just/a/path", url));
    CHECK_FALSE(parse_url("", url));
}

SURL_TEST(url, removes_dot_segments) {
    CHECK_EQ(remove_dot_segments("/a/b/../c"), std::string("/a/c"));
    CHECK_EQ(remove_dot_segments("/a/./b"), std::string("/a/b"));
    CHECK_EQ(remove_dot_segments("/../../x"), std::string("/x"));
    CHECK_EQ(remove_dot_segments("/a/b/"), std::string("/a/b/"));
    CHECK_EQ(remove_dot_segments("/"), std::string("/"));
}

SURL_TEST(url, resolves_relative_references) {
    CHECK_EQ(resolve("https://example.com/dir/page.html", "img.png"),
             std::string("https://example.com/dir/img.png"));
    CHECK_EQ(resolve("https://example.com/dir/page.html", "/img.png"),
             std::string("https://example.com/img.png"));
    CHECK_EQ(resolve("https://example.com/dir/page.html", "../up.png"),
             std::string("https://example.com/up.png"));
    CHECK_EQ(resolve("https://example.com/dir/page.html", "./same.png"),
             std::string("https://example.com/dir/same.png"));
}

SURL_TEST(url, resolves_protocol_relative_references) {
    CHECK_EQ(resolve("https://example.com/a", "//cdn.example.org/x.js"),
             std::string("https://cdn.example.org/x.js"));
    CHECK_EQ(resolve("http://example.com/a", "//cdn.example.org/x.js"),
             std::string("http://cdn.example.org/x.js"));
}

SURL_TEST(url, resolves_query_and_fragment_only_references) {
    CHECK_EQ(resolve("https://example.com/a/b?old=1", "?new=2"),
             std::string("https://example.com/a/b?new=2"));
    CHECK_EQ(resolve("https://example.com/a/b?keep=1", "#section"),
             std::string("https://example.com/a/b?keep=1#section"));
}

SURL_TEST(url, absolute_references_ignore_the_base) {
    CHECK_EQ(resolve("https://example.com/a", "https://other.test/b"),
             std::string("https://other.test/b"));
}

SURL_TEST(url, strips_whitespace_browsers_ignore) {
    Url url;
    CHECK(parse_url("  https://example.com/a\n/b  ", url));
    CHECK_EQ(url.to_string(), std::string("https://example.com/a/b"));
}

SURL_TEST(url, compares_origins_and_sites) {
    const Url a = parse("https://example.com/x");
    const Url b = parse("https://example.com:443/y");
    const Url c = parse("http://example.com/y");
    const Url d = parse("https://cdn.example.com/y");

    CHECK(a.same_origin_as(b));
    CHECK_FALSE(a.same_origin_as(c)); // scheme differs
    CHECK_FALSE(a.same_origin_as(d));
    CHECK(a.same_site_as(d));
}

SURL_TEST(url, recognises_non_fetchable_references) {
    CHECK(is_non_fetchable_reference("#anchor"));
    CHECK(is_non_fetchable_reference(""));
    CHECK(is_non_fetchable_reference("javascript:void(0)"));
    CHECK(is_non_fetchable_reference("data:image/png;base64,AAAA"));
    CHECK(is_non_fetchable_reference("mailto:someone@example.com"));
    CHECK(is_non_fetchable_reference("tel:+1234"));
    CHECK_FALSE(is_non_fetchable_reference("/page.html"));
    CHECK_FALSE(is_non_fetchable_reference("https://example.com/"));
}

SURL_TEST(url, flags_private_and_loopback_hosts) {
    CHECK(is_private_or_loopback_host("localhost"));
    CHECK(is_private_or_loopback_host("127.0.0.1"));
    CHECK(is_private_or_loopback_host("10.0.0.5"));
    CHECK(is_private_or_loopback_host("192.168.1.1"));
    CHECK(is_private_or_loopback_host("172.16.0.1"));
    CHECK(is_private_or_loopback_host("169.254.1.1"));
    CHECK(is_private_or_loopback_host("::1"));
    CHECK(is_private_or_loopback_host("fd00::1"));
    CHECK(is_private_or_loopback_host("intranet"));
    CHECK(is_private_or_loopback_host("printer.local"));

    CHECK_FALSE(is_private_or_loopback_host("example.com"));
    CHECK_FALSE(is_private_or_loopback_host("8.8.8.8"));
    CHECK_FALSE(is_private_or_loopback_host("172.32.0.1"));
}
