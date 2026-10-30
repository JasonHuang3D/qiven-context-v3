#include "checked/rules.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "store/contract.hpp"
#include "store/errors.hpp"
#include "store/stage.hpp"

namespace qiven3::checked {

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

void refuse_kind(const AtView& at, std::size_t index) {
    std::array<char, 32> detail{};
    throw store::StoreError(store::Code::forget_kind, store::name(at.op), at.path, naming(index, detail));
}

void same_heads(std::span<const LaneHead> source, std::span<const LaneHead> given, const AtView& at) {
    const std::size_t n = source.size() < given.size() ? source.size() : given.size();
    for (std::size_t i = 0; i < n; ++i) {
        const LaneHead& s = source[i];
        const LaneHead& g = given[i];
        if (s.scope.view() != g.scope.view() || s.lane.view() != g.lane.view() || s.head.id != g.head.id ||
            s.head.seq != g.head.seq) {
            stage::refuse(at, i, s.head.id.view(), "head_mismatch");
        }
    }
    if (source.size() != given.size()) {
        stage::refuse(at, n, n < source.size() ? source[n].head.id.view() : std::string_view(), "head_mismatch");
    }
}

}  // namespace qiven3::checked
