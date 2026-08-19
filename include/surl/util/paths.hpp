#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace surl {

/// Per-user locations SURL is allowed to write to. These deliberately live
/// outside the installation directory so that an uninstall never has to touch
/// application data, and so that a machine-wide install still works for a user
/// without write access to Program Files.
///
///   Windows: config  %APPDATA%\SURL
///            cache   %LOCALAPPDATA%\SURL\cache
///            logs    %LOCALAPPDATA%\SURL\logs
///   Linux:   XDG_CONFIG_HOME / XDG_CACHE_HOME / XDG_STATE_HOME
///   macOS:   ~/Library/Application Support/SURL, ~/Library/Caches/SURL
std::filesystem::path config_dir();
std::filesystem::path cache_dir();
std::filesystem::path log_dir();

/// Path of the user configuration file (config.json inside config_dir()).
std::filesystem::path config_file();

/// Reads an environment variable as UTF-8, if it is set and non-empty.
std::optional<std::string> get_env(std::string_view name);

/// Absolute path of the running executable.
std::filesystem::path executable_path();

/// Directory containing the running executable.
std::filesystem::path executable_dir();

/// Current user's home directory.
std::filesystem::path home_dir();

/// True when SURL appears to have been started from an installed location that
/// is on PATH (used by `surl doctor` to explain what it found).
bool path_contains(const std::filesystem::path& directory);

/// Every entry of the PATH environment variable, in order.
std::vector<std::filesystem::path> path_entries();

} // namespace surl
