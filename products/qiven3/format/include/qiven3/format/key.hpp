#pragma once

// key: Key holds 32 bytes of key material in no arena, beside the platform
// it came from: its destructor and its moves wipe them by the platform's
// wipe, and keyed alone reads them, through digest's keyed with the
// platform's wipe for the padded key's block. It has no stream operator,
// formatter or accessor, so no log or message can hold it, but for its
// hex: key_hex writes its 64 lowercase hex digits into the caller's buffer,
// and key_from_hex reads them back, its decoded copy on the stack wiped.
// new_key draws one from the platform's random_bytes. The platform's
// contract is included here alone, so a caller of the id takes no platform.
//
// thread: a Key, one (its platform's).

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "qiven/digest/digest.hpp"
#include "qiven/memory/fail.hpp"
#include "qiven/platform/platform.hpp"

namespace qiven3::format {

// 32 bytes of key material and the platform that wipes them.
template <qiven::platform::Platform P>
class Key {
public:
    // A fixed key: the fixtures', or one read back from its record. The
    // caller's copy is the caller's to wipe.
    Key(P& platform, std::span<const std::byte, qiven::digest::bytes> material) noexcept : platform_(&platform) {
        for (std::size_t i = 0; i < qiven::digest::bytes; ++i) {
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
        static_assert(sizeof(Key) == sizeof(P*) + qiven::digest::bytes,
                      "Key's size is pinned: the pointer and 32 bytes");
        wipe();
    }

private:
    template <qiven::platform::Platform Q, typename S>
    friend void keyed(const Key<Q>& key, std::string_view data, S& out);
    template <qiven::platform::Platform Q>
    friend void key_hex(const Key<Q>& key, std::span<char, 2 * qiven::digest::bytes> out) noexcept;

    void wipe() noexcept { platform_->wipe(std::span<std::byte>(material_)); }

    P* platform_;
    std::array<std::byte, qiven::digest::bytes> material_{};
};

// The keyed digest of data under key: hmac-sha256: and its hex, into out.
// The padded key's block on the stack is wiped by the key's platform.
template <qiven::platform::Platform P, typename S>
void keyed(const Key<P>& key, std::string_view data, S& out) {
    P* const platform = key.platform_;
    qiven::digest::keyed(std::span<const std::byte, qiven::digest::bytes>(key.material_), data, out,
                         [platform](std::span<std::byte> block) noexcept { platform->wipe(block); });
}

// Whether hex is a key's text: 64 lowercase hex digits.
constexpr bool key_text(std::string_view hex) noexcept {
    if (hex.size() != 2 * qiven::digest::bytes) {
        return false;
    }
    for (const char c : hex) {
        if ((c < '0' || c > '9') && (c < 'a' || c > 'f')) {
            return false;
        }
    }
    return true;
}

// The key's 64 lowercase hex digits into out; no allocation. The caller's
// buffer is the caller's to wipe.
template <qiven::platform::Platform P>
void key_hex(const Key<P>& key, std::span<char, 2 * qiven::digest::bytes> out) noexcept {
    constexpr std::string_view digits = "0123456789abcdef";
    for (std::size_t i = 0; i < qiven::digest::bytes; ++i) {
        const std::uint8_t b = std::to_integer<std::uint8_t>(key.material_[i]);
        out[2 * i] = digits[b >> 4U];
        out[2 * i + 1] = digits[b & 0x0FU];
    }
}

// A key read back from its record's hex, which the caller has checked by
// key_text; text that is not a key's is a broken invariant, fail_fast. The
// decoded copy on the stack is wiped by the platform after: one call.
template <qiven::platform::Platform P>
Key<P> key_from_hex(P& platform, std::string_view hex) noexcept {
    if (!key_text(hex)) {
        qiven::memory::fail_fast("format: key_from_hex given text that is no key's");
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
        std::array<std::byte, qiven::digest::bytes> material{};
    };
    Decoded decoded(platform);
    for (std::size_t i = 0; i < qiven::digest::bytes; ++i) {
        decoded.material[i] = static_cast<std::byte>((nibble(hex[2 * i]) << 4U) | nibble(hex[2 * i + 1]));
    }
    return Key<P>(platform, std::span<const std::byte, qiven::digest::bytes>(decoded.material));
}

// A key drawn from the platform's random_bytes, the drawn copy wiped; a
// refused draw is the platform's error, with the partial bytes wiped.
template <qiven::platform::Platform P>
Key<P> new_key(P& platform) {
    std::array<std::byte, qiven::digest::bytes> drawn{};
    try {
        platform.random_bytes(std::span<std::byte>(drawn));
    } catch (...) {
        platform.wipe(std::span<std::byte>(drawn));
        throw;
    }
    Key<P> key(platform, std::span<const std::byte, qiven::digest::bytes>(drawn));
    platform.wipe(std::span<std::byte>(drawn));
    return key;
}

}  // namespace qiven3::format
