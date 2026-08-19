#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace surl {

enum class LogLevel {
    Quiet = 0,   ///< errors only
    Normal = 1,  ///< progress + warnings
    Verbose = 2, ///< per-resource detail
    Debug = 3    ///< protocol-level detail
};

/// Thread-safe console logger. Everything except explicit machine-readable
/// output (--json, --list) goes to stderr so stdout stays pipeable.
class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel level() const;

    void set_color(bool enabled);
    bool color() const;

    void error(std::string_view message);
    void warn(std::string_view message);
    void info(std::string_view message);
    void verbose(std::string_view message);
    void debug(std::string_view message);

    /// Prints a line to stdout untouched (machine-readable output).
    void out(std::string_view line);

    std::string paint(std::string_view code, std::string_view text) const;

private:
    Logger() = default;
    void emit(LogLevel required, std::string_view prefix, std::string_view color,
              std::string_view message);

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Normal;
    bool color_ = true;
};

/// ANSI colour codes, used only when colour output is enabled.
namespace color {
inline constexpr std::string_view kReset = "\033[0m";
inline constexpr std::string_view kBold = "\033[1m";
inline constexpr std::string_view kDim = "\033[2m";
inline constexpr std::string_view kRed = "\033[31m";
inline constexpr std::string_view kGreen = "\033[32m";
inline constexpr std::string_view kYellow = "\033[33m";
inline constexpr std::string_view kBlue = "\033[34m";
inline constexpr std::string_view kMagenta = "\033[35m";
inline constexpr std::string_view kCyan = "\033[36m";
} // namespace color

/// Enables ANSI escape processing on the Windows console. No-op elsewhere.
void enable_virtual_terminal();

/// True when the process is attached to an interactive terminal.
bool stderr_is_tty();

inline Logger& log() { return Logger::instance(); }

} // namespace surl
