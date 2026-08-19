#pragma once

#include "surl/cli/options.hpp"

#include <filesystem>
#include <string>

namespace surl {

/// Process exit codes. Anything above 2 means "the work ran but something in
/// it failed", which lets scripts tell a usage mistake from a partial mirror.
enum ExitCode {
    kExitOk = 0,
    kExitError = 1,
    kExitUsage = 2,
    kExitPartial = 3,
};

/// Dispatches to the handler for options.command.
int run_command(const Options& options);

int cmd_clone(const Options& options);
int cmd_serve(const Options& options);
int cmd_inspect(const Options& options);
int cmd_check(const Options& options);
int cmd_clean(const Options& options);
int cmd_doctor(const Options& options);
int cmd_config(const Options& options);
int cmd_install(const Options& options);
int cmd_uninstall(const Options& options);
int cmd_update(const Options& options);

/// Adds @p directory to the user's PATH. Returns false with a reason when the
/// change could not be made; already being present counts as success.
bool add_directory_to_path(const std::filesystem::path& directory, bool& already_present,
                           std::string& error);

/// Removes @p directory from the user's PATH.
bool remove_directory_from_path(const std::filesystem::path& directory, bool& was_present,
                                std::string& error);

/// Resolves the directory argument for commands that operate on a mirror,
/// defaulting to the current directory and reporting a clear error when the
/// path does not look like a SURL mirror.
bool resolve_mirror_directory(const Options& options, std::filesystem::path& out,
                              std::string& error);

} // namespace surl
