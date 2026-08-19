#include "surl/cli/help.hpp"
#include "surl/cli/options.hpp"
#include "surl/cmd/commands.hpp"
#include "surl/util/log.hpp"
#include "surl/util/paths.hpp"

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

surl::LogLevel to_log_level(int verbosity) {
    switch (verbosity) {
    case 0: return surl::LogLevel::Quiet;
    case 2: return surl::LogLevel::Verbose;
    case 3: return surl::LogLevel::Debug;
    default: return surl::LogLevel::Normal;
    }
}

/// Colour is on when the terminal can show it and the user has not opted out.
bool should_use_color(const surl::Options& options) {
    if (!options.color || options.ci) return false;
    // Respect the de-facto standards before falling back to a TTY check.
    if (surl::get_env("NO_COLOR").has_value()) return false;
    if (surl::get_env("CI").has_value()) return false;
    return surl::stderr_is_tty();
}

int surl_main(int argc, char** argv) {
    surl::enable_virtual_terminal();

    surl::Options options;
    surl::apply_builtin_defaults(options);

    std::string error;
    if (!surl::apply_config_file(options, error)) {
        // A broken config file must not stop `surl --help` from working, but
        // the user needs to know their settings were ignored.
        std::fprintf(stderr, "warning: %s\n", error.c_str());
    }

    if (!surl::parse_command_line(argc, argv, options, error)) {
        std::fprintf(stderr, "error: %s\n\n%s\n", error.c_str(), surl::usage_line().c_str());
        return surl::kExitUsage;
    }

    surl::log().set_level(to_log_level(options.verbosity));
    surl::log().set_color(should_use_color(options));

    if (options.show_help) {
        std::fputs(surl::help_text().c_str(), stdout);
        return surl::kExitOk;
    }
    if (options.show_version) {
        std::fputs(surl::version_text(options.verbosity >= 2).c_str(), stdout);
        std::fputc('\n', stdout);
        return surl::kExitOk;
    }

    try {
        return surl::run_command(options);
    } catch (const std::exception& ex) {
        surl::log().error(std::string("unexpected failure: ") + ex.what());
        return surl::kExitError;
    } catch (...) {
        surl::log().error("unexpected failure");
        return surl::kExitError;
    }
}

} // namespace

#ifdef _WIN32

/// Windows hands wide-character arguments to wmain; convert them to UTF-8 once
/// here so the rest of the program only ever deals with one encoding.
int wmain(int argc, wchar_t** wargv) {
    std::vector<std::string> storage;
    storage.reserve(static_cast<std::size_t>(argc));

    for (int i = 0; i < argc; ++i) {
        const int needed =
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string utf8;
        if (needed > 0) {
            utf8.resize(static_cast<std::size_t>(needed));
            WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, utf8.data(), needed, nullptr,
                                nullptr);
            if (!utf8.empty() && utf8.back() == '\0') utf8.pop_back();
        }
        storage.push_back(std::move(utf8));
    }

    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (std::string& argument : storage) argv.push_back(argument.data());
    argv.push_back(nullptr);

    return surl_main(argc, argv.data());
}

#else

int main(int argc, char** argv) { return surl_main(argc, argv); }

#endif
