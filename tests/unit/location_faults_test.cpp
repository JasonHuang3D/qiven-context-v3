// location's fault runs, on the fake over a scratch directory of the case's own (support/fixtures/location_bed.hpp's
// steps).
// faults_*: each step run with each of its platform calls, command and store arena allocations and engine
// calls failed in turn, and the engine heap's allocations with an engine installed (init's create in three
// parts by their k, each within 30 s on every job): each run exit 1, or the step's own status where the
// failure came after the record or was absorbed; after each run every page given back, nothing but the record
// in the configuration directory, the record as before or, at exit 0, whole; after a refused run the next
// run, failing nothing, the step's own status.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/location.hpp"
#include "runner/runner.hpp"
#include "store/engine/sqlite.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/location_bed.hpp"

namespace {
using namespace qiven3::test::location_bed;
}  // namespace

#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

struct Typed {};
struct Untyped {};

enum class Kind : std::uint8_t { platform, arena, store, call, heap };
constexpr std::array<std::string_view, 5> kind_names = {"platform call", "command arena allocation",
                                                        "store arena allocation", "engine call",
                                                        "engine heap allocation"};

struct Tally {
    std::size_t refused = 0;
    std::size_t kept = 0;  // the step's own status: after the record, or absorbed
    std::size_t next = 0;  // after a refused run, the next run's own status
};

// One step, each call of kind failed in turn.
// from and to bound the runs that execute: a run k before from fails at
// once by its own k calls, executing nothing; a run past to completes at
// once, failing nothing, and ends the pass. Each run from from executes,
// counted.
void pass(qiven3::test::Context& qiven3_ctx, Bed& b, Step s, Kind kind, std::size_t from = 1,
          std::size_t to = SIZE_MAX) {
    Countdown c;
    std::size_t n = 0;
    std::size_t executed = 0;
    bool skipped = false;
    const std::size_t lent = b.fake->lent();
    Tally t;
    int code = 0;
    bool fired = false;
    const auto attach = [&](Countdown* on) {
        if (kind == Kind::platform) {
            b.fake->attach(on);
        } else if (kind == Kind::store) {
            sqlite::attach_calls(nullptr, on);
        } else if (kind == Kind::call) {
            sqlite::attach_calls(on, nullptr);
        } else if (kind == Kind::heap) {
            b.engine->heap().attach(on);
        }
    };
    const auto op = [&] {
        skipped = ++n < from || n > to;
        if (n < from) {
            for (std::size_t i = 0; i < n; ++i) {
                static_cast<void>(c.fail());
            }
            throw Typed{};
        }
        if (n > to) {
            return;
        }
        ++executed;
        b.prepare(s);
        attach(&c);
        code = b.run(s, kind == Kind::arena ? &c : nullptr);
        attach(nullptr);
        fired = c.fired();
        if (!fired) {
            if (code != Bed::expected(s)) {
                throw Untyped{};
            }
            return;
        }
        if (code == location::refused || code == Bed::expected(s)) {
            throw Typed{};
        }
        throw Untyped{};
    };
    const auto check = [&](std::size_t k) {
        if (skipped) {
            return true;
        }
        bool held = b.fake->lent() == lent && b.config_clean() && b.record_held(s, code);
        if (fired && code == location::refused && Bed::expected(s) != location::refused) {
            ++t.refused;
            const int again = b.run(s);
            t.next += again == Bed::expected(s) ? 1U : 0U;
            held = held && again == Bed::expected(s) && b.record_held(s, again) && b.fake->lent() == lent;
        } else if (fired) {
            ++(code == location::refused ? t.refused : t.kept);
        }
        if (!held) {
            std::cout << "location.faults: " << name_of(s) << ", " << kind_names.at(static_cast<std::size_t>(kind))
                      << " " << k << ": exit " << code << ", not as before\n";
        }
        return held;
    };
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<Typed>(qiven3_ctx.log, "location.main", c, op, check, 5000);
    attach(nullptr);
    std::cout << "location.faults: " << name_of(s) << ", " << kind_names.at(static_cast<std::size_t>(kind)) << ": "
              << runs.typed << " runs, completed at " << runs.completed_at << " (executing " << from << " to "
              << (to == SIZE_MAX ? runs.completed_at : to) << "); refused " << t.refused
              << ", its own status " << t.kept << ", the next run its own " << t.next << "; untyped " << runs.untyped
              << ", unchecked " << runs.unchecked << "\n";
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(runs.completed_at, runs.typed + 1);
    CHECK(kind != Kind::platform || runs.typed > 0);
    CHECK(to == SIZE_MAX || runs.completed_at == to + 1);
    CHECK_EQ(executed, (to == SIZE_MAX ? runs.completed_at : to) - from + 1);
}

void passes(qiven3::test::Context& qiven3_ctx, std::span<const Step> chosen, std::span<const Kind> kinds,
            std::size_t from = 1, std::size_t to = SIZE_MAX) {
    const auto b = std::make_unique<Bed>(Scratch{});
    for (const Kind kind : kinds) {
        if (kind == Kind::heap) {
            b->engine.emplace(*b->fake, 1);
        }
        for (const Step s : chosen) {
            pass(qiven3_ctx, *b, s, kind, from, to);
        }
        b->engine.reset();
    }
}

// store_dir with each platform call, then each allocation on the caller's
// arena, failed in turn: each run its typed error, the arena's blocks and
// the fake's pages given back, the record unchanged.
void store_dir_faults(qiven3::test::Context& qiven3_ctx, Bed& b) {
    b.prepare(Step::show);
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    for (const bool on_arena : {false, true}) {
        Countdown c;
        std::size_t used = 0;
        bool fired = false;
        std::string got;
        const auto op = [&] {
            qiven3::memory::Arena arena(pages);
            got.clear();
            if (on_arena) {
                arena.attach(&c);
            } else {
                b.fake->attach(&c);
            }
            try {
                qiven3::memory::String out{qiven3::memory::Allocator<char>(arena)};
                location::store_dir(*b.fake, out);
                got = std::string(out);
            } catch (const qiven3::store::StoreError&) {
                got = "StoreError";
            } catch (const qiven3::platform::PlatformError&) {
                got = "PlatformError";
            } catch (const qiven3::memory::MemoryError&) {
                got = "MemoryError";
            }
            b.fake->attach(nullptr);
            used = arena.used();
            fired = c.fired();
            if (fired) {
                if (got == "StoreError" || got == "PlatformError" || got == "MemoryError") {
                    throw Typed{};
                }
                throw Untyped{};
            }
        };
        const auto check = [&](std::size_t) {
            return used == 0 && b.fake->lent() == 0 && bytes_of(b.record) == b.before &&
                   (fired || got == text(b.held));
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<Typed>(qiven3_ctx.log, "location.store_dir", c, op, check, 1000);
        std::cout << "location.faults: store_dir, " << (on_arena ? "caller arena allocation" : "platform call") << ": "
                  << runs.typed << " runs, completed at " << runs.completed_at << "; untyped " << runs.untyped
                  << ", unchecked " << runs.unchecked << "\n";
        CHECK(runs.typed > 0);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK_EQ(runs.completed_at, runs.typed + 1);
    }
}

constexpr std::array<Kind, 4> own_kinds = {Kind::platform, Kind::arena, Kind::store, Kind::call};
constexpr std::array<Step, 4> init_steps = {Step::init_new, Step::init_held, Step::init_chosen, Step::init_warned};
constexpr std::array<Step, 5> change_steps = {Step::show, Step::change_same, Step::change_new, Step::change_held,
                                              Step::change_left};
constexpr std::array<Step, 1> heap_create = {Step::init_new};
constexpr std::array<Step, 1> heap_open = {Step::change_held};
constexpr std::array<Kind, 1> heap_kind = {Kind::heap};

// init's create, its engine heap's allocations failed in three parts, each from its k to the next part's
// less one, the last to the completing run: together every fault point once, as the compiler checks.
constexpr std::array<std::size_t, 3> heap_create_from = {1, 218, 435};
struct Range {
    std::size_t from;
    std::size_t to;
};
constexpr Range heap_create_range(std::size_t part) {
    return {heap_create_from.at(part),
            part + 1 < heap_create_from.size() ? heap_create_from.at(part + 1) - 1 : SIZE_MAX};
}
static_assert(heap_create_range(0).from == 1 && heap_create_range(0).from <= heap_create_range(0).to &&
              heap_create_range(0).to + 1 == heap_create_range(1).from &&
              heap_create_range(1).from <= heap_create_range(1).to &&
              heap_create_range(1).to + 1 == heap_create_range(2).from && heap_create_range(2).to == SIZE_MAX);
void heap_create_part(qiven3::test::Context& qiven3_ctx, std::size_t part) {
    passes(qiven3_ctx, heap_create, heap_kind, heap_create_range(part).from, heap_create_range(part).to);
}

}  // namespace

QIVEN3_CASE(location, faults_init) { passes(qiven3_ctx, init_steps, own_kinds); }
QIVEN3_CASE(location, faults_change) {
    passes(qiven3_ctx, change_steps, own_kinds);
    const auto b = std::make_unique<Bed>(Scratch{});
    store_dir_faults(qiven3_ctx, *b);
}
QIVEN3_CASE(location, faults_heap_create) { heap_create_part(qiven3_ctx, 0); }
QIVEN3_CASE(location, faults_heap_create_middle) { heap_create_part(qiven3_ctx, 1); }
QIVEN3_CASE(location, faults_heap_create_rest) { heap_create_part(qiven3_ctx, 2); }
QIVEN3_CASE(location, faults_heap_open) { passes(qiven3_ctx, heap_open, heap_kind); }
#else
namespace {
constexpr std::string_view unfaulted = "not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers";
}  // namespace
QIVEN3_CASE(location, faults_init) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_change) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_create) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_create_middle) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_create_rest) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_open) { QIVEN3_SKIP(unfaulted); }
#endif
