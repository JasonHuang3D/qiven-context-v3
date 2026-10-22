#pragma once

// digest: framing, the id, plain and keyed digests, and keys. SHA-256 is
// FIPS 180-4's and HMAC-SHA-256 RFC 2104's, written here with no
// intrinsics, a key longer than a block hashed first. A framed field is an
// 8-byte big-endian length and its bytes, an absent one the length
// ffffffffffffffff alone; the id is sha256: and the 64 lowercase hex digits
// of SHA-256 over the framed fields, which entry_id hashes as it frames,
// holding no copy. plain writes sha256: and keyed hmac-sha256: with the
// digest's hex, each into the caller's string, replaced only on success:
// one allocation, the result's. The functions but keyed's and same are
// constexpr and templates on the string type, so their vectors run in
// constant evaluation too; they read by index, with no pointer arithmetic.
// same compares in a time that does not depend on where two texts differ.
//
// Key holds 32 bytes of key material in no arena, beside the platform it
// came from: its destructor and its moves wipe them by the platform's
// wipe, and keyed alone reads them. It has no stream operator, formatter
// or accessor, so no log or message can hold it, but for the body
// record's key alone: key_hex writes its 64 lowercase hex digits into the
// caller's buffer, and key_from_hex reads them back, its decoded copy on the
// stack wiped. new_key draws one from the platform's random_bytes.
//
// thread: none; a Key, one (its platform's).

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "memory/fail.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"

namespace qiven3::digest {

inline constexpr std::size_t bytes = 32;  // a digest's, and a key's
using Bytes = std::array<std::uint8_t, bytes>;

// A header's field: its bytes, or none when absent.
using Field = std::optional<std::string_view>;

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

// HMAC over a key already padded to a block, or hashed first.
constexpr Bytes hmac_block(const Block& key, std::string_view data) noexcept {
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
    return outer.finish();
}

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

// Each byte of the framed fields, in order, to put.
template <typename Put>
constexpr void each_framed(std::span<const Field> fields, const Put& put) {
    for (const Field& field : fields) {
        const std::uint64_t length = field ? static_cast<std::uint64_t>(field->size())
                                           : std::numeric_limits<std::uint64_t>::max();
        for (unsigned shift = 64U; shift > 0U; shift -= 8U) {
            put(static_cast<std::uint8_t>((length >> (shift - 8U)) & 0xFFU));
        }
        if (field) {
            for (const char c : *field) {
                put(static_cast<std::uint8_t>(c));
            }
        }
    }
}

[[noreturn]] void too_large(std::size_t held);

}  // namespace detail

// HMAC-SHA-256 under a key of any length, the RFC's vectors' form; keyed
// takes a Key.
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
    return detail::hmac_block(padded, data);
}

// The framed fields' size, checked: a sum past std::size_t is MemoryError
// budget.
constexpr std::size_t framed_size(std::span<const Field> fields) {
    std::size_t total = 0;
    for (const Field& field : fields) {
        std::optional<std::size_t> next = memory::add(total, 8U);
        if (next && field) {
            next = memory::add(*next, field->size());
        }
        if (!next) {
            detail::too_large(total);
        }
        total = *next;
    }
    return total;
}

// The framed fields' bytes into out, replaced only on success.
template <typename S>
constexpr void frame(std::span<const Field> fields, S& out) {
    S framed(out.get_allocator());
    framed.reserve(framed_size(fields));
    detail::each_framed(fields, [&framed](std::uint8_t b) { framed.push_back(static_cast<char>(b)); });
    out = std::move(framed);
}

// The id over the framed fields: sha256: and its hex, into out.
template <typename S>
constexpr void entry_id(std::span<const Field> fields, S& out) {
    Sha256 s;
    detail::each_framed(fields, [&s](std::uint8_t b) { s.push(b); });
    detail::write_hex("sha256:", s.finish(), out);
}

// The plain digest of data: sha256: and its hex, into out.
template <typename S>
constexpr void plain(std::string_view data, S& out) {
    detail::write_hex("sha256:", sha256(data), out);
}

// Whether a and b are equal, every byte pair compared, with no early exit;
// texts of unequal lengths are unequal.
bool same(std::string_view a, std::string_view b) noexcept;

// 32 bytes of key material and the platform that wipes them.
template <platform::Platform P>
class Key {
public:
    // A fixed key: the fixtures', or one read back from its record. The
    // caller's copy is the caller's to wipe.
    Key(P& platform, std::span<const std::byte, bytes> material) noexcept : platform_(&platform) {
        for (std::size_t i = 0; i < bytes; ++i) {
            material_[i] = material[i];
        }
    }
    Key(Key&& other) noexcept : platform_(other.platform_), material_(other.material_) { other.wipe(); }
    Key& operator=(Key&& other) noexcept {
        if (this != &other) {
            wipe();
            platform_ = other.platform_;
            material_ = other.material_;
            other.wipe();
        }
        return *this;
    }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    ~Key() {
        // The size is pinned for each platform it is made for, here where
        // the class is complete: the platform's pointer and the material,
        // 40 bytes on a 64-bit target.
        static_assert(sizeof(Key) == sizeof(P*) + bytes, "Key's size is pinned: the pointer and 32 bytes");
        wipe();
    }

private:
    template <platform::Platform Q, typename S>
    friend void keyed(const Key<Q>& key, std::string_view data, S& out);
    template <platform::Platform Q>
    friend void key_hex(const Key<Q>& key, std::span<char, 2 * bytes> out) noexcept;

    void wipe() noexcept { platform_->wipe(std::span<std::byte>(material_)); }

    P* platform_;
    std::array<std::byte, bytes> material_{};
};

// The keyed digest of data under key: hmac-sha256: and its hex, into out.
// The padded key's block on the stack is wiped by the key's platform.
template <platform::Platform P, typename S>
void keyed(const Key<P>& key, std::string_view data, S& out) {
    detail::Block padded{};
    for (std::size_t i = 0; i < bytes; ++i) {
        padded[i] = std::to_integer<std::uint8_t>(key.material_[i]);
    }
    const Bytes digest = detail::hmac_block(padded, data);
    key.platform_->wipe(std::as_writable_bytes(std::span<std::uint8_t>(padded)));
    detail::write_hex("hmac-sha256:", digest, out);
}

// Whether hex is a key's text: 64 lowercase hex digits.
constexpr bool key_text(std::string_view hex) noexcept {
    if (hex.size() != 2 * bytes) {
        return false;
    }
    for (const char c : hex) {
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
            return false;
        }
    }
    return true;
}

// The key's 64 lowercase hex digits into out, the body record's key; no
// allocation. The caller's buffer is the caller's to wipe.
template <platform::Platform P>
void key_hex(const Key<P>& key, std::span<char, 2 * bytes> out) noexcept {
    constexpr std::string_view digits = "0123456789abcdef";
    for (std::size_t i = 0; i < bytes; ++i) {
        const std::uint8_t b = std::to_integer<std::uint8_t>(key.material_[i]);
        out[2 * i] = digits[b >> 4U];
        out[2 * i + 1] = digits[b & 0x0FU];
    }
}

// A key read back from its record's hex, which the caller has checked by
// key_text; text that is not a key's is a broken invariant, fail_fast. The
// decoded copy on the stack is wiped by the platform after: one call.
template <platform::Platform P>
Key<P> key_from_hex(P& platform, std::string_view hex) noexcept {
    if (!key_text(hex)) {
        memory::fail_fast("digest: key_from_hex given text that is no key's");
    }
    const auto nibble = [](char c) noexcept {
        return static_cast<unsigned>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    // The copy, wiped as the call returns, after the key is built in place.
    struct Decoded {
        explicit Decoded(P& p) noexcept : platform(&p) {}
        Decoded(const Decoded&) = delete;
        Decoded& operator=(const Decoded&) = delete;
        Decoded(Decoded&&) = delete;
        Decoded& operator=(Decoded&&) = delete;
        ~Decoded() { platform->wipe(std::span<std::byte>(material)); }
        P* platform;
        std::array<std::byte, bytes> material{};
    };
    Decoded decoded(platform);
    for (std::size_t i = 0; i < bytes; ++i) {
        decoded.material[i] = static_cast<std::byte>((nibble(hex[2 * i]) << 4U) | nibble(hex[2 * i + 1]));
    }
    return Key<P>(platform, std::span<const std::byte, bytes>(decoded.material));
}

// A key drawn from the platform's random_bytes, the drawn copy wiped; a
// refused draw is the platform's error, with the partial bytes wiped.
template <platform::Platform P>
Key<P> new_key(P& platform) {
    std::array<std::byte, bytes> drawn{};
    try {
        platform.random_bytes(std::span<std::byte>(drawn));
    } catch (...) {
        platform.wipe(std::span<std::byte>(drawn));
        throw;
    }
    Key<P> key(platform, std::span<const std::byte, bytes>(drawn));
    platform.wipe(std::span<std::byte>(drawn));
    return key;
}

}  // namespace qiven3::digest
