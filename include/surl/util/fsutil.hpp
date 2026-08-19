#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace surl {

/// Reads a whole file into memory. Returns false when the file is unreadable.
bool read_file(const std::filesystem::path& path, std::string& out);

/// Writes @p data to @p path, creating parent directories as needed. The write
/// goes to a temporary sibling first and is then renamed, so a crash or a
/// cancelled run never leaves a half-written asset behind.
bool write_file_atomic(const std::filesystem::path& path, std::string_view data,
                       std::string& error);

/// Ensures the directory exists, reporting the reason on failure.
bool ensure_directory(const std::filesystem::path& path, std::string& error);

/// Total byte size of every regular file below @p root.
std::uint64_t directory_size(const std::filesystem::path& root);

/// Number of regular files below @p root.
std::uint64_t directory_file_count(const std::filesystem::path& root);

/// Lists every regular file below @p root as a path relative to @p root, using
/// forward slashes. The list is sorted for reproducible output.
std::vector<std::string> list_files_relative(const std::filesystem::path& root);

/// Converts a filesystem path to a UTF-8 string. On Windows this goes through
/// the wide-character representation so non-ASCII paths survive intact.
std::string path_to_utf8(const std::filesystem::path& path);

/// Converts a UTF-8 string to a filesystem path.
std::filesystem::path utf8_to_path(std::string_view text);

/// True when @p candidate is inside @p root after both are made canonical-ish.
/// Used to make sure nothing a remote server says can escape the output root.
bool is_inside(const std::filesystem::path& root, const std::filesystem::path& candidate);

/// Replaces characters Windows forbids in file names and neutralises reserved
/// device names such as CON, NUL and COM1.
std::string sanitize_path_component(std::string_view component);

/// Removes a directory tree, tolerating read-only files. Returns false and sets
/// @p error when something could not be deleted.
bool remove_tree(const std::filesystem::path& path, std::string& error);

} // namespace surl
