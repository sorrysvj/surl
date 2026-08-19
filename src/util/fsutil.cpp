#include "surl/util/fsutil.hpp"

#include "surl/util/strings.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace surl {
namespace {

/// Windows reserves these names in every directory, with or without extension.
bool is_reserved_device_name(std::string_view stem) {
    static const char* const kReserved[] = {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::string lowered = to_lower(stem);
    for (const char* name : kReserved) {
        if (lowered == name) return true;
    }
    return false;
}

std::atomic<std::uint64_t> g_temp_counter{0};

} // namespace

std::string path_to_utf8(const fs::path& path) {
#ifdef _WIN32
    const std::u8string u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return path.string();
#endif
}

fs::path utf8_to_path(std::string_view text) {
#ifdef _WIN32
    const std::u8string u8(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return fs::path(u8);
#else
    return fs::path(std::string(text));
#endif
}

bool read_file(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool ensure_directory(const fs::path& path, std::string& error) {
    if (path.empty()) return true;
    std::error_code ec;
    if (fs::exists(path, ec)) {
        if (fs::is_directory(path, ec)) return true;
        error = "path exists but is not a directory: " + path_to_utf8(path);
        return false;
    }
    fs::create_directories(path, ec);
    if (ec) {
        error = "could not create directory " + path_to_utf8(path) + ": " + ec.message();
        return false;
    }
    return true;
}

bool write_file_atomic(const fs::path& path, std::string_view data, std::string& error) {
    const fs::path parent = path.parent_path();
    if (!parent.empty() && !ensure_directory(parent, error)) return false;

    // A unique sibling keeps concurrent writers from colliding.
    const std::uint64_t ticket = g_temp_counter.fetch_add(1, std::memory_order_relaxed);
    fs::path temp = path;
    temp += ".surl-tmp-" + std::to_string(ticket);

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "could not open " + path_to_utf8(temp) + " for writing";
            return false;
        }
        if (!data.empty()) {
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        out.flush();
        if (!out) {
            error = "could not write " + path_to_utf8(temp);
            out.close();
            std::error_code ignored;
            fs::remove(temp, ignored);
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp, path, ec);
    if (ec) {
        // rename() does not overwrite on every platform; retry explicitly.
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temp, path, ec);
    }
    if (ec) {
        error = "could not move temporary file into place at " + path_to_utf8(path) +
                ": " + ec.message();
        std::error_code ignored;
        fs::remove(temp, ignored);
        return false;
    }
    return true;
}

std::uint64_t directory_size(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return 0;
    std::uint64_t total = 0;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code file_ec;
        if (it->is_regular_file(file_ec)) {
            const std::uintmax_t size = fs::file_size(it->path(), file_ec);
            if (!file_ec) total += static_cast<std::uint64_t>(size);
        }
    }
    return total;
}

std::uint64_t directory_file_count(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec)) return 0;
    std::uint64_t count = 0;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code file_ec;
        if (it->is_regular_file(file_ec)) ++count;
    }
    return count;
}

std::vector<std::string> list_files_relative(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(root, ec)) return out;

    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec)) continue;
        const fs::path relative = fs::relative(it->path(), root, file_ec);
        if (file_ec) continue;
        out.push_back(replace_all(path_to_utf8(relative), "\\", "/"));
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool is_inside(const fs::path& root, const fs::path& candidate) {
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(root, ec);
    if (ec) return false;
    ec.clear();
    const fs::path target = fs::weakly_canonical(candidate, ec);
    if (ec) return false;

    auto base_it = base.begin();
    auto target_it = target.begin();
    for (; base_it != base.end(); ++base_it, ++target_it) {
        if (target_it == target.end()) return false;
        if (*base_it != *target_it) return false;
    }
    return true;
}

std::string sanitize_path_component(std::string_view component) {
    std::string out;
    out.reserve(component.size());

    for (const unsigned char c : component) {
        // Control characters and the Windows-reserved set become '_'.
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' ||
            c == '\\' || c == '|' || c == '?' || c == '*' || c == 0x7F) {
            out.push_back('_');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }

    // "." and ".." must never survive: they are how a remote URL would try to
    // climb out of the output directory. Rename rather than escape them, so
    // the result cannot be turned back into a traversal by any later step.
    if (out == ".") return "_dot";
    if (out == "..") return "_dotdot";

    // Windows silently strips trailing dots and spaces, so "a." and "a" would
    // land on the same file. Strip them here and mark that we did.
    const std::size_t before_trim = out.size();
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.size() != before_trim) out.push_back('_');

    const std::size_t dot = out.find('.');
    const std::string stem = (dot == std::string::npos) ? out : out.substr(0, dot);
    if (is_reserved_device_name(stem)) out.insert(out.begin(), '_');

    if (out.empty()) out = "_";

    // Keep individual components well inside the 255-byte limit common to
    // NTFS, ext4 and APFS.
    if (out.size() > 180) {
        const std::size_t ext_dot = out.rfind('.');
        std::string extension;
        if (ext_dot != std::string::npos && out.size() - ext_dot <= 16) {
            extension = out.substr(ext_dot);
        }
        out = out.substr(0, 180 - extension.size()) + extension;
    }
    return out;
}

bool remove_tree(const fs::path& path, std::string& error) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return true;

    // Clear read-only bits first; Windows refuses to unlink read-only files.
    for (fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec),
         end;
         it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code perm_ec;
        fs::permissions(it->path(), fs::perms::owner_write, fs::perm_options::add, perm_ec);
    }

    ec.clear();
    fs::remove_all(path, ec);
    if (ec) {
        error = "could not remove " + path_to_utf8(path) + ": " + ec.message();
        return false;
    }
    return true;
}

} // namespace surl
