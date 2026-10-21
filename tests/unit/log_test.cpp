// log's cases: the lines a Log writes, its operations, its drain, its
// budgets counted by the arena (the ring's 64 KiB and the events' 4 KiB
// exactly, an event's peak within its 4 KiB, a byte short refused), each
// allocation failed in turn, and the fields' types checked at compile time.

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "log/catalogue.hpp"
#include "log/line.hpp"
#include "log/log.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/log/cases.hpp"

namespace {

namespace qlog = qiven3::log;
namespace events = qiven3::log::events;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using qiven3::test::Lines;
using qiven3::test::Sink;

constexpr std::size_t log_budget = qlog::Log::ring_bytes + qlog::Log::event_bytes;

// The line's text after "t":, which the clock writes, cut out.
std::string untimed(std::string_view line) {
    const std::size_t t = line.find("\"t\":");
    if (t == std::string_view::npos) {
        return std::string(line);
    }
    std::size_t end = t + 4;
    while (end < line.size() && line[end] >= '0' && line[end] <= '9') {
        ++end;
    }
    return std::string(line.substr(0, t + 4)) + "T" + std::string(line.substr(end));
}

}  // namespace

QIVEN3_CASE(log, start) {
    Arena& arena = qiven3_ctx.counted.arena(log_budget);
    {
        const qlog::Log lg(arena, qlog::Count(7U), "qiven3_unit_log");
        // The ring's span and the events' pages, exactly, and no padding.
        CHECK_EQ(arena.used(), log_budget);
        CHECK_EQ(arena.peak(), log_budget);
        const Lines lines(lg.ring().lines());
        REQUIRE(lines.size() == 1);
        CHECK_EQ(untimed(lines.at(0)), std::string(R"({"binary":"qiven3_unit_log","event":"log.start","level":"info",)"
                                                   R"("log":7,"min":")") +
                                           std::string(qlog::level_name(qlog::minimum).text()) +
                                           R"(","op":0,"ring":65536,"t":T})");
        CHECK_EQ(lg.failed(), 0U);
        std::cout << "log.start: " << lines.at(0) << ", peak " << lg.peak() << "\n";
    }
    CHECK_EQ(arena.used(), 0U);
}

QIVEN3_CASE(log, operations) {
    Arena& arena = qiven3_ctx.counted.arena(log_budget);
    qlog::Log lg(arena, qlog::Count(1U), "qiven3_unit_log");
    {
        qlog::Op first = qlog::begin(lg, "first");
        CHECK_EQ(first.id(), 1U);
        qlog::event(first, events::error_typed, qlog::Name("MemoryError"), qlog::Name("budget"), qlog::Name("first"));
        first.end("ok", qlog::Count(123U));
        qlog::Op second = qlog::begin(lg, "second");
        qlog::Op moved(std::move(second));
        CHECK_EQ(moved.id(), 2U);
        // moved ends at the scope's end, not ended: dropped, peak 0.
    }
    const Lines lines(lg.ring().lines());
    std::vector<std::string> want;
    want.emplace_back(R"({"binary":"qiven3_unit_log","event":"log.start",)");
    if (events::OpBegin::level >= qlog::minimum) {
        want.emplace_back(R"({"event":"op.begin","level":"debug","log":1,"name":"first","op":1,"t":T})");
    }
    want.emplace_back(R"({"code":"budget","event":"error.typed","level":"error","log":1,"op":1,)"
                      R"("operation":"first","t":T,"type":"MemoryError"})");
    want.emplace_back(R"({"event":"op.end","level":"info","log":1,"ms":)");
    if (events::OpBegin::level >= qlog::minimum) {
        want.emplace_back(R"({"event":"op.begin","level":"debug","log":1,"name":"second","op":2,"t":T})");
    }
    want.emplace_back(R"({"event":"op.end","level":"info","log":1,"ms":)");
    REQUIRE(lines.size() == want.size());
    for (std::size_t i = 0; i < want.size(); ++i) {
        const std::string got = untimed(lines.at(i));
        CHECK(got.starts_with(want[i]));
    }
    const std::string last_first = lines.at(want.size() - 1 - (events::OpBegin::level >= qlog::minimum ? 2 : 1));
    CHECK(last_first.find(R"("name":"first","op":1,"outcome":"ok","peak":123,)") != std::string::npos);
    const std::string last = lines.at(want.size() - 1);
    CHECK(last.find(R"("name":"second","op":2,"outcome":"dropped","peak":0,)") != std::string::npos);
}

QIVEN3_CASE(log, drain) {
    Arena& arena = qiven3_ctx.counted.arena(log_budget);
    qlog::Log lg(arena, qlog::Count(2U), "qiven3_unit_log");
    Sink out;
    CHECK(lg.drain(out));
    CHECK_EQ(out.writes, 1);
    CHECK(out.bytes.starts_with(R"({"binary":"qiven3_unit_log","event":"log.start")"));
    CHECK(out.bytes.ends_with("}\n"));
    const std::size_t first = out.bytes.size();
    CHECK(lg.drain(out));
    CHECK_EQ(out.bytes.size(), first);
    // Past the ring: the oldest evicted before the drain, counted.
    std::size_t pushed = 0;
    while (lg.ring().first() < 10) {
        qlog::event(lg, events::error_typed, qlog::Name("MemoryError"), qlog::Name("budget"), qlog::Name("drain"));
        ++pushed;
    }
    out.bytes.clear();
    out.writes = 0;
    // log.start was drained: the 9 lines after it were lost undrained.
    const std::uint64_t evicted = lg.ring().first();
    CHECK(lg.drain(out));
    const Lines got(out.bytes);
    REQUIRE(got.size() > 0);
    const std::size_t dropped = got.size() - 1;
    CHECK(untimed(got.at(dropped)).starts_with(R"({"count":9,"event":"log.dropped","level":"warn","log":2,)"));
    // Each event drained, counted, or evicted by log.dropped's own line,
    // which the next drain counts.
    const std::uint64_t later = lg.ring().first() - evicted;
    CHECK_EQ(dropped + 9 + later, pushed);
    CHECK(out.writes >= 1 && out.writes <= 2);
    // A failed write: false, the lines drained all the same.
    qlog::event(lg, events::error_typed, qlog::Name("MemoryError"), qlog::Name("budget"), qlog::Name("drain"));
    out.fail = true;
    CHECK(!lg.drain(out));
    out.fail = false;
    out.bytes.clear();
    CHECK(lg.drain(out));
    CHECK(out.bytes.empty());
    std::cout << "log.drain: " << pushed << " events, " << dropped << " drained, 9 dropped, " << later
              << " evicted by log.dropped's line\n";
}

QIVEN3_CASE(log, budget) {
    // The Log's two blocks: a byte short refuses the second, nothing held.
    {
        Arena& shorter = qiven3_ctx.counted.arena(log_budget - 1);
        bool refused = false;
        try {
            const qlog::Log lg(shorter, qlog::Count(1U), "qiven3_unit_log");
        } catch (const MemoryError& e) {
            refused = e.code == MemoryCode::budget;
        }
        CHECK(refused);
        CHECK_EQ(shorter.used(), 0U);
    }
    // The widest event, eight 64-byte names: its line within 1,024 bytes
    // and its arena's peak within the 4 KiB.
    Arena& arena = qiven3_ctx.counted.arena(log_budget);
    qlog::Log lg(arena, qlog::Count(qlog::max_number), "qiven3_unit_log");
    qiven3::test::widest(lg);
    CHECK_EQ(lg.failed(), 0U);
    const Lines lines(lg.ring().lines());
    REQUIRE(lines.size() == 2);
    const std::size_t line = lines.at(1).size();
    CHECK(line <= qiven3::test::widest_max());
    CHECK(qiven3::test::widest_max() <= qlog::Log::max_line_bytes);
    CHECK(lg.peak() <= qlog::Log::event_bytes);
    std::cout << "log.budget: the widest line " << line << " bytes (at most " << qiven3::test::widest_max()
              << "), its event's peak " << lg.peak() << " of " << qlog::Log::event_bytes << "\n";
    // The same line over its peak less a byte: MemoryError budget, the
    // output unchanged.
    const std::size_t peak = qiven3::test::widest_peak(qiven3_ctx.counted.arena(qlog::Log::event_bytes));
    CHECK(peak <= lg.peak());
    Arena& tight = qiven3_ctx.counted.arena(peak - 1);
    bool refused = false;
    try {
        static_cast<void>(qiven3::test::widest_peak(tight));
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget;
    }
    CHECK(refused);
    CHECK_EQ(tight.used(), 0U);
}

QIVEN3_CASE(log, faults) {
#if defined(QIVEN3_FAULTS)
    std::vector<std::byte> pages(log_budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto make = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        const qlog::Log lg(*arena, qlog::Count(1U), "qiven3_unit_log");
    };
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    const qiven3::test::FaultRuns made = qiven3::test::run_faults<MemoryError>(countdown, make, check);
    // The ring's span and the events' pages.
    CHECK_EQ(made.typed, 2U);
    CHECK_EQ(made.completed_at, 3U);
    CHECK_EQ(made.untyped, 0U);
    CHECK_EQ(made.unchecked, 0U);

    // An event's allocations, each failed in turn: the event dropped and
    // counted, the ring and the operation unchanged, never an exception.
    arena.emplace(pages);
    qlog::Log lg(*arena, qlog::Count(1U), "qiven3_unit_log");
    lg.attach(&countdown);
    std::size_t allocations = 0;
    std::size_t completed_at = 0;
    for (std::size_t k = 1; k < 100 && completed_at == 0; ++k) {
        countdown.arm(k);
        const std::uint64_t failed = lg.failed();
        const std::uint64_t next = lg.ring().next();
        qlog::event(lg, events::error_typed, qlog::Name("MemoryError"), qlog::Name("budget"), qlog::Name("faults"));
        if (countdown.fired()) {
            ++allocations;
            CHECK_EQ(lg.failed(), failed + 1);
            CHECK_EQ(lg.ring().next(), next);
        } else {
            completed_at = k;
            CHECK_EQ(lg.ring().next(), next + 1);
        }
        countdown.disarm();
    }
    lg.attach(nullptr);
    std::cout << "log.faults: the Log's allocations " << made.typed << ", an event's " << allocations << "\n";
    // The value's nodes and text, encode's members and the line's bytes.
    CHECK_EQ(allocations, 4U);
    CHECK_EQ(completed_at, 5U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS");
#endif
}

QIVEN3_CASE(log, constant) {
    // The static half (unit/log_static.cpp) held if this
    // binary was built; this reports it.
    CHECK(qiven3::test::constant_held());
}
