#pragma once

// entry_id: an entry's framing and its id. A framed field is an 8-byte
// big-endian length and its bytes, an absent one the length
// ffffffffffffffff alone; the id is sha256: and the 64 lowercase hex digits
// of digest's SHA-256 over the framed fields, which entry_id hashes as it
// frames, holding no copy, into the caller's string, replaced only on
// success: one allocation, the result's. The functions are constexpr and
// templates on the string type, so their vectors run in constant evaluation
// too; they read by index, with no pointer arithmetic.
//
// thread: none.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "qiven/digest/digest.hpp"
#include "qiven/memory/memory.hpp"

namespace qiven3::format {

// A header's field: its bytes, or none when absent.
using Field = std::optional<std::string_view>;

namespace detail {

// Each byte of the framed fields, in order, to put.
template <typename Put>
constexpr void each_framed(std::span<const Field> fields, const Put& put) {
    for (const Field& field : fields) {
        // The field's bytes, held once: none for an absent field.
        const std::string_view held = field.value_or(std::string_view());
        const std::uint64_t length = field ? static_cast<std::uint64_t>(held.size())
                                           : std::numeric_limits<std::uint64_t>::max();
        for (unsigned shift = 64U; shift > 0U; shift -= 8U) {
            put(static_cast<std::uint8_t>((length >> (shift - 8U)) & 0xFFU));
        }
        for (const char c : held) {
            put(static_cast<std::uint8_t>(c));
        }
    }
}

[[noreturn]] void too_large(std::size_t held);

}  // namespace detail

// The framed fields' size, checked: a sum past std::size_t is MemoryError
// budget.
constexpr std::size_t framed_size(std::span<const Field> fields) {
    std::size_t total = 0;
    for (const Field& field : fields) {
        std::optional<std::size_t> next = qiven::memory::add(total, 8U);
        if (next && field) {
            next = qiven::memory::add(*next, field->size());
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
    qiven::digest::Sha256 s;
    detail::each_framed(fields, [&s](std::uint8_t b) { s.push(b); });
    qiven::digest::write_hex("sha256:", s.finish(), out);
}

}  // namespace qiven3::format
