#include "checked/rules.hpp"

#include <cstddef>
#include <span>
#include <string_view>

#include "store/contract.hpp"
#include "store/stage.hpp"

namespace qiven3::checked {

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
