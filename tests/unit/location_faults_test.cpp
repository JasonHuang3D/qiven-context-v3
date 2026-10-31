// location's fault runs, on the fake over a scratch directory of the case's own (support/fixtures/location_bed.hpp's
// steps).
// faults_*: each step run with each of its platform calls, command and store arena allocations and engine
// calls failed in turn, and the engine heap's allocations with an engine installed; faults_init,
// faults_change and faults_heap_create in parts, _part1 on (support/faults/parts.hpp), each within 30 s on
// every job: each run exit 1, or the step's own status where the
// failure came after the record or was absorbed, or, for a line's write failed, 2 with that write the run's last
// platform call; after each run every page given back, nothing but the record in the configuration directory,
// the record as before or, at exit 0 or after replace, whole; after a refused run the next run, failing nothing,
// the step's own status.
// unwritten: each step with each of its lines' writes failed in turn: exit 2, that write the run's last call,
// the lines before it written as an unfailed run writes them, the record as before or, after replace, whole.

#include <algorithm>
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
#include "support/faults/parts.hpp"
#include "support/fixtures/location_bed.hpp"
#include "wire/wire.hpp"

namespace {
using namespace qiven3::test::location_bed;
}  // namespace

#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
namespace {

namespace wire = qiven3::wire;

struct Typed {};
struct Untyped {};

enum class Kind : std::uint8_t { platform, arena, store, call, heap };
constexpr std::array<std::string_view, 5> kind_names = {"platform call", "command arena allocation",
                                                        "store arena allocation", "engine call",
                                                        "engine heap allocation"};

struct Tally {
    std::size_t refused = 0;
    std::size_t kept = 0;       // the step's own status: after the record, or absorbed
    std::size_t next = 0;       // after a refused run, the next run's own status
    std::size_t unwritten = 0;  // a line's write failed: the wire's status
};

// After a run that ended with a line's write failed, from the fake's call
// `from` on: that write its last platform call, and the record as before
// or, where replace ran, the step's own.
bool unwritten_held(const Bed& b, Step s, std::size_t from) {
    const std::vector<std::string>& calls = b.fake->calls();
    if (calls.size() <= from || calls.back() != "standard_output") {
        return false;
    }
    bool replaced = false;
    for (std::size_t i = from; i < calls.size(); ++i) {
        replaced = replaced || calls[i] == "replace";
    }
    return b.record_held(s, replaced ? location::recorded : location::refused);
}

// One step, each call of kind failed in turn, the runs of the part's range
// r executing (support/faults/parts.hpp's PartRuns).
void pass(qiven3::test::Context& qiven3_ctx, Bed& b, Step s, Kind kind, qiven3::test::FaultRange r) {
    Countdown c;
    qiven3::test::PartRuns part(r);
    const std::size_t lent = b.fake->lent();
    Tally t;
    int code = 0;
    bool fired = false;
    std::size_t from = 0;  // the run's first call on the fake
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
        part.enter<Typed>(c);
        b.prepare(s);
        from = b.fake->calls().size();
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
        if (code == location::refused || code == Bed::expected(s) ||
            (code == wire::failed && kind == Kind::platform)) {
            throw Typed{};
        }
        throw Untyped{};
    };
    const auto check = [&](std::size_t k) {
        if (part.skipped()) {
            return true;
        }
        // No step's own status is the wire's.
        const bool unwritten = fired && code == wire::failed;
        bool held = b.fake->lent() == lent && b.config_clean() &&
                    (unwritten ? unwritten_held(b, s, from) : b.record_held(s, code));
        if (unwritten) {
            ++t.unwritten;  // no next run: the record stands as the run left it
        } else if (fired && code == location::refused && Bed::expected(s) != location::refused) {
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
              << runs.typed << " runs, completed at " << runs.completed_at << " (executing " << r.from << " to "
              << part.last(runs) << "); refused " << t.refused
              << ", its own status " << t.kept << ", the next run its own " << t.next << ", a line unwritten "
              << t.unwritten << "; untyped " << runs.untyped
              << ", unchecked " << runs.unchecked << "\n";
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(runs.completed_at, runs.typed + 1);
    CHECK(kind != Kind::platform || runs.typed > 0);
    CHECK(part.held(runs));
}

// The passes of the chosen steps under the kinds, kind by kind and each
// step in turn: pass p the kind p / steps and the step p % steps; part i of
// parts runs its range of each (support/faults/parts.hpp). Whether the part
// reaches pass after, the one past them.
template <std::size_t N>
bool passes(qiven3::test::Context& qiven3_ctx, std::span<const Step> chosen, std::span<const Kind> kinds,
            const qiven3::test::FaultParts<N>& parts, std::size_t i) {
    const auto b = std::make_unique<Bed>(Scratch{});
    for (std::size_t k = 0; k < kinds.size(); ++k) {
        if (kinds[k] == Kind::heap) {
            b->engine.emplace(*b->fake, 1);
        }
        for (std::size_t s = 0; s < chosen.size(); ++s) {
            const qiven3::test::FaultRange r = parts.range(i, (k * chosen.size()) + s);
            if (!r.none()) {
                pass(qiven3_ctx, *b, chosen[s], kinds[k], r);
            }
        }
        b->engine.reset();
    }
    return !parts.range(i, kinds.size() * chosen.size()).none();
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

// Each case's parts by their first points, pass and k (passes() numbers the passes), each part within 30 s on
// every job. init: the platform calls' passes; the arenas'; the engine calls' of init new from k 1, 34 and 67;
// the rest. change: the platform calls' passes; the command arena's of show, change same and change new; the
// rest to change new's engine calls, from k 1, 34 and 67 (67 to its completing run, 99, in three); the rest and
// store_dir's (pass 20). init's create under the engine heap: 41 k a stretch, the last to its completing run
// (652 on Windows, 640 on Linux). A stretch a CI Windows job ran in over 20 s (its slowest seconds beside it)
// is cut in even parts, each taking 20 s or under there.
constexpr qiven3::test::FaultParts<6> init_parts{{{{0, 1}, {4, 1}, {12, 1}, {12, 34}, {12, 67}, {13, 1}}}};
constexpr qiven3::test::FaultParts<9> change_parts{
    {{{0, 1}, {5, 1}, {8, 1}, {17, 1}, {17, 34}, {17, 67}, {17, 78}, {17, 89}, {18, 1}}}};  // 17, 67: 46.0 s
constexpr qiven3::test::FaultParts<23> heap_create_parts{{{
    {0, 1}, {0, 22},                           // 27.7 s
    {0, 42}, {0, 83},                          // 14.3, 19.2 s
    {0, 124}, {0, 145},                        // 20.4 s
    {0, 165}, {0, 206}, {0, 247}, {0, 288},    // 19.0, 19.1, 17.7, 18.3 s
    {0, 329},                                  // 18.6 s
    {0, 370}, {0, 384}, {0, 398},              // 46.3 s
    {0, 411}, {0, 425}, {0, 439},              // 45.5 s
    {0, 452}, {0, 473},                        // 28.6 s
    {0, 493}, {0, 534}, {0, 575}, {0, 616},    // 19.3, 19.4, 19.1, 19.4 s
}}};
static_assert(init_parts.whole() && change_parts.whole() && heap_create_parts.whole());

void init_part(qiven3::test::Context& qiven3_ctx, std::size_t i) {
    CHECK(qiven3::test::registered("location.faults_init", init_parts.from.size()));
    passes(qiven3_ctx, init_steps, own_kinds, init_parts, i);
}
void change_part(qiven3::test::Context& qiven3_ctx, std::size_t i) {
    CHECK(qiven3::test::registered("location.faults_change", change_parts.from.size()));
    if (passes(qiven3_ctx, change_steps, own_kinds, change_parts, i)) {
        const auto b = std::make_unique<Bed>(Scratch{});
        store_dir_faults(qiven3_ctx, *b);
    }
}
void heap_create_part(qiven3::test::Context& qiven3_ctx, std::size_t i) {
    CHECK(qiven3::test::registered("location.faults_heap_create", heap_create_parts.from.size()));
    passes(qiven3_ctx, heap_create, heap_kind, heap_create_parts, i);
}

// One step's runs, each of its lines' writes failed in turn by the fake's
// countdown (as the wire's answer_faults): the lines failed. The countdown
// asked at the i-th call is at least the calls before it that ask it (all
// but wipe), and more where the fake's files ask it within a call: from
// there, the first armed run whose fault ends it 2 at its i-th call, a
// standard_output, is that line's.
std::size_t unwritten_runs(qiven3::test::Context& qiven3_ctx, Bed& b, Step s) {
    b.prepare(s);
    const std::size_t from = b.fake->calls().size();
    const std::size_t mark = b.fake->written().size();
    CHECK_EQ(b.run(s), Bed::expected(s));
    const std::vector<std::string> calls(b.fake->calls().begin() + static_cast<std::ptrdiff_t>(from),
                                         b.fake->calls().end());
    const std::string whole = b.fake->written().substr(mark);
    std::size_t lines = 0;
    std::size_t asked = 0;
    std::size_t k = 1;
    for (std::size_t i = 0; i < calls.size(); ++i) {
        asked += calls[i] == "wipe" ? 0U : 1U;
        if (calls[i] != "standard_output") {
            continue;
        }
        ++lines;
        Countdown c;
        bool hit = false;
        std::size_t at = 0;
        std::size_t written = 0;
        for (k = std::max(k, asked); !hit && k < asked + 1000; ++k) {
            b.prepare(s);
            at = b.fake->calls().size();
            written = b.fake->written().size();
            b.fake->attach(&c);
            c.arm(k);
            const int code = b.run(s);
            b.fake->attach(nullptr);
            hit = c.fired() && code == wire::failed && b.fake->calls().size() == at + i + 1 &&
                  b.fake->calls().back() == "standard_output";
            c.disarm();
        }
        // The wire's status at that write, the run's last call; the lines
        // before it as an unfailed run writes them, and no more.
        CHECK(hit);
        const std::string before = b.fake->written().substr(written);
        CHECK(before.size() < whole.size() && whole.starts_with(before));
        CHECK(unwritten_held(b, s, at));
    }
    CHECK(lines > 0);
    return lines;
}

}  // namespace

QIVEN3_CASE(location, unwritten) {
    const auto b = std::make_unique<Bed>(Scratch{});
    std::size_t lines = 0;
    for (std::size_t s = 0; s < step_names.size(); ++s) {
        lines += unwritten_runs(qiven3_ctx, *b, static_cast<Step>(s));
    }
    std::cout << "location.unwritten: " << step_names.size() << " steps, " << lines << " lines' writes failed\n";
}

QIVEN3_CASE(location, faults_heap_open) { passes(qiven3_ctx, heap_open, heap_kind, qiven3::test::FaultParts<1>{}, 0); }
#else
namespace {
constexpr std::string_view unfaulted = "not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers";
void init_part(qiven3::test::Context& /*qiven3_ctx*/, std::size_t /*i*/) { QIVEN3_SKIP(unfaulted); }
void change_part(qiven3::test::Context& /*qiven3_ctx*/, std::size_t /*i*/) { QIVEN3_SKIP(unfaulted); }
void heap_create_part(qiven3::test::Context& /*qiven3_ctx*/, std::size_t /*i*/) { QIVEN3_SKIP(unfaulted); }
}  // namespace
QIVEN3_CASE(location, unwritten) { QIVEN3_SKIP(unfaulted); }
QIVEN3_CASE(location, faults_heap_open) { QIVEN3_SKIP(unfaulted); }
#endif

QIVEN3_FAULT_PART(location, faults_init, 1, init_part)
QIVEN3_FAULT_PART(location, faults_init, 2, init_part)
QIVEN3_FAULT_PART(location, faults_init, 3, init_part)
QIVEN3_FAULT_PART(location, faults_init, 4, init_part)
QIVEN3_FAULT_PART(location, faults_init, 5, init_part)
QIVEN3_FAULT_PART(location, faults_init, 6, init_part)
QIVEN3_FAULT_PART(location, faults_change, 1, change_part)
QIVEN3_FAULT_PART(location, faults_change, 2, change_part)
QIVEN3_FAULT_PART(location, faults_change, 3, change_part)
QIVEN3_FAULT_PART(location, faults_change, 4, change_part)
QIVEN3_FAULT_PART(location, faults_change, 5, change_part)
QIVEN3_FAULT_PART(location, faults_change, 6, change_part)
QIVEN3_FAULT_PART(location, faults_change, 7, change_part)
QIVEN3_FAULT_PART(location, faults_change, 8, change_part)
QIVEN3_FAULT_PART(location, faults_change, 9, change_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 1, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 2, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 3, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 4, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 5, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 6, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 7, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 8, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 9, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 10, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 11, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 12, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 13, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 14, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 15, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 16, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 17, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 18, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 19, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 20, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 21, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 22, heap_create_part)
QIVEN3_FAULT_PART(location, faults_heap_create, 23, heap_create_part)
