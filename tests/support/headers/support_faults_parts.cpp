// parts' unit for the analysis preset: a table of three parts over two
// passes, its ranges, the registry's check, and, under QIVEN3_FAULTS, a
// pass's runs within a range over a typed error of the unit's own, as the
// cases give them.

#include "support/faults/parts.hpp"

#include <cstddef>

#include "log/log.hpp"
#include "memory/memory.hpp"

namespace qiven3::test {

namespace {
constexpr FaultParts<3> parts{{{{0, 1}, {0, 41}, {1, 1}}}};
static_assert(parts.whole());
static_assert(parts.range(0, 0).from == 1 && parts.range(0, 0).to == 40 && parts.range(0, 1).none());
static_assert(parts.range(1, 0).from == 41 && parts.range(1, 1).none() && parts.range(2, 0).none());
static_assert(parts.range(2, 1).from == 1 && parts.range(2, 1).to == SIZE_MAX);
}  // namespace

bool parts_registered() { return registered("unit.parts", parts.from.size()); }

#if defined(QIVEN3_FAULTS)
namespace {
struct Refused {};
}  // namespace

bool parts_run(log::Log& log, log::Name site, memory::Countdown& countdown, std::size_t& calls) {
    PartRuns part(parts.range(1, 0));
    const FaultRuns runs = run_faults<Refused>(
        log, site, countdown,
        [&] {
            part.enter<Refused>(countdown);
            ++calls;
        },
        [&](std::size_t k) { return part.skipped() || calls >= k; });
    return part.held(runs) && part.completed_here(runs) && part.executed() > part.before();
}
#endif

}  // namespace qiven3::test
