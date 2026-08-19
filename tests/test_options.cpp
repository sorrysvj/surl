#include "test_framework.hpp"

#include "surl/cli/options.hpp"

#include <string>
#include <vector>

using namespace surl;

namespace {

/// Parses an argument list the way main() would, minus the config file.
bool parse(const std::vector<std::string>& arguments, Options& out, std::string& error) {
    std::vector<std::string> storage;
    storage.push_back("surl");
    for (const std::string& argument : arguments) storage.push_back(argument);

    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (std::string& item : storage) argv.push_back(item.data());

    out = Options{};
    apply_builtin_defaults(out);
    return parse_command_line(static_cast<int>(argv.size()), argv.data(), out, error);
}

Options must_parse(const std::vector<std::string>& arguments) {
    Options options;
    std::string error;
    if (!parse(arguments, options, error)) {
        ::surltest::fail("expected the arguments to parse, got: " + error, __FILE__, __LINE__);
    }
    return options;
}

} // namespace

SURL_TEST(options, a_bare_url_means_clone) {
    const Options options = must_parse({"https://example.com"});
    CHECK(options.command == Command::Clone);
    CHECK_EQ(options.url, std::string("https://example.com"));
}

SURL_TEST(options, accepts_an_explicit_subcommand) {
    const Options options = must_parse({"mirror", "https://example.com"});
    CHECK(options.command == Command::Mirror);
    CHECK_EQ(options.url, std::string("https://example.com"));
    // mirror implies recursion; that is its whole point.
    CHECK(options.recursive);
}

SURL_TEST(options, a_bare_host_is_treated_as_a_url_not_a_command) {
    const Options options = must_parse({"example.com/path"});
    CHECK(options.command == Command::Clone);
    CHECK_EQ(options.url, std::string("example.com/path"));
}

SURL_TEST(options, rejects_an_unknown_command) {
    Options options;
    std::string error;
    CHECK_FALSE(parse({"frobnicate"}, options, error));
    CHECK_CONTAINS(error, "unknown command");
}

SURL_TEST(options, rejects_an_unknown_option) {
    Options options;
    std::string error;
    CHECK_FALSE(parse({"https://example.com", "--wat"}, options, error));
    CHECK_CONTAINS(error, "unknown option");
}

SURL_TEST(options, directory_flags_are_interchangeable) {
    CHECK_EQ(must_parse({"https://example.com", "-d", "out"}).directory.filename().string(),
             std::string("out"));
    CHECK_EQ(must_parse({"https://example.com", "--output", "out"}).directory.filename().string(),
             std::string("out"));
    CHECK_EQ(must_parse({"https://example.com", "--directory=out"}).directory.filename().string(),
             std::string("out"));
}

SURL_TEST(options, parses_transfer_settings) {
    const Options options = must_parse({"https://example.com", "-c", "12", "-t", "45s",
                                        "--retries", "5", "--delay", "250ms",
                                        "--rate-limit", "500k"});
    CHECK_EQ(options.concurrency, 12);
    CHECK_EQ(options.timeout_ms, 45000u);
    CHECK_EQ(options.retries, 5);
    CHECK_EQ(options.delay_ms, 250ULL);
    CHECK_EQ(options.rate_limit_bytes_per_sec, 512000ULL);
}

SURL_TEST(options, parses_budgets) {
    const Options options = must_parse({"https://example.com", "--max-file-size", "10M",
                                        "--max-total-size", "1G", "--max-files", "250"});
    CHECK_EQ(options.max_file_size, 10ULL * 1024 * 1024);
    CHECK_EQ(options.max_total_size, 1024ULL * 1024 * 1024);
    CHECK_EQ(options.max_files, 250ULL);
}

SURL_TEST(options, rejects_out_of_range_values) {
    Options options;
    std::string error;
    CHECK_FALSE(parse({"https://example.com", "-c", "0"}, options, error));
    CHECK_FALSE(parse({"https://example.com", "-c", "1000"}, options, error));
    CHECK_FALSE(parse({"https://example.com", "-t", "nope"}, options, error));
    CHECK_FALSE(parse({"https://example.com", "--port", "70000"}, options, error));
}

SURL_TEST(options, reports_a_missing_value) {
    Options options;
    std::string error;
    CHECK_FALSE(parse({"https://example.com", "-d"}, options, error));
    CHECK_CONTAINS(error, "requires a value");
}

SURL_TEST(options, collects_repeatable_flags) {
    const Options options = must_parse({"https://example.com",
                                        "--include", "*.html", "--include", "*.css",
                                        "--exclude", "**/admin/**",
                                        "-H", "X-Test: 1", "-H", "Authorization: Bearer x",
                                        "--cookie", "a=1", "--cookie", "b=2"});
    CHECK_EQ(options.include.size(), static_cast<std::size_t>(2));
    CHECK_EQ(options.exclude.size(), static_cast<std::size_t>(1));
    CHECK_EQ(options.headers.size(), static_cast<std::size_t>(2));
    CHECK_EQ(options.headers[0].first, std::string("X-Test"));
    CHECK_EQ(options.headers[0].second, std::string("1"));
    CHECK_EQ(options.cookies.size(), static_cast<std::size_t>(2));
}

SURL_TEST(options, rejects_a_malformed_header) {
    Options options;
    std::string error;
    CHECK_FALSE(parse({"https://example.com", "-H", "nocolon"}, options, error));
    CHECK_CONTAINS(error, "Name: value");
}

SURL_TEST(options, verbosity_flags) {
    CHECK_EQ(must_parse({"https://example.com", "-v"}).verbosity, 2);
    CHECK_EQ(must_parse({"https://example.com", "--debug"}).verbosity, 3);
    CHECK_EQ(must_parse({"https://example.com", "-q"}).verbosity, 0);
    CHECK_EQ(must_parse({"https://example.com"}).verbosity, 1);
}

SURL_TEST(options, ci_disables_colour) {
    const Options options = must_parse({"https://example.com", "--ci"});
    CHECK(options.ci);
    CHECK_FALSE(options.color);
}

SURL_TEST(options, list_implies_dry_run) {
    const Options options = must_parse({"https://example.com", "--list"});
    CHECK(options.list_only);
    CHECK(options.dry_run);
}

SURL_TEST(options, directory_commands_take_a_positional_path) {
    const Options serve = must_parse({"serve", "./site", "-p", "9000"});
    CHECK(serve.command == Command::Serve);
    CHECK_EQ(serve.directory.filename().string(), std::string("site"));
    CHECK_EQ(serve.port, static_cast<std::uint16_t>(9000));

    const Options inspect = must_parse({"inspect", "./site"});
    CHECK(inspect.command == Command::Inspect);
    CHECK_EQ(inspect.directory.filename().string(), std::string("site"));
}

SURL_TEST(options, config_keeps_its_positional_arguments) {
    const Options options = must_parse({"config", "set", "concurrency", "16"});
    CHECK(options.command == Command::Config);
    CHECK_EQ(options.positional.size(), static_cast<std::size_t>(3));
    CHECK_EQ(options.positional[0], std::string("set"));
    CHECK_EQ(options.positional[2], std::string("16"));
}

SURL_TEST(options, update_check_flag) {
    const Options options = must_parse({"update", "--check"});
    CHECK(options.command == Command::Update);
    CHECK(options.check_only);
}

SURL_TEST(options, help_and_version) {
    CHECK(must_parse({"--help"}).show_help);
    CHECK(must_parse({"-h"}).show_help);
    CHECK(must_parse({"--version"}).show_version);
    // No arguments at all should show help rather than fail.
    CHECK(must_parse({}).show_help);
}

SURL_TEST(options, double_dash_ends_option_parsing) {
    const Options options = must_parse({"config", "set", "--", "--weird-key", "value"});
    CHECK_EQ(options.positional.size(), static_cast<std::size_t>(3));
    CHECK_EQ(options.positional[1], std::string("--weird-key"));
}

SURL_TEST(options, default_user_agent_identifies_surl) {
    const std::string agent = default_user_agent();
    CHECK_CONTAINS(agent, "surl/");
    CHECK_CONTAINS(agent, "github.com");
}
