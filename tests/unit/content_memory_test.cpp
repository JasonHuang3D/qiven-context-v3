// content's budgets and fault runs: check compiles each syntax it meets
// once, two allocations each, holding the automata until it returns, and
// refs' and a repo subject's compiles beside them; over an arena it leaves
// the arena's use where it was, and its cursor moved by at most one
// compile's budget a compile. Each allocation, failed in turn, is a
// MemoryError with nothing held once the check ends; the run completes
// past the last, which counts them exactly: a direction by import none;
// a usage's two ids one compile, 6 with its episode; A, a claim, 6; L1, a
// lesson, 8; M1, a map, 8; C, a check, 6; a checkpoint by import with
// for_lane, item and detail 6; a question with its question_id 4; a
// decision closing two ids 4. The runner's count after each case holds
// too.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/content.hpp"
#include "entry/header.hpp"
#include "entry/matcher.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace content = qiven3::entry::content;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

#define ID "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ID2 "sha256:1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ADMIT R"("admit":{"mistake":"m","underivable":"u"})"
#define OUTPUT                                                                                                   \
    R"([{"command":"c","digest":"hmac-sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",)" \
    R"("type":"output"}])"

// A case: its header line's fields, or a fixture's name, its content and
// the allocations its check takes.
struct Case {
    std::string name;
    std::string header;
    std::string content;
    std::size_t allocations;
};

std::string line(std::string_view kind, std::string_view writer, std::string_view basis) {
    header::FieldsView f;
    f.scope = "project:qiven";
    f.lane = "implementer";
    f.kind = kind;
    f.writer = writer;
    f.basis = basis;
    f.at = "2026-10-17T09:30:00.000Z";
    f.id = ID;
    std::string out;
    header::make(f).line(out);
    return out;
}

std::vector<Case> cases() {
    std::vector<Case> out;
    out.push_back(Case{"a direction by import", line("direction", "import", "owner-words"),
                       R"({"admit":{"mistake":"m","underivable":"u"},"band":"core","subject":"entity:owner",)"
                       R"("words":"w"})",
                       0});
    out.push_back(Case{"a usage of two ids", line("usage", "loop", "observed"),
                       R"({"episode":{"client":"claude-code","session":"s-1"},"served":[")" ID R"(",")" ID2 R"("]})", 6});
    // The format's changes: for_lane, item and detail, the digest, slug and
    // lane compiled; question_id, the digest and slug; closes, the digest
    // and id.
    out.push_back(Case{"a checkpoint by import", line("checkpoint", "import", "claimed"),
                       "{" ADMIT R"(,"band":"normal","detail":"d","evidence":)" OUTPUT
                       R"(,"for_lane":"reviewer","item":"p3","next_action":"n","progress":"p","subject":"entity:x"})",
                       6});
    out.push_back(Case{"a question with its id", line("question", "import", "claimed"),
                       "{" ADMIT R"(,"band":"normal","evidence":)" OUTPUT
                       R"(,"question":"q","question_id":"q1","subject":"entity:x"})",
                       4});
    out.push_back(Case{"a decision closing two", line("decision", "import", "claimed"),
                       "{" ADMIT R"(,"band":"normal","closes":[")" ID R"(",")" ID2 R"("],"decides":"d","evidence":)" OUTPUT
                       R"(,"subject":"entity:x"})",
                       4});
    canon::Basic<> v;
    fixtures::load_entries(v);
    for (const auto& [name, want] : {std::pair<std::string_view, std::size_t>{"A", 6}, {"L1", 8}, {"M1", 8}, {"C", 6}}) {
        const std::optional<fixtures::EntryView> e = fixtures::find_entry(v, name);
        if (!e || !e->content) {
            std::cout << "content.cases: no fixture " << name << "\n";
            continue;
        }
        out.push_back(Case{std::string(name), std::string(e->header), std::string(*e->content), want});
    }
    return out;
}

constexpr std::size_t budget = 262144;

}  // namespace

QIVEN_CASE(content, budget) {
    const std::vector<Case> all = cases();
    CHECK_EQ(all.size(), 9U);
    for (const Case& c : all) {
        const header::Header<> h = header::parse_line(c.header);
        Arena& arena = qiven_ctx.counted.arena(budget);
        canon::Basic<Allocator<char>> v{Allocator<char>(arena)};
        canon::parse(c.content, v);
        const std::size_t used = arena.used();
        const std::size_t before = arena.peak();
        CHECK(!content::try_check(h, v).has_value());
        CHECK_EQ(arena.used(), used);
        CHECK(arena.peak() - before <= c.allocations / 2 * qiven3::entry::matcher::compile_budget);
        std::cout << "content.budget: " << c.name << ": " << arena.peak() - before << " bytes for "
                  << c.allocations / 2 << " compiles, within " << qiven3::entry::matcher::compile_budget
                  << " each\n";
    }
}

QIVEN_CASE(content, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    bool done = false;
    // The value and every compile given back once the check ends.
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    for (const Case& c : cases()) {
        const header::Header<> h = header::parse_line(c.header);
        const auto op = [&] {
            arena.emplace(pages);
            canon::Basic<Allocator<char>> v{Allocator<char>(*arena)};
            canon::parse(c.content, v);
            arena->attach(&countdown);
            done = !content::try_check(h, v).has_value();
            arena->attach(nullptr);
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven_ctx.log, "content.check", countdown, op, empty);
        std::cout << "content.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
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
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
