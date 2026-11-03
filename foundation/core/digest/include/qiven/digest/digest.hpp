#pragma once

// digest: SHA-256, HMAC-SHA-256, and plain and keyed digests. SHA-256 is
// FIPS 180-4's and HMAC-SHA-256 RFC 2104's, written here with no
// intrinsics, a key longer than a block hashed first. write_hex writes a
// prefix and a digest's 64 lowercase hex digits, plain sha256: and keyed
// hmac-sha256: with the digest's hex, each into the caller's string,
// replaced only on success: one allocation, the result's. keyed takes a
// 32-byte key's bytes and the caller's wipe, which wipes the HMAC's two
// key-derived states and the padded key's block on the stack before the
// text is written. The functions but keyed's and same are constexpr and
// templates on the string type, so their vectors run in constant
// evaluation too; they read by index, with no pointer arithmetic. same
// compares in a time that does not depend on where two texts differ.
//
// thread: none.

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace qiven::digest {

inline constexpr std::size_t bytes = 32;  // a digest's, and a key's
using Bytes = std::array<std::uint8_t, bytes>;

namespace detail {

inline constexpr std::size_t block = 64;
using Block = std::array<std::uint8_t, block>;

inline constexpr std::array<std::uint32_t, 64> round_k = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept { return (x >> n) | (x << (32U - n)); }

}  // namespace detail

// SHA-256 over bytes given in parts.
class Sha256 {
public:
    constexpr void push(std::uint8_t b) noexcept {
        block_[used_] = b;
        ++used_;
        ++count_;
        if (used_ == detail::block) {
            compress();
            used_ = 0;
        }
    }
    constexpr void update(std::string_view data) noexcept {
        for (const char c : data) {
            push(static_cast<std::uint8_t>(c));
        }
    }
    constexpr void update(std::span<const std::uint8_t> data) noexcept {
        for (const std::uint8_t b : data) {
            push(b);
        }
    }
    // The digest; the object is spent.
    [[nodiscard]] constexpr Bytes finish() noexcept {
        const std::uint64_t bits = count_ * 8U;
        push(0x80U);
        while (used_ != detail::block - 8U) {
            push(0U);
        }
        for (unsigned shift = 64U; shift > 0U; shift -= 8U) {
            push(static_cast<std::uint8_t>((bits >> (shift - 8U)) & 0xFFU));
        }
        Bytes out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = static_cast<std::uint8_t>((h_[i / 4U] >> (24U - 8U * (i % 4U))) & 0xFFU);
        }
        return out;
    }

private:
    constexpr void compress() noexcept {
        using detail::rotr;
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            for (std::size_t b = 0; b < 4; ++b) {
                w[i] = (w[i] << 8U) | block_[i * 4U + b];
            }
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3U);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::array<std::uint32_t, 8> v = h_;
        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + ch + detail::round_k[i] + w[i];
            const std::uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const std::uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            v = {t1 + s0 + maj, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (std::size_t i = 0; i < 8; ++i) {
            h_[i] += v[i];
        }
    }

    std::array<std::uint32_t, 8> h_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::uint64_t count_ = 0;
    std::size_t used_ = 0;
    detail::Block block_{};
};

constexpr Bytes sha256(std::string_view data) noexcept {
    Sha256 s;
    s.update(data);
    return s.finish();
}

namespace detail {

// HMAC over a key already padded to a block, or hashed first. Its two
// states, each derived from the key, are given to spent, inner then outer,
// before the digest is returned.
template <typename Spent>
constexpr Bytes hmac_block(const Block& key, std::string_view data, const Spent& spent) {
    Sha256 inner;
    for (const std::uint8_t b : key) {
        inner.push(static_cast<std::uint8_t>(b ^ 0x36U));
    }
    inner.update(data);
    const Bytes first = inner.finish();
    Sha256 outer;
    for (const std::uint8_t b : key) {
        outer.push(static_cast<std::uint8_t>(b ^ 0x5cU));
    }
    outer.update(first);
    const Bytes digest = outer.finish();
    spent(inner, outer);
    return digest;
}

}  // namespace detail

// A digest's text: the prefix and the 64 hex digits, into out, replaced
// only on success: one allocation.
template <typename S>
constexpr void write_hex(std::string_view prefix, const Bytes& digest, S& out) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::array<char, 2 * bytes> hex{};
    for (std::size_t i = 0; i < bytes; ++i) {
        hex[2 * i] = digits[digest[i] >> 4U];
        hex[2 * i + 1] = digits[digest[i] & 0x0FU];
    }
    S text(out.get_allocator());
    text.reserve(prefix.size() + hex.size());
    text.append(prefix);
    text.append(std::string_view(hex.data(), hex.size()));
    out = std::move(text);
}

// HMAC-SHA-256 under a key of any length, the RFC's vectors' form; keyed
// takes a 32-byte key and a wipe.
constexpr Bytes hmac(std::string_view key, std::string_view data) noexcept {
    detail::Block padded{};
    if (key.size() > detail::block) {
        const Bytes hashed = sha256(key);
        for (std::size_t i = 0; i < hashed.size(); ++i) {
            padded[i] = hashed[i];
        }
    } else {
        for (std::size_t i = 0; i < key.size(); ++i) {
            padded[i] = static_cast<std::uint8_t>(key[i]);
        }
    }
    return detail::hmac_block(padded, data, [](const Sha256&, const Sha256&) noexcept {});
}

// The plain digest of data: sha256: and its hex, into out.
template <typename S>
constexpr void plain(std::string_view data, S& out) {
    write_hex("sha256:", sha256(data), out);
}

// The keyed digest of data under a 32-byte key: hmac-sha256: and its hex,
// into out. The HMAC's two key-derived states and the padded key's block,
// all on the stack, are wiped by wipe, the caller's, in that order, before
// the text is written: three calls. The key's own copy is the caller's.
template <typename S, std::invocable<std::span<std::byte>> Wipe>
void keyed(std::span<const std::byte, bytes> key, std::string_view data, S& out, const Wipe& wipe) {
    detail::Block padded{};
    for (std::size_t i = 0; i < bytes; ++i) {
        padded[i] = std::to_integer<std::uint8_t>(key[i]);
    }
    const Bytes digest = detail::hmac_block(padded, data, [&wipe](Sha256& inner, Sha256& outer) {
        wipe(std::as_writable_bytes(std::span<Sha256, 1>(&inner, 1)));
        wipe(std::as_writable_bytes(std::span<Sha256, 1>(&outer, 1)));
    });
    wipe(std::as_writable_bytes(std::span<std::uint8_t>(padded)));
    write_hex("hmac-sha256:", digest, out);
}

// Whether a and b are equal, every byte pair compared, with no early exit;
// texts of unequal lengths are unequal.
bool same(std::string_view a, std::string_view b) noexcept;

}  // namespace qiven::digest
