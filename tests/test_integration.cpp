#include "fixture_server.hpp"
#include "test_framework.hpp"

#include "surl/cli/options.hpp"
#include "surl/mirror/cache.hpp"
#include "surl/mirror/crawler.hpp"
#include "surl/mirror/pathmap.hpp"
#include "surl/net/http_client.hpp"
#include "surl/util/fsutil.hpp"
#include "surl/util/hash.hpp"

#include <filesystem>

namespace fs = std::filesystem;
using namespace surl;
using surltest::FixtureServer;
using surltest::FixtureResource;

namespace {

/// Builds the site every crawl test runs against: a small but realistic mix of
/// pages, stylesheets, images, a redirect and a 404.
void populate(FixtureServer& server) {
    server.add_html("/", R"(<!doctype html>
<html><head>
  <title>Home</title>
  <link rel="stylesheet" href="/css/site.css">
  <link rel="icon" href="/favicon.ico">
</head><body>
  <h1>Home</h1>
  <img src="/img/logo.png" alt="logo">
  <img srcset="/img/logo.png 1x, /img/logo@2x.png 2x" src="/img/logo.png">
  <a href="/about">About</a>
  <a href="/docs/guide.html">Guide</a>
  <a href="/missing.html">Broken</a>
  <a href="https://elsewhere.invalid/page">Off-site</a>
  <a href="#top">Anchor</a>
  <a href="mailto:hi@example.com">Mail</a>
  <script src="/js/app.js"></script>
</body></html>)");

    server.add_html("/about", R"(<!doctype html>
<html><head><link rel="stylesheet" href="/css/site.css"></head>
<body><h1>About</h1><a href="/">Home</a></body></html>)");

    server.add_html("/docs/guide.html", R"(<!doctype html>
<html><head><link rel="stylesheet" href="../css/site.css"></head>
<body><h1>Guide</h1>
<img src="../img/logo.png">
<a href="/">Home</a>
<a href="/docs/deep/nested.html">Deeper</a>
</body></html>)");

    server.add_html("/docs/deep/nested.html",
                    "<html><body><h1>Nested</h1><a href=\"/\">Home</a></body></html>");

    FixtureResource css;
    css.body = "@import \"reset.css\";\nbody{background:url(/img/bg.png) no-repeat}";
    css.content_type = "text/css";
    css.etag = "\"css-v1\"";
    server.add("/css/site.css", css);

    FixtureResource reset;
    reset.body = "*{margin:0}";
    reset.content_type = "text/css";
    server.add("/css/reset.css", reset);

    const auto binary = [&](const char* path, const char* type, std::string body) {
        FixtureResource resource;
        resource.body = std::move(body);
        resource.content_type = type;
        server.add(path, resource);
    };
    binary("/img/logo.png", "image/png", std::string("\x89PNG\r\n\x1a\n") + "logo-bytes");
    binary("/img/logo@2x.png", "image/png", std::string("\x89PNG\r\n\x1a\n") + "logo-2x");
    binary("/img/bg.png", "image/png", std::string("\x89PNG\r\n\x1a\n") + "bg-bytes");
    binary("/favicon.ico", "image/x-icon", "icon-bytes");
    binary("/js/app.js", "text/javascript", "console.log('hi');");

    FixtureResource missing;
    missing.status = 404;
    missing.body = "gone";
    missing.content_type = "text/html";
    server.add("/missing.html", missing);

    FixtureResource robots;
    robots.body = "User-agent: *\nDisallow: /private/\n";
    robots.content_type = "text/plain";
    server.add("/robots.txt", robots);
}

Options base_options(const fs::path& directory) {
    Options options;
    apply_builtin_defaults(options);
    options.directory = directory;
    options.recursive = true;
    options.max_depth = 5;
    options.concurrency = 4;
    options.timeout_ms = 10000;
    options.retries = 0;
    // The fixture runs on loopback, which SURL refuses to touch by default.
    options.allow_private_hosts = true;
    options.respect_robots = true;
    options.verbosity = 0;
    options.color = false;
    return options;
}

fs::path fresh_dir(const char* name) {
    const fs::path base = fs::temp_directory_path() / "surl-tests" / name;
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base, ec);
    return base;
}

struct CrawlOutcome {
    CrawlResult result;
    fs::path directory;
};

CrawlOutcome crawl(FixtureServer& server, const Options& options, const fs::path& directory,
                   Manifest& manifest) {
    Url start;
    CHECK(parse_url(server.base_url() + "/", start));

    HttpClientConfig http_config;
    http_config.user_agent = options.user_agent;
    http_config.timeout_ms = options.timeout_ms;
    http_config.connect_timeout_ms = 5000;

    std::string error;
    std::unique_ptr<HttpClient> http = HttpClient::create(http_config, error);
    CHECK(http != nullptr);

    manifest.set_source(start.to_string_no_fragment());

    PathMapConfig map_config;
    map_config.root = directory;
    map_config.primary = start;

    Crawler crawler(options, *http, manifest, PathMapper(map_config), directory);
    CrawlOutcome outcome;
    outcome.result = crawler.run(start);
    outcome.directory = directory;
    return outcome;
}

std::string read(const fs::path& file) {
    std::string body;
    CHECK(read_file(file, body));
    return body;
}

bool file_exists(const fs::path& file) {
    std::error_code ec;
    return fs::exists(file, ec);
}

} // namespace

SURL_TEST(integration, mirrors_a_site_and_rewrites_its_links) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("mirror");
    const Options options = base_options(directory);
    Manifest manifest;
    const CrawlOutcome outcome = crawl(server, options, directory, manifest);

    CHECK(outcome.result.ok());
    CHECK(outcome.result.stats.downloaded > 0);

    // Every page and asset reachable from the root should be on disk.
    CHECK(file_exists(directory / "index.html"));
    CHECK(file_exists(directory / "about.html"));
    CHECK(file_exists(directory / "docs" / "guide.html"));
    CHECK(file_exists(directory / "docs" / "deep" / "nested.html"));
    CHECK(file_exists(directory / "css" / "site.css"));
    CHECK(file_exists(directory / "css" / "reset.css"));
    CHECK(file_exists(directory / "img" / "logo.png"));
    CHECK(file_exists(directory / "img" / "bg.png"));
    CHECK(file_exists(directory / "js" / "app.js"));
    CHECK(file_exists(directory / "favicon.ico"));

    const std::string index = read(directory / "index.html");

    // Links to mirrored resources become relative paths.
    CHECK_CONTAINS(index, "href=\"css/site.css\"");
    CHECK_CONTAINS(index, "src=\"img/logo.png\"");
    CHECK_CONTAINS(index, "src=\"js/app.js\"");
    CHECK_CONTAINS(index, "href=\"about.html\"");
    CHECK_CONTAINS(index, "href=\"docs/guide.html\"");

    // Nothing that points back at the live fixture should remain.
    CHECK_NOT_CONTAINS(index, server.base_url() + "/css");
    CHECK_NOT_CONTAINS(index, "href=\"/about\"");

    // References SURL must not touch.
    CHECK_CONTAINS(index, "href=\"#top\"");
    CHECK_CONTAINS(index, "mailto:hi@example.com");
    // An off-site link that was never mirrored stays absolute.
    CHECK_CONTAINS(index, "https://elsewhere.invalid/page");

    // A document in a subdirectory gets correctly relative links.
    const std::string guide = read(directory / "docs" / "guide.html");
    CHECK_CONTAINS(guide, "href=\"../css/site.css\"");
    CHECK_CONTAINS(guide, "src=\"../img/logo.png\"");
    CHECK_CONTAINS(guide, "href=\"../index.html\"");

    // CSS is rewritten too, including @import and url().
    const std::string css = read(directory / "css" / "site.css");
    CHECK_CONTAINS(css, "\"reset.css\"");
    CHECK_CONTAINS(css, "url(../img/bg.png)");

    // srcset candidates are each rewritten.
    CHECK_CONTAINS(index, "logo.png 1x");
    CHECK_CONTAINS(index, "2x");

    // Binary assets are stored byte-for-byte.
    CHECK_CONTAINS(read(directory / "img" / "logo.png"), "logo-bytes");

    // The 404 is recorded as a failure rather than written out.
    CHECK(outcome.result.stats.failed >= 1);
    CHECK_FALSE(file_exists(directory / "missing.html"));

    server.stop();
}

SURL_TEST(integration, writes_a_manifest_that_describes_the_run) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("manifest-run");
    const Options options = base_options(directory);
    Manifest manifest;
    crawl(server, options, directory, manifest);

    std::string error;
    CHECK(manifest.save(Manifest::default_path(directory), error));
    CHECK(file_exists(Manifest::default_path(directory)));

    Manifest reloaded;
    CHECK(reloaded.load(Manifest::default_path(directory), error));
    CHECK(reloaded.size() > 5);

    const std::optional<CacheEntry> index = reloaded.find(server.base_url() + "/");
    CHECK(index.has_value());
    CHECK_EQ(index->path, std::string("index.html"));
    CHECK_EQ(index->status, 200);
    CHECK(index->kind == ResourceKind::Html);
    CHECK_FALSE(index->sha256.empty());

    // The recorded checksum has to describe the rewritten file on disk, not
    // the bytes that came off the wire.
    std::string digest;
    CHECK(sha256_file(directory / "index.html", digest));
    CHECK_EQ(digest, index->sha256);

    server.stop();
}

SURL_TEST(integration, respects_max_depth) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("depth");
    Options options = base_options(directory);
    options.max_depth = 1;
    Manifest manifest;
    crawl(server, options, directory, manifest);

    CHECK(file_exists(directory / "index.html"));
    CHECK(file_exists(directory / "about.html"));         // depth 1
    CHECK(file_exists(directory / "docs" / "guide.html")); // depth 1
    // "nested" sits at depth 2 and must not be fetched.
    CHECK_FALSE(file_exists(directory / "docs" / "deep" / "nested.html"));

    server.stop();
}

SURL_TEST(integration, non_recursive_run_keeps_assets_but_not_other_pages) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("single-page");
    Options options = base_options(directory);
    options.recursive = false;
    Manifest manifest;
    crawl(server, options, directory, manifest);

    CHECK(file_exists(directory / "index.html"));
    CHECK(file_exists(directory / "css" / "site.css"));
    CHECK(file_exists(directory / "img" / "logo.png"));
    CHECK_FALSE(file_exists(directory / "about.html"));

    // An un-mirrored page link must become absolute so it still works.
    CHECK_CONTAINS(read(directory / "index.html"), server.base_url() + "/about");

    server.stop();
}

SURL_TEST(integration, exclude_patterns_are_honoured) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("exclude");
    Options options = base_options(directory);
    options.exclude.push_back("**/docs/**");
    Manifest manifest;
    crawl(server, options, directory, manifest);

    CHECK(file_exists(directory / "index.html"));
    CHECK(file_exists(directory / "about.html"));
    CHECK_FALSE(file_exists(directory / "docs" / "guide.html"));

    server.stop();
}

SURL_TEST(integration, no_assets_skips_images_and_stylesheets) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("no-assets");
    Options options = base_options(directory);
    options.download_assets = false;
    Manifest manifest;
    crawl(server, options, directory, manifest);

    CHECK(file_exists(directory / "index.html"));
    CHECK_FALSE(file_exists(directory / "css" / "site.css"));
    CHECK_FALSE(file_exists(directory / "img" / "logo.png"));

    server.stop();
}

SURL_TEST(integration, robots_txt_is_obeyed_unless_disabled) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);
    server.add_html("/index.html", "<a href=\"/private/hidden.html\">x</a>");
    server.add_html("/", "<a href=\"/private/hidden.html\">x</a>");
    server.add_html("/private/hidden.html", "<h1>hidden</h1>");

    {
        const fs::path directory = fresh_dir("robots-on");
        Manifest manifest;
        crawl(server, base_options(directory), directory, manifest);
        CHECK_FALSE(file_exists(directory / "private" / "hidden.html"));
    }
    {
        const fs::path directory = fresh_dir("robots-off");
        Options options = base_options(directory);
        options.respect_robots = false;
        Manifest manifest;
        crawl(server, options, directory, manifest);
        CHECK(file_exists(directory / "private" / "hidden.html"));
    }

    server.stop();
}

SURL_TEST(integration, dry_run_writes_nothing) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("dry-run");
    Options options = base_options(directory);
    options.dry_run = true;
    Manifest manifest;
    const CrawlOutcome outcome = crawl(server, options, directory, manifest);

    CHECK(outcome.result.stats.downloaded > 0);
    CHECK_FALSE(file_exists(directory / "index.html"));
    CHECK_EQ(directory_file_count(directory), 0ULL);

    server.stop();
}

SURL_TEST(integration, resume_reuses_the_previous_run) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("resume");
    const fs::path manifest_path = Manifest::default_path(directory);

    // First run: everything is fetched.
    {
        Manifest manifest;
        const CrawlOutcome outcome = crawl(server, base_options(directory), directory, manifest);
        CHECK(outcome.result.stats.downloaded > 0);
        std::string error;
        CHECK(manifest.save(manifest_path, error));
    }

    server.reset_hits();

    // Second run with --resume: nothing should be downloaded again.
    {
        Manifest manifest;
        std::string error;
        CHECK(manifest.load(manifest_path, error));

        Options options = base_options(directory);
        options.resume = true;
        const CrawlOutcome outcome = crawl(server, options, directory, manifest);

        CHECK(outcome.result.stats.reused > 0);
        CHECK_EQ(outcome.result.stats.downloaded, 0ULL);
        // robots.txt is still consulted, but no page should be re-requested.
        CHECK_EQ(server.hits("/css/site.css"), 0);
        CHECK_EQ(server.hits("/img/logo.png"), 0);
    }

    // The mirror is still complete and still correctly rewritten.
    CHECK(file_exists(directory / "index.html"));
    CHECK_CONTAINS(read(directory / "index.html"), "href=\"css/site.css\"");

    server.stop();
}

SURL_TEST(integration, update_sends_conditional_requests_and_picks_up_changes) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("update");
    const fs::path manifest_path = Manifest::default_path(directory);

    {
        Manifest manifest;
        crawl(server, base_options(directory), directory, manifest);
        std::string error;
        CHECK(manifest.save(manifest_path, error));
    }

    // The stylesheet keeps its ETag, so --update should get a 304 for it.
    {
        Manifest manifest;
        std::string error;
        CHECK(manifest.load(manifest_path, error));

        Options options = base_options(directory);
        options.update_mode = true;
        const CrawlOutcome outcome = crawl(server, options, directory, manifest);
        CHECK(outcome.result.stats.unchanged > 0);
        CHECK(manifest.save(manifest_path, error));
    }

    // Now the site changes: a new ETag means the body must be refetched.
    server.update_body("/css/site.css", "body{background:#000}", "\"css-v2\"");
    {
        Manifest manifest;
        std::string error;
        CHECK(manifest.load(manifest_path, error));

        Options options = base_options(directory);
        options.update_mode = true;
        crawl(server, options, directory, manifest);
    }

    CHECK_CONTAINS(read(directory / "css" / "site.css"), "#000");

    server.stop();
}

SURL_TEST(integration, force_refetches_everything) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("force");
    {
        Manifest manifest;
        crawl(server, base_options(directory), directory, manifest);
        std::string error;
        CHECK(manifest.save(Manifest::default_path(directory), error));
    }

    server.reset_hits();
    {
        Manifest manifest;
        std::string error;
        CHECK(manifest.load(Manifest::default_path(directory), error));
        Options options = base_options(directory);
        options.resume = true;
        options.force = true;
        const CrawlOutcome outcome = crawl(server, options, directory, manifest);
        CHECK(outcome.result.stats.downloaded > 0);
        CHECK(server.hits("/css/site.css") > 0);
    }

    server.stop();
}

SURL_TEST(integration, max_files_stops_the_run_early) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("max-files");
    Options options = base_options(directory);
    options.max_files = 3;
    options.concurrency = 1;
    Manifest manifest;
    const CrawlOutcome outcome = crawl(server, options, directory, manifest);

    CHECK_FALSE(outcome.result.stop_reason.empty());
    CHECK_CONTAINS(outcome.result.stop_reason, "max-files");
    CHECK(directory_file_count(directory) <= 6); // includes staged raw copies

    server.stop();
}

SURL_TEST(integration, no_rewrite_keeps_the_original_bytes) {
    FixtureServer server;
    CHECK(server.start());
    populate(server);

    const fs::path directory = fresh_dir("no-rewrite");
    Options options = base_options(directory);
    options.rewrite_links = false;
    Manifest manifest;
    crawl(server, options, directory, manifest);

    const std::string index = read(directory / "index.html");
    CHECK_CONTAINS(index, "href=\"/css/site.css\"");
    CHECK_CONTAINS(index, "src=\"/img/logo.png\"");

    server.stop();
}

SURL_TEST(integration, follows_redirects_to_the_final_url) {
    FixtureServer server;
    CHECK(server.start());

    FixtureResource redirect;
    redirect.status = 302;
    redirect.location = "/real/page.html";
    redirect.body = "";
    server.add("/start", redirect);
    server.add_html("/real/page.html", "<h1>arrived</h1><img src=\"pic.png\">");

    FixtureResource picture;
    picture.body = "pixels";
    picture.content_type = "image/png";
    server.add("/real/pic.png", picture);

    FixtureResource robots;
    robots.body = "";
    robots.content_type = "text/plain";
    server.add("/robots.txt", robots);

    server.add_html("/", "<a href=\"/start\">go</a>");

    const fs::path directory = fresh_dir("redirect");
    Manifest manifest;
    crawl(server, base_options(directory), directory, manifest);

    // The image is resolved against the URL the content actually came from,
    // not the URL that was requested.
    CHECK(file_exists(directory / "real" / "pic.png"));

    server.stop();
}
