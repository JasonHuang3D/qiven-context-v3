// faults' unit for the analysis preset: the loop, which the header holds
// under QIVEN_FAULTS alone (this unit's own definition, for an object no
// binary links), run over a typed error of memory's, as the cases give it,
// with an operation and a check of the kinds they give.

#include "qiven/faults/faults.hpp"

#include <cstddef>

#include "qiven/log/log.hpp"
#include "qiven/memory/memory.hpp"

namespace qiven::test {

FaultRuns faults_run(qiven::log::Log& log, qiven::log::Name site, qiven::memory::Countdown& countdown, std::size_t& calls) {
    return run_faults<qiven::memory::MemoryError>(
        log, site, countdown, [&calls] { ++calls; }, [&calls](std::size_t k) { return calls >= k; });
}

}  // namespace qiven::test
