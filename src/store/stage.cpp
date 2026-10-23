#include "store/stage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "entry/check.hpp"
#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::stage {

namespace {

// "entry <index>" into out; the view is out's.
std::string_view naming(std::size_t index, std::array<char, 32>& out) noexcept {
    constexpr std::string_view head = "entry ";
    std::size_t n = 0;
    for (const char c : head) {
        out.at(n++) = c;
    }
    std::array<char, 20> digits{};
    std::size_t d = 0;
    do {
        digits.at(d++) = static_cast<char>('0' + (index % 10));
        index /= 10;
    } while (index != 0);
    while (d > 0) {
        out.at(n++) = digits.at(--d);
    }
    return {out.data(), n};
}

}  // namespace

void refuse(const AtView& at, std::size_t index, std::string_view id, std::string_view code, std::string_view field,
            std::string_view pattern) {
    throw StoreError::entry_invalid(name(at.op), at.path, index, id, code, field, pattern);
}

void refuse_part1(const AtView& at, std::size_t index, std::string_view id, const entry::check::Fault& fault) {
    const bool content = fault.part == entry::check::Part::content;
    refuse(at, index, id, entry::check::name(fault), content ? fault.error.path.view() : std::string_view());
}

void refuse_kind(const AtView& at, std::size_t index) {
    std::array<char, 32> detail{};
    throw StoreError(Code::forget_kind, name(at.op), at.path, naming(index, detail));
}

void refuse_full(const AtView& at, std::uint64_t size, std::uint64_t count, const Limits& limits) {
    throw StoreError::full(name(at.op), at.path, Full::store_full, size, count, limits.max_size, limits.max_count);
}

void too_large(std::size_t entries) {
    throw memory::MemoryError::budget("stage", validator::max_entries, entries, 0);
}

void same_heads(std::span<const LaneHead> source, std::span<const LaneHead> given, const AtView& at) {
    const std::size_t n = source.size() < given.size() ? source.size() : given.size();
    for (std::size_t i = 0; i < n; ++i) {
        const LaneHead& s = source[i];
        const LaneHead& g = given[i];
        if (s.scope.view() != g.scope.view() || s.lane.view() != g.lane.view() || s.head.id != g.head.id ||
            s.head.seq != g.head.seq) {
            refuse(at, i, s.head.id.view(), "head_mismatch");
        }
    }
    if (source.size() != given.size()) {
        refuse(at, n, n < source.size() ? source[n].head.id.view() : std::string_view(), "head_mismatch");
    }
}

}  // namespace qiven3::store::stage
