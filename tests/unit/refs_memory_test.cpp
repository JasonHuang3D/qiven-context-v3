// refs' budgets and fault runs: check_reference compiles each syntax it
// meets, two allocations each, freed before the next; over an arena it
// leaves the arena's use where it was, and its cursor moved by at most one
// compile's budget a compile. Each allocation of each type's reference,
// and of a repo subject's, failed in turn, is a MemoryError with nothing
// held once the operation ends; the run completes past the last, which
// counts them exactly: entry 2, repo 4 and 8 with its revision and anchor,
// transcript 8, output 2, a repo subject 2, any other subject, a path and
// a name none. The runner's count after each case holds too.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/matcher.hpp"
#include "entry/refs.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace refs = qiven3::entry::refs;
namespace canon = qiven3::canon;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

#define DIGEST "hmac-sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

struct Case {
    std::string_view name;
    std::string_view text;
    std::size_t allocations;
};

constexpr std::array<Case, 5> cases = {
    Case{"entry", R"({"id":"sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","type":"entry"})", 2},
    Case{"repo", R"({"digest":")" DIGEST R"(","path":"qiven3/entry.py","repository":"qiven","type":"repo"})", 4},
    Case{"repo, revision and anchor",
         R"({"anchor":"Q14","digest":")" DIGEST R"(","name":"entry_id","path":"a.md","repository":"q",)"
         R"("revision":"0123456789abcdef0123456789abcdef01234567","type":"repo"})",
         8},
    Case{"transcript",
         R"({"client":"claude-code","digest":")" DIGEST R"(","message":"m-1","session":"s-1","type":"transcript"})", 8},
    Case{"output", R"({"command":"python -I probe.py","digest":")" DIGEST R"(","type":"output"})", 2},
};

constexpr std::size_t budget = 262144;

}  // namespace

QIVEN_CASE(refs, budget) {
    for (const Case& c : cases) {
        Arena& arena = qiven_ctx.counted.arena(budget);
        canon::Basic<Allocator<char>> v{Allocator<char>(arena)};
        canon::parse(c.text, v);
        const std::size_t used = arena.used();
        const std::size_t before = arena.peak();
        CHECK(!refs::try_check_reference(v).has_value());
        CHECK_EQ(arena.used(), used);
        // The arena's cursor never goes back: each compile adds its own.
        CHECK(arena.peak() - before <= c.allocations / 2 * qiven3::entry::matcher::compile_budget);
        std::cout << "refs.budget: " << c.name << ": " << arena.peak() - before << " bytes for " << c.allocations / 2
                  << " compiles, within " << qiven3::entry::matcher::compile_budget << " each\n";
    }
    // A path, a name and a subject but repo's take nothing from an arena of none.
    Arena& none = qiven_ctx.counted.arena(64);
    CHECK(refs::check_subject("entity:docker", Allocator<char>(none)));
    CHECK(refs::check_path("src/x.py") && refs::check_name("Store.append"));
    CHECK_EQ(none.peak(), 0U);
    // A byte short of one compile's peak: MemoryError, nothing held.
    Arena& one = qiven_ctx.counted.arena(budget);
    CHECK(refs::check_subject("repo:qiven/src/x.py", Allocator<char>(one)));
    const std::size_t peak = one.peak();
    CHECK_EQ(one.used(), 0U);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    bool refused = false;
    try {
        Arena& shorter = qiven_ctx.counted.arena(peak - 1);
        static_cast<void>(refs::check_subject("repo:qiven/src/x.py", Allocator<char>(shorter)));
    } catch (const MemoryError&) {
        refused = true;
    }
    CHECK(refused);
#endif
}

QIVEN_CASE(refs, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    bool done = false;
    // The value and every compile given back once the operation ends.
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    const auto report = [&](std::string_view op, const qiven3::test::FaultRuns& runs, std::size_t want) {
        std::cout << "refs.faults: " << op << "'s allocations " << runs.typed << ", completed at " << runs.completed_at
                  << "\n";
        CHECK_EQ(runs.typed, want);
        CHECK_EQ(runs.completed_at, want + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        done = false;
    };
    for (const Case& c : cases) {
        const auto op = [&] {
            arena.emplace(pages);
            canon::Basic<Allocator<char>> v{Allocator<char>(*arena)};
            canon::parse(c.text, v);
            arena->attach(&countdown);
            done = !refs::try_check_reference(v).has_value();
            arena->attach(nullptr);
        };
        report(c.name, qiven3::test::run_faults<MemoryError>(qiven_ctx.log, "refs.check_reference", countdown, op, empty),
               c.allocations);
    }
    for (const auto& [subject, want] : {std::pair<std::string_view, std::size_t>{"repo:qiven/src/x.py#f", 2},
                                        std::pair<std::string_view, std::size_t>{"module:qiven3.entry", 0}}) {
        const auto op = [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            done = refs::check_subject(subject, Allocator<char>(*arena));
            arena->attach(nullptr);
        };
        report(subject, qiven3::test::run_faults<MemoryError>(qiven_ctx.log, "refs.check_subject", countdown, op, empty),
               want);
    }
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
