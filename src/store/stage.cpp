#include "store/stage.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "entry/check.hpp"
#include "memory/memory.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::stage {

void refuse(const AtView& at, std::size_t index, std::string_view id, std::string_view code, std::string_view field,
            std::string_view pattern) {
    throw StoreError::entry_invalid(name(at.op), at.path, index, id, code, field, pattern);
}

void refuse_part1(const AtView& at, std::size_t index, std::string_view id, const entry::check::Fault& fault) {
    const bool content = fault.part == entry::check::Part::content;
    refuse(at, index, id, entry::check::name(fault), content ? fault.error.path.view() : std::string_view());
}

void refuse_full(const AtView& at, std::uint64_t size, std::uint64_t count, const Limits& limits) {
    throw StoreError::full(name(at.op), at.path, Full::store_full, size, count, limits.max_size, limits.max_count);
}

void refuse_kind(const AtView& at, std::size_t index) {
    std::array<char, 32> detail{'e', 'n', 't', 'r', 'y', ' '};
    const std::to_chars_result r = std::to_chars(detail.data() + 6, detail.data() + detail.size(), index);
    throw StoreError(Code::forget_kind, name(at.op), at.path,
                     std::string_view(detail.data(), static_cast<std::size_t>(r.ptr - detail.data())));
}

void too_large(std::size_t entries) {
    throw memory::MemoryError::budget("stage", max_call_entries, entries, 0);
}

}  // namespace qiven3::store::stage
