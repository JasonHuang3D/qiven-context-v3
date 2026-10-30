#pragma once

// keys: what the store orders and finds a record by, each its caller's.
// Id is an id by value, Head a lane's head, KeysView a record's keys as a
// caller gives them (views, valid while the caller's bytes are), and
// in_scopes whether a scope set holds a scope. The store keeps the keys as
// given and reads no rule of a product's into them.
//
// A scope set's entry ending in `*` holds every scope its bytes before the
// `*` begin, `*` alone every scope; any other entry holds the scope it
// equals. A scope holding `*` is a scope like any other.
//
// Allocations: none. Platform calls: none. thread: none.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace qiven3::store {

// An id by value: up to 71 bytes, the length of sha256: and 64 hex. of()
// keeps a longer text to its first 71; a caller whose text may be longer
// asks fits() first, as the store does of every key it is given.
struct Id {
    static constexpr std::size_t capacity = 71;
    std::array<char, capacity> bytes{};
    std::uint8_t size = 0;

    [[nodiscard]] static constexpr bool fits(std::string_view text) noexcept { return text.size() <= capacity; }
    static constexpr Id of(std::string_view text) noexcept {
        Id id;
        const std::size_t n = std::min(text.size(), capacity);
        for (std::size_t i = 0; i < n; ++i) {
            id.bytes.at(i) = text[i];
        }
        id.size = static_cast<std::uint8_t>(n);
        return id;
    }
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(bytes.data(), size);
    }
    constexpr bool operator==(const Id&) const = default;
};
static_assert(sizeof(Id) == 72, "Id's size is pinned: 71 characters and their count");

// A lane's head: its id and seq.
struct Head {
    Id id;
    std::int64_t seq = 0;
};
static_assert(sizeof(Head) == 80, "Head's size is pinned: an Id and seq");

// The most entries one call takes.
inline constexpr std::size_t max_call_entries = UINT32_MAX - 1U;

// A record's keys: its id, scope, lane, kind and seq, its parent and its
// target if it has them, and whether it has a body, held or forgotten.
struct KeysView {
    std::string_view id, scope, lane, kind;
    std::int64_t seq = 0;
    std::optional<std::string_view> parent, target;
    bool body = false;
};

// A scope set, an entry ending in `*` matching by prefix; none for the
// whole store.
using Scopes = std::optional<std::span<const std::string_view>>;

// Whether one entry of a scope set holds scope.
[[nodiscard]] constexpr bool in_scope(std::string_view entry, std::string_view scope) noexcept {
    if (entry.ends_with('*')) {
        return scope.starts_with(entry.substr(0, entry.size() - 1));
    }
    return entry == scope;
}

// Whether a scope set holds scope: none holds every scope.
[[nodiscard]] constexpr bool in_scopes(const Scopes& scopes, std::string_view scope) noexcept {
    return !scopes || std::ranges::any_of(*scopes, [scope](std::string_view s) { return in_scope(s, scope); });
}

}  // namespace qiven3::store
