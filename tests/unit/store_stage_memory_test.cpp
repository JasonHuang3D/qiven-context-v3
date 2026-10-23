// store/stage's budgets and fault runs, over three calls: an append of A,
// B and D on an empty lane; an append of A's content with a key in it, its
// hit cleared; and a forget of E on implementer's lane held. Each line's
// work on a fresh scratch arena, its peak one line's, and what check keeps
// and admit takes on the operation's arena: the two together within the
// operation's budget, 1 MiB plus 4 times the lines given; the operation's
// arena as it was once the checked entries are dropped, what it took
// within 16 KiB plus 8 times the lines, and no scratch arena ending with a
// block held. Each allocation, failed in turn, is a
// MemoryError with nothing held; the run completes past the last, which
// counts them exactly: the entries' room 1; each entry's part 1 (check's:
// A 34, B 34, D 32, E 17); at append its content's value 4 and the secret
// check's 24; a first hit's room 1 in the scratch and 1 in the call's
// hits; each entry kept, its header 1 and body 1; at forget the line's
// value first, 4. Then admit: part 2's (validator's), its index 1 and each
// body's value 4, and a header the view gives 1 (a first entry's parent, a
// target). Append: 1 + 64 + 64 + 62 = 191 and 1 + 12 = 13, 204; the
// secret's 1 + 34 + 4 + 24 + 1 + 1 + 2 = 67 and 1 + 4 = 5, 72; E's 4 + 1 +
// 17 + 1 = 23 and 1 + 2 = 3, 26.
// The runner's count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/stage.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/fixtures/view.hpp"

namespace {

using namespace qiven3::test::stage_book;  // NOLINT(google-build-using-namespace)
using qiven3::memory::MemoryError;

// A case: its entries, the view's, the operation, the expected head, the
// clearing it needs, and the allocations its check and admit take.
struct Case {
    std::string name;
    std::vector<Stored> call;
    std::vector<Stored> held;
    Operation op;
    std::optional<std::string> expected;
    bool clear;
    std::size_t allocations;
};

std::vector<Case> cases(const Book& book) {
    std::vector<Case> out;
    out.push_back({"append", {book.stored("A"), book.stored("B"), book.stored("D")}, {}, Operation::append,
                   std::nullopt, false, 204});
    out.push_back({"secret", {book.with_secret("A", std::nullopt)}, {}, Operation::append, std::nullopt, true, 72});
    const std::vector<Stored> lane = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
    out.push_back({"forget", {book.stored("E")}, lane, Operation::forget, id_of(lane.back()), false, 26});
    return out;
}

void prepare(fixtures::MemoryView& view, const Case& c) {
    hold(view, c.held);
    if (c.clear) {
        view.clear("project:qiven", content_of(c.call.front()), "aws-akia");
    }
}

// check then admit, on chars and the scratch; whether the call was staged
// whole.
bool run(Fake& fake, fixtures::MemoryView& view, const Case& c, const std::vector<EntryView>& lines,
         const Chars& chars, ScratchPages& scratch) {
    const stage::Checked<Fake> checked =
        stage::check(fake, std::span<const EntryView>(lines), at(c.op), chars, scratch);
    const std::optional<std::string_view> expected =
        c.expected ? std::optional<std::string_view>(*c.expected) : std::nullopt;
    const stage::Staged staged = stage::admit(view, "project:qiven", checked.entries[0].header().value(Slot::lane),
                                              expected, checked, Limits{}, at(c.op), chars);
    return staged.count == c.call.size();
}

std::size_t budget_of(const std::vector<EntryView>& lines) {
    return *qiven3::store::operation_budget(*qiven3::store::given(lines));
}

}  // namespace

QIVEN3_CASE(store_stage, budget) {
    Fake fake(qiven3::platform::fake::Config{});
    const Book book(fake);
    for (const Case& c : cases(book)) {
        fixtures::MemoryView view;
        prepare(view, c);
        const std::vector<EntryView> lines = views(c.call);
        Arena& arena = qiven3_ctx.counted.arena(budget_of(lines));
        ScratchPages scratch(scratch_bytes);
        const std::size_t used = arena.used();
        const std::size_t before = arena.peak();
        CHECK(run(fake, view, c, lines, Chars(arena), scratch));
        CHECK(scratch.end());
        CHECK(!scratch.leaked());
        CHECK_EQ(arena.used(), used);
        const std::size_t kept = arena.peak() - before;
        CHECK(kept + scratch.peak() <= budget_of(lines));
        // What stays on the operation's arena is bounded by the lines: the
        // per-line work is the scratch's.
        CHECK(kept <= 16384 + 8 * *qiven3::store::given(lines));
        std::cout << "store_stage.budget: " << c.name << ": " << kept << " bytes kept and admitted, "
                  << scratch.peak() << " a line's scratch at most, within " << budget_of(lines) << "\n";
    }
}

QIVEN3_CASE(store_stage, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    Fake fake(qiven3::platform::fake::Config{});
    const Book book(fake);
    std::vector<std::byte> pages(4U << 20U);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    ScratchPages scratch(scratch_bytes);
    scratch.attach(&countdown);
    bool done = false;
    // Every entry, value and buffer given back, the scratch's too.
    const auto empty = [&](std::size_t) { return scratch.end() && arena->used() == 0; };
    for (const Case& c : cases(book)) {
        fixtures::MemoryView view;
        prepare(view, c);
        const std::vector<EntryView> lines = views(c.call);
        const auto op = [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            done = run(fake, view, c, lines, Chars(*arena), scratch);
            arena->attach(nullptr);
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "store_stage.call", countdown, op, empty);
        std::cout << "store_stage.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
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
