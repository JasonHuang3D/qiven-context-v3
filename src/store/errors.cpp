#include "store/errors.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace qiven3::store {

namespace {

constexpr bool continuation(char c) noexcept { return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U; }

// Appends text and numbers to out; at the first byte that does not fit, the
// line ends, cut at a code point.
class LineView {
public:
    explicit LineView(std::span<char> out) noexcept : out_(out) {}
    LineView& add(std::string_view text) noexcept {
        for (const char c : text) {
            if (cut_ || size_ == out_.size()) {
                if (!cut_ && continuation(c)) {
                    while (size_ > 0 && continuation(out_[size_ - 1])) {
                        --size_;
                    }
                    if (size_ > 0) {
                        --size_;
                    }
                }
                cut_ = true;
                break;
            }
            out_[size_++] = c;
        }
        return *this;
    }
    LineView& number(std::uint64_t value) noexcept {
        std::array<char, 20> digits{};
        std::size_t n = 0;
        do {
            digits.at(n++) = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (n > 0) {
            add(std::string_view(&digits.at(--n), 1));
        }
        return *this;
    }
    LineView& id(std::string_view text) noexcept { return add(text.empty() ? std::string_view("none") : text); }
    [[nodiscard]] std::string_view view() const noexcept { return {out_.data(), size_}; }

private:
    std::span<char> out_;
    std::size_t size_ = 0;
    bool cut_ = false;
};

// Seconds to the millisecond, as "S.mmm s"; a negative or non-finite count
// as 0, a count past 2^53 ms held there.
void seconds(LineView& line, double value) noexcept {
    constexpr double most = 9007199254740991.0;
    const double ms = std::isfinite(value) && value > 0 ? std::fmin(std::round(value * 1000.0), most) : 0.0;
    const auto whole = static_cast<std::uint64_t>(ms);
    const std::uint64_t part = whole % 1000;
    line.number(whole / 1000).add(".");
    line.add(part < 100 ? "0" : "").add(part < 10 ? "0" : "").number(part).add(" s");
}

}  // namespace

std::string_view message(const StoreError& error, std::span<char> out) noexcept {
    LineView line(out);
    line.add(name(error.kind()));
    if (const auto c = error.code()) {
        line.add(" ").add(name(*c));
    } else if (const auto cause = error.cause()) {
        line.add(" ").add(name(*cause));
    } else if (const auto corrupt = error.corrupt()) {
        line.add(" ").add(name(*corrupt));
    } else if (const auto full = error.full()) {
        line.add(" ").add(name(*full));
    } else if (error.kind() == Kind::entry_invalid) {
        line.add(" ").add(error.entry_code());
    }
    line.add(": ").add(error.operation()).add(" ").add(error.path());
    switch (error.kind()) {
    case Kind::head_conflict:
        line.add("; scope ").add(error.scope()).add(", lane ").add(error.lane());
        line.add(", expected ").id(error.expected()).add(", actual ").id(error.actual());
        break;
    case Kind::entry_invalid:
        line.add("; entry ").number(error.index()).add(", id ").id(error.id()).add(", field ").add(error.field());
        if (!error.pattern().empty()) {
            line.add(", pattern ").add(error.pattern());
        }
        break;
    case Kind::not_found: line.add("; id ").add(error.id()); break;
    case Kind::busy: seconds(line.add("; waited "), error.seconds()); break;
    case Kind::full:
        line.add("; size ").number(error.size()).add(", count ").number(error.count());
        line.add(", maximum ").number(error.max_size()).add(" bytes, ").number(error.max_count()).add(" entries");
        break;
    case Kind::not_empty: line.add("; count ").number(error.count()); break;
    case Kind::store_error:
    case Kind::locked:
    case Kind::corrupt: break;
    }
    if (!error.detail().empty()) {
        line.add("; ").add(error.detail());
    }
    return line.view();
}

}  // namespace qiven3::store
