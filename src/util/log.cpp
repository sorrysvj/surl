#include "surl/util/log.hpp"

#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace surl {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::set_color(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    color_ = enabled;
}

bool Logger::color() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return color_;
}

std::string Logger::paint(std::string_view code, std::string_view text) const {
    if (!color()) return std::string(text);
    std::string out;
    out.reserve(code.size() + text.size() + color::kReset.size());
    out.append(code);
    out.append(text);
    out.append(color::kReset);
    return out;
}

void Logger::emit(LogLevel required, std::string_view prefix, std::string_view col,
                  std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level_) < static_cast<int>(required)) return;

    std::string line;
    if (!prefix.empty()) {
        if (color_) {
            line.append(col);
            line.append(prefix);
            line.append(color::kReset);
        } else {
            line.append(prefix);
        }
        line.push_back(' ');
    }
    line.append(message);
    line.push_back('\n');

    std::fwrite(line.data(), 1, line.size(), stderr);
    std::fflush(stderr);
}

void Logger::error(std::string_view message) {
    emit(LogLevel::Quiet, "error:", color::kRed, message);
}

void Logger::warn(std::string_view message) {
    emit(LogLevel::Normal, "warning:", color::kYellow, message);
}

void Logger::info(std::string_view message) {
    emit(LogLevel::Normal, "", "", message);
}

void Logger::verbose(std::string_view message) {
    emit(LogLevel::Verbose, "·", color::kDim, message);
}

void Logger::debug(std::string_view message) {
    emit(LogLevel::Debug, "debug:", color::kMagenta, message);
}

void Logger::out(std::string_view line) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void enable_virtual_terminal() {
#ifdef _WIN32
    // Ask the console for UTF-8 and ANSI escape handling; both are available on
    // Windows 10 1607+ and fail harmlessly when output is redirected.
    SetConsoleOutputCP(CP_UTF8);
    for (const DWORD handle_id : {STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        const HANDLE handle = GetStdHandle(handle_id);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) continue;
        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode)) continue;
        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

bool stderr_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

} // namespace surl
