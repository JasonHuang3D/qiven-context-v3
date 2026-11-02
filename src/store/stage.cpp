#include "store/stage.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "qiven/memory/memory.hpp"
#include "store/contract.hpp"
#include "store/errors.hpp"

namespace qiven3::store::stage {

void refuse(const AtView& at, std::size_t index, std::string_view id, std::string_view code, std::string_view field,
            std::string_view pattern) {
    throw StoreError::entry_invalid(name(at.op), at.path, index, id, code, field, pattern);
}

void refuse_full(const AtView& at, std::uint64_t size, std::uint64_t count, const Limits& limits) {
    throw StoreError::full(name(at.op), at.path, Full::store_full, size, count, limits.max_size, limits.max_count);
}

void refuse_kind(const AtView& at, std::size_t index) {
    std::array<char, 32> detail{'e', 'n', 't', 'r', 'y', ' '};
    const std::span<char> digits = std::span(detail).subspan<6>();
    const std::to_chars_result r = std::to_chars(digits.data(), std::to_address(digits.end()), index);
    throw StoreError(Code::forget_kind, name(at.op), at.path,
                     std::string_view(detail.data(), static_cast<std::size_t>(r.ptr - detail.data())));
}

void too_large(std::size_t entries) {
    throw qiven::memory::MemoryError::budget("stage", max_call_entries, entries, 0);
}

}  // namespace qiven3::store::stage
