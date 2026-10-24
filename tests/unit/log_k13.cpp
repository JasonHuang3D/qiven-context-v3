// K13's binary half: an event at trace and its control at info, each with
// its own name, which tests/drive/test_log.py looks for in this binary's
// bytes: the trace name present only when the build's minimum is trace, the
// control's always. Neither name is written anywhere else in the binary's
// sources, and nothing but the events reads them.

#include <array>
#include <cstddef>
#include <string_view>

#include "log/log.hpp"
#include "memory/arena.hpp"
#include "runner/runner.hpp"
#include "support/log/cases.hpp"

namespace {

namespace qlog = qiven3::log;

struct TraceOnly {
    static constexpr qlog::Level level = qlog::Level::trace;
    static constexpr std::string_view name = "k13.trace-marker";
    static constexpr std::array<std::string_view, 1> keys{"n"};
    using fields = qlog::Types<qlog::Count>;
};

struct InfoControl {
    static constexpr qlog::Level level = qlog::Level::info;
    static constexpr std::string_view name = "k13.info-control";
    static constexpr std::array<std::string_view, 1> keys{"n"};
    using fields = qlog::Types<qlog::Count>;
};

}  // namespace

QIVEN3_CASE(log, k13_levels) {
    qiven3::memory::Arena& arena = qiven3_ctx.counted.arena(qlog::Log::ring_bytes + qlog::Log::event_bytes);
    const std::size_t lines = [&] {
        qlog::Log lg(arena, qlog::Count(1U), "qiven3_unit_log");
        qlog::event(lg, TraceOnly{}, qlog::Count(1U));
        qlog::event(lg, InfoControl{}, qlog::Count(2U));
        return qiven3::test::Lines(lg.ring().lines()).size();
    }();
    // log.start, the control, and the trace event where the minimum takes it.
    CHECK_EQ(lines, qlog::minimum == qlog::Level::trace ? 3U : 2U);
}
