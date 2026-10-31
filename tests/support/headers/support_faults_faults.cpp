// faults' unit for the analysis preset: the loop, which the header holds
// under QIVEN_FAULTS alone (this unit's own definition, for an object no
// binary links), run over a typed error of memory's, as the cases give it,
// with an operation and a check of the kinds they give.

#include "support/faults/faults.hpp"

#include <cstddef>

#include "log/log.hpp"
#include "memory/memory.hpp"

namespace qiven3::test {

FaultRuns faults_run(log::Log& log, log::Name site, memory::Countdown& countdown, std::size_t& calls) {
    return run_faults<memory::MemoryError>(
        log, site, countdown, [&calls] { ++calls; }, [&calls](std::size_t k) { return calls >= k; });
}

}  // namespace qiven3::test
