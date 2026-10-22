// check's budgets and fault runs: check_entry over an arena peaks within
// 256 KiB plus 8 times the entry's line and record, and leaves the arena
// holding the entry alone, its header's text and its body's. Each
// allocation, failed in turn, is a MemoryError with nothing held once the
// entry is dropped; the run completes past the last, which counts them
// exactly: the line's value 4, the record's 4, the header 5, its check 8
// or 10, the body 1, the content's value 4 and its check's walk (A 6, L1 8,
// M1 8, C 6): A 34, L1 36, M1 36, C 34; a header with no body 4, 5 and its
// check's, A's 19, E's (a forget, no content) 17. The runner's count after
// each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "entry/body.hpp"
#include "entry/check.hpp"
#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace check = qiven3::entry::check;
namespace body = qiven3::entry::body;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using Fake = qiven3::platform::fake::FakePlatform;

// A case: its fixture, its line, its record or none, and the allocations
// its check takes.
struct Case {
    std::string name;
    std::string line;
    std::optional<std::string> record;
    std::size_t allocations;
};

std::vector<Case> cases(Fake& fake) {
    std::vector<Case> out;
    canon::Basic<> v;
    fixtures::load_entries(v);
    for (const auto& [name, with, want] : {std::tuple<std::string_view, bool, std::size_t>{"A", true, 34},
                                           {"L1", true, 36},
                                           {"M1", true, 36},
                                           {"C", true, 34},
                                           {"A", false, 19},
                                           {"E", false, 17}}) {
        const std::optional<fixtures::EntryView> e = fixtures::find_entry(v, name);
        if (!e || (with && (!e->content || !e->key))) {
            std::cout << "check.cases: no fixture " << name << "\n";
            continue;
        }
        Case c{std::string(name) + (with ? "" : " alone"), std::string(e->header), std::nullopt, want};
        if (with) {
            const header::Header<> h = header::parse_line(e->header);
            std::string record;
            body::make(*e->content, digest::key_from_hex(fake, *e->key), h.value(header::Slot::id)).line(record);
            c.record = record;
        }
        out.push_back(c);
    }
    return out;
}

std::optional<std::string_view> view(const std::optional<std::string>& record) {
    return record ? std::optional<std::string_view>(*record) : std::nullopt;
}

constexpr std::size_t budget_base = 262144;

std::size_t budget_of(const Case& c) { return budget_base + 8 * (c.line.size() + (c.record ? c.record->size() : 0)); }

}  // namespace

QIVEN3_CASE(check, budget) {
    Fake fake(qiven3::platform::fake::Config{});
    const std::vector<Case> all = cases(fake);
    CHECK_EQ(all.size(), 6U);
    for (const Case& c : all) {
        Arena& arena = qiven3_ctx.counted.arena(budget_of(c));
        const std::size_t used = arena.used();
        const std::size_t before = arena.peak();
        {
            const check::Entry<Fake, Allocator<char>> e =
                check::check_entry(fake, c.line, view(c.record), Allocator<char>(arena));
            // The entry's texts held, each rounded up to the arena's alignment.
            const std::size_t texts = e.header().bytes() + (e.has_body() ? e.body().bytes() : 0);
            CHECK(arena.used() - used >= texts);
            CHECK(arena.used() - used <= texts + 2 * alignof(std::max_align_t));
        }
        CHECK_EQ(arena.used(), used);
        CHECK(arena.peak() - before <= budget_of(c));
        std::cout << "check.budget: " << c.name << ": " << arena.peak() - before << " bytes, within "
                  << budget_of(c) << "\n";
    }
}

QIVEN3_CASE(check, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    Fake fake(qiven3::platform::fake::Config{});
    const std::vector<Case> all = cases(fake);
    CHECK_EQ(all.size(), 6U);
    std::vector<std::byte> pages(budget_base + 8 * 65536);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    bool done = false;
    // The entry and every value given back once it is dropped.
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    for (const Case& c : all) {
        const auto op = [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            {
                const check::Entry<Fake, Allocator<char>> e =
                    check::check_entry(fake, c.line, view(c.record), Allocator<char>(*arena));
                done = e.has_body() == c.record.has_value();
            }
            arena->attach(nullptr);
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "check.entry", countdown, op, empty);
        std::cout << "check.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, c.allocations);
        CHECK_EQ(runs.completed_at, c.allocations + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        done = false;
    }
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
