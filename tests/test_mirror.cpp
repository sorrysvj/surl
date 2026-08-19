#include "test_framework.hpp"

#include "surl/mirror/cache.hpp"
#include "surl/mirror/pathmap.hpp"
#include "surl/mirror/rewrite.hpp"
#include "surl/net/robots.hpp"
#include "surl/util/fsutil.hpp"
#include "surl/util/hash.hpp"
#include "surl/util/json.hpp"
#include "surl/util/strings.hpp"

#include <filesystem>
#include <set>

namespace fs = std::filesystem;
using namespace surl;

namespace {

Url must_parse(const char* text) {
    Url url;
    CHECK(parse_url(text, url));
    return url;
}

PathMapper make_mapper(const char* primary = "https://example.com/") {
    PathMapConfig config;
    config.root = fs::path("out");
    config.primary = must_parse(primary);
    return PathMapper(config);
}

std::string map(const char* url, ResourceKind kind = ResourceKind::Other) {
    return make_mapper().relative_path_for(must_parse(url), kind);
}

/// A resolver that mirrors exactly the URLs it was handed.
LinkResolver resolver_for(const std::set<std::string>& mirrored,
                          const PathMapper& mapper) {
    return [mirrored, mapper](const Url& target, LinkRole,
                              ResourceKind hint) -> std::optional<std::string> {
        const std::string key = target.to_string_no_fragment();
        if (mirrored.find(key) == mirrored.end()) return std::nullopt;
        return mapper.relative_path_for(target, hint);
    };
}

fs::path scratch_dir(const char* name) {
    const fs::path base = fs::temp_directory_path() / "surl-tests" / name;
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

} // namespace

// --- path mapping ----------------------------------------------------------

SURL_TEST(pathmap, maps_directory_urls_to_index_files) {
    CHECK_EQ(map("https://example.com/", ResourceKind::Html), std::string("index.html"));
    CHECK_EQ(map("https://example.com/docs/", ResourceKind::Html),
             std::string("docs/index.html"));
}

SURL_TEST(pathmap, keeps_ordinary_asset_paths_intact) {
    CHECK_EQ(map("https://example.com/css/site.css", ResourceKind::Css),
             std::string("css/site.css"));
    CHECK_EQ(map("https://example.com/img/logo.png", ResourceKind::Image),
             std::string("img/logo.png"));
}

SURL_TEST(pathmap, gives_extensionless_html_an_extension) {
    CHECK_EQ(map("https://example.com/about", ResourceKind::Html), std::string("about.html"));
    CHECK_EQ(map("https://example.com/page.php", ResourceKind::Html),
             std::string("page.php.html"));
    // Already HTML-shaped names are left alone.
    CHECK_EQ(map("https://example.com/x.html", ResourceKind::Html), std::string("x.html"));
}

SURL_TEST(pathmap, query_strings_produce_distinct_stable_files) {
    const std::string a = map("https://example.com/search?q=cats", ResourceKind::Html);
    const std::string b = map("https://example.com/search?q=dogs", ResourceKind::Html);
    const std::string a_again = map("https://example.com/search?q=cats", ResourceKind::Html);

    CHECK_NE(a, b);
    CHECK_EQ(a, a_again); // deterministic, which is what --resume relies on
    CHECK_CONTAINS(a, "search@");
    CHECK_CONTAINS(a, ".html");
}

SURL_TEST(pathmap, foreign_hosts_land_under_external) {
    const std::string path = map("https://cdn.other.test/lib.js", ResourceKind::JavaScript);
    CHECK_CONTAINS(path, "_external/cdn.other.test/");
    CHECK_CONTAINS(path, "lib.js");
}

SURL_TEST(pathmap, neutralises_traversal_and_reserved_names) {
    // The URL parser collapses literal dot segments, but percent-encoded ones
    // survive to the mapper, which is exactly the case that must not escape.
    const std::string path = map("https://example.com/%2e%2e/%2e%2e/etc/passwd");

    // The security property is that no component is a traversal token, not
    // that the string never contains dots.
    for (const std::string& component : split(path, '/', false)) {
        CHECK_NE(component, std::string(".."));
        CHECK_NE(component, std::string("."));
    }
    // And the mapped file really does stay under the output root.
    const PathMapper mapper = make_mapper();
    CHECK(is_inside(mapper.config().root,
                    mapper.absolute_path_for(must_parse("https://example.com/%2e%2e/%2e%2e/etc/passwd"),
                                             ResourceKind::Other)));

    const std::string device = map("https://example.com/CON.txt");
    CHECK_NOT_CONTAINS(device, "/CON.");
}

SURL_TEST(pathmap, renames_dot_components_without_leaving_traversal) {
    CHECK_EQ(sanitize_path_component("."), std::string("_dot"));
    CHECK_EQ(sanitize_path_component(".."), std::string("_dotdot"));
    // Windows strips trailing dots and spaces, so they must not survive either.
    CHECK_EQ(sanitize_path_component("name."), std::string("name_"));
    CHECK_EQ(sanitize_path_component("name  "), std::string("name_"));
}

SURL_TEST(pathmap, builds_relative_links_between_documents) {
    CHECK_EQ(PathMapper::relative_link("index.html", "about.html"), std::string("about.html"));
    CHECK_EQ(PathMapper::relative_link("index.html", "css/site.css"),
             std::string("css/site.css"));
    CHECK_EQ(PathMapper::relative_link("docs/guide.html", "index.html"),
             std::string("../index.html"));
    CHECK_EQ(PathMapper::relative_link("a/b/page.html", "a/c/other.html"),
             std::string("../c/other.html"));
    CHECK_EQ(PathMapper::relative_link("docs/a.html", "docs/b.html"), std::string("b.html"));
}

SURL_TEST(pathmap, percent_encodes_awkward_characters_in_links) {
    CHECK_EQ(PathMapper::relative_link("index.html", "my file.png"),
             std::string("my%20file.png"));
}

// --- rewriting -------------------------------------------------------------

SURL_TEST(rewrite, localises_mirrored_links_and_leaves_the_rest_absolute) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/index.html");

    const std::string html =
        "<html><head><link rel=\"stylesheet\" href=\"/css/site.css\"></head>"
        "<body><a href=\"/about\">About</a>"
        "<a href=\"/secret\">Secret</a>"
        "<img src=\"img/logo.png\"></body></html>";

    const std::set<std::string> mirrored = {
        "https://example.com/css/site.css",
        "https://example.com/about",
        "https://example.com/img/logo.png",
    };

    const RewriteResult result =
        rewrite_html(html, base, "index.html", resolver_for(mirrored, mapper));

    CHECK_CONTAINS(result.content, "href=\"css/site.css\"");
    CHECK_CONTAINS(result.content, "src=\"img/logo.png\"");
    // Not mirrored, and originally relative: it must become absolute so the
    // link still resolves when the mirror is opened from disk.
    CHECK_CONTAINS(result.content, "href=\"https://example.com/secret\"");
    CHECK_EQ(result.stats.absolutised, static_cast<std::size_t>(1));
}

SURL_TEST(rewrite, computes_links_relative_to_the_document) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/docs/guide.html");
    const std::string html = "<link rel=\"stylesheet\" href=\"/css/site.css\">";

    const RewriteResult result = rewrite_html(
        html, base, "docs/guide.html",
        resolver_for({"https://example.com/css/site.css"}, mapper));

    CHECK_CONTAINS(result.content, "href=\"../css/site.css\"");
}

SURL_TEST(rewrite, keeps_fragments_on_localised_links) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/index.html");
    const RewriteResult result =
        rewrite_html("<a href=\"/about#team\">t</a>", base, "index.html",
                     resolver_for({"https://example.com/about"}, mapper));
    CHECK_CONTAINS(result.content, "href=\"about.html#team\"");
}

SURL_TEST(rewrite, leaves_non_fetchable_references_alone) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/index.html");
    const std::string html =
        "<a href=\"#top\">top</a>"
        "<a href=\"mailto:a@example.com\">mail</a>"
        "<img src=\"data:image/gif;base64,R0lGOD\">"
        "<a href=\"javascript:void(0)\">js</a>";

    const RewriteResult result = rewrite_html(html, base, "index.html",
                                              resolver_for({}, mapper));
    CHECK_EQ(result.content, html);
    CHECK_EQ(result.stats.untouched, static_cast<std::size_t>(4));
}

SURL_TEST(rewrite, rewrites_srcset_candidates) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/index.html");
    const RewriteResult result =
        rewrite_html("<img srcset=\"/a.png 1x, /b.png 2x\">", base, "index.html",
                     resolver_for({"https://example.com/a.png",
                                   "https://example.com/b.png"},
                                  mapper));
    CHECK_CONTAINS(result.content, "a.png 1x");
    CHECK_CONTAINS(result.content, "b.png 2x");
    CHECK_NOT_CONTAINS(result.content, "/a.png 1x");
}

SURL_TEST(rewrite, neutralises_base_href) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/index.html");
    // A remote <base> would send every rewritten relative link back to the
    // live site, so it has to be replaced.
    const RewriteResult result =
        rewrite_html("<head><base href=\"https://example.com/deep/\"></head>"
                     "<img src=\"x.png\">",
                     base, "index.html",
                     resolver_for({"https://example.com/deep/x.png"}, mapper));

    CHECK_CONTAINS(result.content, "base href=\"./\"");
    CHECK_CONTAINS(result.content, "src=\"deep/x.png\"");
}

SURL_TEST(rewrite, rewrites_css_urls_and_imports) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/css/site.css");
    const std::string css = "@import \"reset.css\"; body{background:url(/img/bg.png)}";

    const RewriteResult result =
        rewrite_css(css, base, "css/site.css",
                    resolver_for({"https://example.com/css/reset.css",
                                  "https://example.com/img/bg.png"},
                                 mapper));

    CHECK_CONTAINS(result.content, "\"reset.css\"");
    CHECK_CONTAINS(result.content, "url(../img/bg.png)");
}

SURL_TEST(rewrite, rewrites_css_inside_html) {
    const PathMapper mapper = make_mapper();
    const Url base = must_parse("https://example.com/index.html");
    const RewriteResult result =
        rewrite_html("<style>a{background:url(/img/bg.png)}</style>", base, "index.html",
                     resolver_for({"https://example.com/img/bg.png"}, mapper));
    CHECK_CONTAINS(result.content, "url(img/bg.png)");
}

SURL_TEST(rewrite, discovers_links_for_the_crawler) {
    const Url base = must_parse("https://example.com/index.html");
    const std::vector<DiscoveredLink> links = discover_html_links(
        "<link rel=\"stylesheet\" href=\"/a.css\"><a href=\"/b.html\">b</a>"
        "<img src=\"/c.png\">",
        base);

    CHECK_EQ(links.size(), static_cast<std::size_t>(3));
    bool saw_navigation = false;
    for (const DiscoveredLink& link : links) {
        if (link.role == LinkRole::Navigation) saw_navigation = true;
    }
    CHECK(saw_navigation);
}

// --- robots ----------------------------------------------------------------

SURL_TEST(robots, honours_the_wildcard_group) {
    const RobotsTxt robots = RobotsTxt::parse(
        "User-agent: *\nDisallow: /private/\nAllow: /private/ok\n", "surl");
    CHECK(robots.is_allowed("/"));
    CHECK(robots.is_allowed("/public/page"));
    CHECK_FALSE(robots.is_allowed("/private/secret"));
    CHECK(robots.is_allowed("/private/ok"));
}

SURL_TEST(robots, a_named_group_beats_the_wildcard) {
    const RobotsTxt robots = RobotsTxt::parse(
        "User-agent: *\nDisallow: /\n\nUser-agent: surl\nDisallow: /nope\n", "surl");
    CHECK(robots.is_allowed("/anything"));
    CHECK_FALSE(robots.is_allowed("/nope"));
}

SURL_TEST(robots, supports_wildcards_and_end_anchors) {
    const RobotsTxt robots =
        RobotsTxt::parse("User-agent: *\nDisallow: /*.pdf$\nDisallow: /tmp/*/cache\n", "surl");
    CHECK_FALSE(robots.is_allowed("/docs/manual.pdf"));
    CHECK(robots.is_allowed("/docs/manual.pdf.html"));
    CHECK_FALSE(robots.is_allowed("/tmp/a/cache"));
}

SURL_TEST(robots, empty_disallow_allows_everything) {
    const RobotsTxt robots = RobotsTxt::parse("User-agent: *\nDisallow:\n", "surl");
    CHECK(robots.is_allowed("/anything"));
}

SURL_TEST(robots, reads_crawl_delay) {
    const RobotsTxt robots =
        RobotsTxt::parse("User-agent: *\nCrawl-delay: 2.5\nDisallow: /x\n", "surl");
    CHECK_EQ(robots.crawl_delay_ms(), 2500ULL);
}

// --- manifest --------------------------------------------------------------

SURL_TEST(manifest, round_trips_through_disk) {
    const fs::path dir = scratch_dir("manifest");
    const fs::path file = Manifest::default_path(dir);

    Manifest written;
    written.set_source("https://example.com/");

    CacheEntry entry;
    entry.url = "https://example.com/index.html";
    entry.path = "index.html";
    entry.status = 200;
    entry.size = 1234;
    entry.etag = "\"abc\"";
    entry.content_type = "text/html";
    entry.sha256 = sha256_hex("hello");
    entry.kind = ResourceKind::Html;
    entry.state = EntryState::Done;
    entry.depth = 0;
    written.put(entry);

    CacheEntry failure;
    failure.url = "https://example.com/missing";
    failure.state = EntryState::Failed;
    failure.status = 404;
    failure.note = "not found";
    written.put(failure);

    std::string error;
    CHECK(written.save(file, error));
    CHECK(fs::exists(file));

    Manifest read;
    CHECK(read.load(file, error));
    CHECK_EQ(read.size(), static_cast<std::size_t>(2));
    CHECK_EQ(read.source(), std::string("https://example.com/"));

    const std::optional<CacheEntry> loaded = read.find("https://example.com/index.html");
    CHECK(loaded.has_value());
    CHECK_EQ(loaded->path, std::string("index.html"));
    CHECK_EQ(loaded->etag, std::string("\"abc\""));
    CHECK_EQ(loaded->size, 1234ULL);
    CHECK(loaded->kind == ResourceKind::Html);
    CHECK(loaded->state == EntryState::Done);

    CHECK_EQ(read.count_state(EntryState::Failed), static_cast<std::size_t>(1));
}

SURL_TEST(manifest, a_missing_file_is_an_empty_manifest_not_an_error) {
    const fs::path dir = scratch_dir("manifest-missing");
    Manifest manifest;
    std::string error;
    CHECK(manifest.load(Manifest::default_path(dir), error));
    CHECK_EQ(manifest.size(), static_cast<std::size_t>(0));
    CHECK_FALSE(manifest.loaded_from_disk());
}

SURL_TEST(manifest, refuses_a_newer_format) {
    const fs::path dir = scratch_dir("manifest-future");
    const fs::path file = Manifest::default_path(dir);
    std::string error;
    CHECK(ensure_directory(file.parent_path(), error));
    CHECK(write_file_atomic(file, "{\"format\": 99, \"entries\": {}}", error));

    Manifest manifest;
    CHECK_FALSE(manifest.load(file, error));
    CHECK_CONTAINS(error, "newer SURL");
}

// --- filesystem helpers ----------------------------------------------------

SURL_TEST(fsutil, atomic_write_creates_parents_and_replaces) {
    const fs::path dir = scratch_dir("atomic");
    const fs::path file = dir / "a" / "b" / "c.txt";
    std::string error;

    CHECK(write_file_atomic(file, "first", error));
    std::string body;
    CHECK(read_file(file, body));
    CHECK_EQ(body, std::string("first"));

    CHECK(write_file_atomic(file, "second", error));
    CHECK(read_file(file, body));
    CHECK_EQ(body, std::string("second"));

    // No temporary files should be left behind.
    for (const auto& item : fs::directory_iterator(file.parent_path())) {
        CHECK_NOT_CONTAINS(path_to_utf8(item.path()), ".surl-tmp-");
    }
}

SURL_TEST(fsutil, is_inside_rejects_escapes) {
    const fs::path dir = scratch_dir("inside");
    CHECK(is_inside(dir, dir / "a" / "b.txt"));
    CHECK_FALSE(is_inside(dir, dir / ".." / "outside.txt"));
    CHECK_FALSE(is_inside(dir, fs::temp_directory_path() / "elsewhere.txt"));
}

SURL_TEST(fsutil, sanitises_path_components) {
    CHECK_EQ(sanitize_path_component(".."), std::string("_dotdot"));
    CHECK_EQ(sanitize_path_component("a<b>c"), std::string("a_b_c"));
    CHECK_EQ(sanitize_path_component("a/b"), std::string("a_b"));
    CHECK_CONTAINS(sanitize_path_component("nul"), "_");
    CHECK_FALSE(sanitize_path_component("normal.txt").empty());
    CHECK_EQ(sanitize_path_component("normal.txt"), std::string("normal.txt"));
}

// --- hashing and JSON ------------------------------------------------------

SURL_TEST(hash, sha256_matches_the_published_vectors) {
    CHECK_EQ(sha256_hex(""),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(sha256_hex("abc"),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

SURL_TEST(hash, sha256_handles_multi_block_input) {
    const std::string long_input(1000000, 'a');
    CHECK_EQ(sha256_hex(long_input),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

SURL_TEST(json, parses_and_serialises) {
    Json value;
    std::string error;
    CHECK(Json::parse(R"({"a":1,"b":[true,null,"x"],"c":{"d":2.5}})", value, error));

    CHECK(value.is_object());
    CHECK_EQ(value["a"].as_int(), static_cast<std::int64_t>(1));
    CHECK_EQ(value["b"].size(), static_cast<std::size_t>(3));
    CHECK(value["b"][0].as_bool());
    CHECK(value["b"][1].is_null());
    CHECK_EQ(value["b"][2].as_string(), std::string("x"));
    CHECK_EQ(value["c"]["d"].as_double(), 2.5);

    // Integral numbers must round-trip without a decimal point so byte counts
    // in the manifest stay readable.
    Json rebuilt = Json::object();
    rebuilt.set("n", Json(static_cast<std::uint64_t>(4096)));
    CHECK_EQ(rebuilt.dump(), std::string("{\"n\":4096}"));
}

SURL_TEST(json, decodes_escapes_and_surrogate_pairs) {
    Json value;
    std::string error;
    CHECK(Json::parse(R"("a\nbA😀")", value, error));
    CHECK_EQ(value.as_string(), std::string("a\nbA\xF0\x9F\x98\x80"));
}

SURL_TEST(json, reports_malformed_input) {
    Json value;
    std::string error;
    CHECK_FALSE(Json::parse("{unquoted: 1}", value, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(Json::parse("[1,2", value, error));
    CHECK_FALSE(Json::parse("", value, error));
}

SURL_TEST(json, tolerates_a_utf8_bom) {
    Json value;
    std::string error;
    CHECK(Json::parse("\xEF\xBB\xBF{\"a\":1}", value, error));
    CHECK_EQ(value["a"].as_int(), static_cast<std::int64_t>(1));
}
