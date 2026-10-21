#include "diag/diag.hpp"

#include <exception>

#include "memory/fail.hpp"

namespace qiven3::diag::detail {

void Line::put(char c) noexcept {
    if (cut_) {
        return;
    }
    if (size_ == line_max) {
        // Back to the start of the code point the bound cuts.
        while (size_ > 0 && (static_cast<unsigned char>(bytes_[size_ - 1]) & 0xC0U) == 0x80U) {
            --size_;
        }
        if (size_ > 0 && (static_cast<unsigned char>(bytes_[size_ - 1]) & 0x80U) != 0) {
            --size_;
        }
        cut_ = true;
        return;
    }
    bytes_[size_] = c;
    ++size_;
}

Line& Line::text(std::string_view part) noexcept {
    for (char c : part) {
        put(c == '\n' || c == '\r' ? ' ' : c);
    }
    return *this;
}

Line& Line::wide(std::wstring_view part) noexcept {
    for (wchar_t c : part) {
        put(c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
    }
    return *this;
}

Line& Line::number(std::uint64_t value) noexcept {
    std::array<char, 20> digits{};
    std::size_t n = 0;
    do {
        digits[n] = static_cast<char>('0' + value % 10);
        ++n;
        value /= 10;
    } while (value != 0);
    while (n > 0) {
        --n;
        put(digits[n]);
    }
    return *this;
}

Line& Line::hex(std::uint64_t value, std::size_t digits) noexcept {
    constexpr std::string_view symbols = "0123456789ABCDEF";
    std::array<char, 16> out{};
    std::size_t n = 0;
    do {
        out[n] = symbols[value & 0xFU];
        ++n;
        value >>= 4U;
    } while (value != 0 || n < digits);
    text("0x");
    while (n > 0) {
        --n;
        put(out[n]);
    }
    return *this;
}

std::string_view Line::line() noexcept {
    bytes_[size_] = '\n';
    return {bytes_.data(), size_ + 1};
}

void on_terminate() {
    Line cause;
    const std::string_view kept = memory::fail_cause();
    if (!kept.empty()) {
        cause.text("fail-fast: ").text(kept);
        fatal(cause.view(), End::fail_fast);
    }
    if (const std::exception_ptr current = std::current_exception()) {
        cause.text("unhandled exception");
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            cause.text(": ").text(e.what());
        } catch (...) {
            cause.text(", not a std::exception");
        }
        fatal(cause.view(), End::abort);
    }
    cause.text("terminate");
    fatal(cause.view(), End::abort);
}

}  // namespace qiven3::diag::detail
