#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace surl {

/// Self-contained SHA-256 (FIPS 180-4). SURL ships no crypto dependency: the
/// digest is only used for integrity checks and cache keys, never for secrets.
class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t len);
    void update(std::string_view s) { update(s.data(), s.size()); }

    /// Finalises the digest. The object must not be reused afterwards.
    std::array<std::uint8_t, 32> digest();
    std::string hex();

private:
    void transform(const std::uint8_t block[64]);

    std::uint32_t state_[8];
    std::uint64_t bit_count_;
    std::uint8_t buffer_[64];
    std::size_t buffer_len_;
};

std::string sha256_hex(std::string_view data);

/// Streams a file through SHA-256. Returns false if the file cannot be read.
bool sha256_file(const std::filesystem::path& path, std::string& out_hex);

/// Fast non-cryptographic hash used for short, collision-tolerant path suffixes.
std::uint64_t fnv1a64(std::string_view data);

} // namespace surl
