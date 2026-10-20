#include "memory/memory.hpp"

namespace qiven3::memory {

namespace {

// Appends text at `at`, keeping what fits.
std::size_t put(std::array<char, 96>& out, std::size_t at, std::string_view text) noexcept {
    for (const char c : text) {
        if (at == out.size()) {
            break;
        }
        out[at] = c;
        ++at;
    }
    return at;
}

std::size_t put(std::array<char, 96>& out, std::size_t at, std::size_t value) noexcept {
    // The decimal digits, last first.
    std::array<char, 20> digits{};
    std::size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + value % 10);
        value /= 10;
        ++count;
    } while (value != 0);
    while (count > 0) {
        --count;
        at = put(out, at, std::string_view(&digits[count], 1));
    }
    return at;
}

}  // namespace

std::string_view name(MemoryCode code) noexcept {
    switch (code) {
    case MemoryCode::budget:
        return "budget";
    }
    return "budget";
}

MemoryError MemoryError::budget(std::string_view operation, std::size_t budget,
                                std::size_t request, std::size_t held) noexcept {
    std::array<char, 96> text{};
    std::size_t at = put(text, 0, "budget ");
    at = put(text, at, budget);
    at = put(text, at, ", request ");
    at = put(text, at, request);
    at = put(text, at, ", held ");
    at = put(text, at, held);
    return MemoryError(MemoryCode::budget, operation, std::string_view(text.data(), at));
}

}  // namespace qiven3::memory
