// events' unit for the analysis preset: the report's events read with a
// head and a line callable of the kinds the report gives them.

#include "diag/events.hpp"

#include <cstdint>
#include <string_view>

namespace qiven3::diag::detail {

std::uint64_t counted_events(const Rings& rings) noexcept {
    std::uint64_t lines = 0;
    std::uint64_t points = 0;
    events(
        rings, [&lines](std::uint64_t n) noexcept { lines = n; },
        [&points](std::string_view line) noexcept { points += line.size(); });
    return lines + points;
}

}  // namespace qiven3::diag::detail
