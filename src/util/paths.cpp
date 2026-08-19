#include "surl/util/paths.hpp"

#include "surl/util/fsutil.hpp"

#include <cstdint>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <pwd.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <cstring>
#endif

namespace fs = std::filesystem;

namespace surl {
namespace {

#ifdef _WIN32
std::optional<std::string> get_env_wide(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return std::nullopt;
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(), needed);
    if (written == 0 || written >= needed) return std::nullopt;
    buffer.resize(written);
    if (buffer.empty()) return std::nullopt;
    return path_to_utf8(fs::path(buffer));
}
#endif

fs::path env_path(std::string_view name) {
    const std::optional<std::string> value = get_env(name);
    if (!value || value->empty()) return {};
    return utf8_to_path(*value);
}

} // namespace

std::optional<std::string> get_env(std::string_view name) {
#ifdef _WIN32
    const fs::path wide_name = utf8_to_path(name);
    return get_env_wide(wide_name.c_str());
#else
    const std::string key(name);
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::string(value);
#endif
}

fs::path home_dir() {
#ifdef _WIN32
    fs::path profile = env_path("USERPROFILE");
    if (!profile.empty()) return profile;
    const fs::path drive = env_path("HOMEDRIVE");
    const fs::path rest = env_path("HOMEPATH");
    if (!drive.empty() && !rest.empty()) return drive / rest;
    return fs::current_path();
#else
    const fs::path home = env_path("HOME");
    if (!home.empty()) return home;
    if (const passwd* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr) {
        return fs::path(pw->pw_dir);
    }
    return fs::current_path();
#endif
}

fs::path config_dir() {
    // An explicit override always wins; the test suite relies on it.
    const fs::path override_dir = env_path("SURL_CONFIG_DIR");
    if (!override_dir.empty()) return override_dir;

#ifdef _WIN32
    const fs::path appdata = env_path("APPDATA");
    if (!appdata.empty()) return appdata / "SURL";
    return home_dir() / "AppData" / "Roaming" / "SURL";
#elif defined(__APPLE__)
    return home_dir() / "Library" / "Application Support" / "SURL";
#else
    const fs::path xdg = env_path("XDG_CONFIG_HOME");
    if (!xdg.empty()) return xdg / "surl";
    return home_dir() / ".config" / "surl";
#endif
}

fs::path cache_dir() {
    const fs::path override_dir = env_path("SURL_CACHE_DIR");
    if (!override_dir.empty()) return override_dir;

#ifdef _WIN32
    const fs::path local = env_path("LOCALAPPDATA");
    if (!local.empty()) return local / "SURL" / "cache";
    return home_dir() / "AppData" / "Local" / "SURL" / "cache";
#elif defined(__APPLE__)
    return home_dir() / "Library" / "Caches" / "SURL";
#else
    const fs::path xdg = env_path("XDG_CACHE_HOME");
    if (!xdg.empty()) return xdg / "surl";
    return home_dir() / ".cache" / "surl";
#endif
}

fs::path log_dir() {
    const fs::path override_dir = env_path("SURL_LOG_DIR");
    if (!override_dir.empty()) return override_dir;

#ifdef _WIN32
    const fs::path local = env_path("LOCALAPPDATA");
    if (!local.empty()) return local / "SURL" / "logs";
    return home_dir() / "AppData" / "Local" / "SURL" / "logs";
#elif defined(__APPLE__)
    return home_dir() / "Library" / "Logs" / "SURL";
#else
    const fs::path xdg = env_path("XDG_STATE_HOME");
    if (!xdg.empty()) return xdg / "surl" / "logs";
    return home_dir() / ".local" / "state" / "surl" / "logs";
#endif
}

fs::path config_file() { return config_dir() / "config.json"; }

fs::path executable_path() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD written =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) return {};
        if (written < buffer.size()) {
            buffer.resize(written);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    // macOS has no /proc; ask dyld how long the path is, then for the path.
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, char{});
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return fs::current_path();
    buffer.resize(std::strlen(buffer.c_str()));
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(fs::path(buffer), ec);
    return ec ? fs::path(buffer) : resolved;
#else
    std::error_code ec;
    const fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return self;
    return fs::current_path();
#endif
}

fs::path executable_dir() {
    const fs::path exe = executable_path();
    return exe.empty() ? fs::current_path() : exe.parent_path();
}

std::vector<fs::path> path_entries() {
    std::vector<fs::path> out;
    const std::optional<std::string> raw = get_env("PATH");
    if (!raw) return out;

#ifdef _WIN32
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif

    std::size_t start = 0;
    const std::string& text = *raw;
    while (start <= text.size()) {
        const std::size_t pos = text.find(kSeparator, start);
        const std::string piece =
            text.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (!piece.empty()) out.push_back(utf8_to_path(piece));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out;
}

bool path_contains(const fs::path& directory) {
    std::error_code ec;
    const fs::path target = fs::weakly_canonical(directory, ec);
    if (ec) return false;

    for (const fs::path& entry : path_entries()) {
        std::error_code entry_ec;
        const fs::path resolved = fs::weakly_canonical(entry, entry_ec);
        if (entry_ec) continue;
        if (resolved == target) return true;
    }
    return false;
}

} // namespace surl
