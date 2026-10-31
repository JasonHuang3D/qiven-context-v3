#include "platform/platform.hpp"

namespace qiven3::platform {

std::string_view name(Code code) noexcept {
    switch (code) {
    case Code::unsupported:
        return "unsupported";
    case Code::config:
        return "config";
    case Code::path:
        return "path";
    case Code::exists:
        return "exists";
    case Code::busy:
        return "busy";
    case Code::io:
        return "io";
    case Code::start:
        return "start";
    case Code::deadline:
        return "deadline";
    case Code::output:
        return "output";
    case Code::memory:
        return "memory";
    }
    return "unknown";
}

Detail& Detail::add(std::string_view text) noexcept {
    for (const char c : text) {
        if (size_ == bytes_.size()) {
            break;
        }
        bytes_[size_++] = c;
    }
    return *this;
}

Detail& Detail::number(std::uint64_t value) noexcept {
    std::array<char, 20> digits{};
    std::size_t n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (n > 0) {
        add(std::string_view(&digits[--n], 1));
    }
    return *this;
}

Detail& Detail::hex(std::uint32_t value) noexcept {
    static constexpr std::string_view digits = "0123456789abcdef";
    add("0x");
    for (unsigned shift = 32U; shift > 0U; shift -= 4U) {
        add(digits.substr((value >> (shift - 4U)) & 0xFU, 1));
    }
    return *this;
}

}  // namespace qiven3::platform
