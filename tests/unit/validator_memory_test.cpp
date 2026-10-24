// validator's budgets and fault runs, over three calls of the fixtures:
// reviewer's C, M1, M2, ACC2 and MX on implementer's lane held; learner's
// L1 and L2, L2 citing L1 in the call; and M2 alone, revising M1 in the
// view. validate over an arena peaks within 256 KiB plus 8 times the
// call's contents and its revisions' targets' contents, and leaves the
// arena as it found it. Each allocation, failed in turn, is a MemoryError
// with nothing held; the run completes past the last, which counts them
// exactly: the index 1; each entry with a body its value's 4; a revision
// of a kind with an identity its target's value's 4 and, from the view,
// the text 1; each header the view gives 1 (a first entry's parent, a
// target, an entry referenced): reviewer's 1 + 20 + 4 + 2 (A and D) = 27;
// learner's 1 + 8 = 9; M2's 1 + 4 + 2 (M1 as parent and target) + 1 + 4
// = 12.
// The runner's count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "entry/validator.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/view.hpp"

namespace {

namespace validator = qiven3::entry::validator;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using Item = fixtures::Item<Allocator<char>>;
using Slot = header::Slot;

// A case: the view's fixtures, the call's, the target the view gives a
// revision's content of, or none, and the allocations it takes.
struct Case {
    std::string name;
    std::vector<std::string_view> held;
    std::vector<std::string_view> call;
    std::string_view target;
    std::size_t allocations;
};

std::vector<Case> cases() {
    return {
        {"reviewer", {"A", "B", "D", "ACC"}, {"C", "M1", "M2", "ACC2", "MX"}, {}, 27},
        {"learner", {}, {"L1", "L2"}, {}, 9},
        {"M2 alone", {"A", "B", "D", "ACC", "C", "M1"}, {"M2"}, "M1", 12},
    };
}

constexpr std::size_t budget_base = 262144;

// The view over a case's held fixtures, and its call's items on an arena.
struct Setup {
    fixtures::MemoryView view;
    std::vector<Item> items;
    std::string scope;
    std::string lane;
    std::optional<std::string> expected;
    std::size_t bytes = 0;
};

Setup setup(const canon::Basic<>& v, const Case& c, Arena& arena) {
    Setup s;
    for (const std::string_view n : c.held) {
        const fixtures::EntryView e = *fixtures::find_entry(v, n);
        s.view.put(header::parse_line(e.header), e.content);
    }
    for (const std::string_view n : c.call) {
        const fixtures::EntryView e = *fixtures::find_entry(v, n);
        Item it{header::parse_line(e.header, Allocator<char>(arena)), std::nullopt};
        if (e.content) {
            it.text.emplace(*e.content, Allocator<char>(arena));
            s.bytes += e.content->size();
        }
        s.items.push_back(std::move(it));
    }
    if (!c.target.empty()) {
        s.bytes += fixtures::find_entry(v, c.target)->content->size();
    }
    const header::Header<Allocator<char>>& first = s.items.at(0).header();
    s.scope = first.value(Slot::scope);
    s.lane = first.value(Slot::lane);
    if (const std::optional<validator::Head> head = s.view.head(s.scope, s.lane)) {
        s.expected = std::string(head->id.view());
    }
    return s;
}

std::optional<validator::RuleError> run(Setup& s, Arena& arena) {
    const std::optional<std::string_view> expected =
        s.expected ? std::optional<std::string_view>(*s.expected) : std::nullopt;
    return validator::try_validate(s.view, s.scope, s.lane, expected, std::span<const Item>(s.items),
                                   Allocator<char>(arena));
}

}  // namespace

QIVEN3_CASE(validator, budget) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<std::byte> pages(1U << 20U);
    for (const Case& c : cases()) {
        Arena items(pages);
        Setup s = setup(v, c, items);
        Arena& arena = qiven3_ctx.counted.arena(budget_base + 8 * s.bytes);
        const std::size_t used = arena.used();
        const std::size_t before = arena.peak();
        CHECK(!run(s, arena));
        CHECK_EQ(arena.used(), used);
        CHECK(arena.peak() - before <= budget_base + 8 * s.bytes);
        std::cout << "validator.budget: " << c.name << ": " << arena.peak() - before << " bytes, within "
                  << budget_base + 8 * s.bytes << "\n";
        s.items.clear();
    }
}

QIVEN3_CASE(validator, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<std::byte> item_pages(1U << 20U);
    std::vector<std::byte> pages(budget_base + 8 * 65536);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    bool done = false;
    // Every buffer and value given back.
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    for (const Case& c : cases()) {
        Arena items(item_pages);
        Setup s = setup(v, c, items);
        const auto op = [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            done = !run(s, *arena);
            arena->attach(nullptr);
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "validator.validate", countdown, op, empty);
        std::cout << "validator.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, c.allocations);
        CHECK_EQ(runs.completed_at, c.allocations + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        done = false;
        s.items.clear();
    }
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
