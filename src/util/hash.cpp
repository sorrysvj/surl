#include "surl/util/hash.hpp"

#include "surl/util/strings.hpp"

#include <cstring>
#include <fstream>
#include <vector>

namespace surl {
namespace {

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

} // namespace

Sha256::Sha256() : bit_count_(0), buffer_len_(0) {
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
    std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::transform(const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<std::uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, std::size_t len) {
    const std::uint8_t* p = static_cast<const std::uint8_t*>(data);
    bit_count_ += static_cast<std::uint64_t>(len) * 8;

    while (len > 0) {
        const std::size_t take = (64 - buffer_len_ < len) ? (64 - buffer_len_) : len;
        std::memcpy(buffer_ + buffer_len_, p, take);
        buffer_len_ += take;
        p += take;
        len -= take;
        if (buffer_len_ == 64) {
            transform(buffer_);
            buffer_len_ = 0;
        }
    }
}

std::array<std::uint8_t, 32> Sha256::digest() {
    const std::uint64_t total_bits = bit_count_;

    // Padding: 0x80, then zeroes, then the 64-bit big-endian length.
    const std::uint8_t one = 0x80;
    update(&one, 1);
    const std::uint8_t zero = 0x00;
    while (buffer_len_ != 56) {
        update(&zero, 1);
    }

    std::uint8_t length_bytes[8];
    for (int i = 0; i < 8; ++i) {
        length_bytes[i] = static_cast<std::uint8_t>((total_bits >> (56 - i * 8)) & 0xFF);
    }
    // Feed the length directly: update() would corrupt bit_count_, which no
    // longer matters, but the buffer must be flushed exactly once here.
    std::memcpy(buffer_ + buffer_len_, length_bytes, 8);
    buffer_len_ += 8;
    transform(buffer_);
    buffer_len_ = 0;

    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFF);
        out[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFF);
        out[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFF);
        out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFF);
    }
    return out;
}

std::string Sha256::hex() {
    const std::array<std::uint8_t, 32> d = digest();
    return to_hex(d.data(), d.size());
}

std::string sha256_hex(std::string_view data) {
    Sha256 h;
    h.update(data);
    return h.hex();
}

bool sha256_file(const std::filesystem::path& path, std::string& out_hex) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    Sha256 h;
    std::vector<char> buf(64 * 1024);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) h.update(buf.data(), static_cast<std::size_t>(got));
    }
    if (in.bad()) return false;

    out_hex = h.hex();
    return true;
}

std::uint64_t fnv1a64(std::string_view data) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : data) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace surl
