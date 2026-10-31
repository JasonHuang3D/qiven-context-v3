// kinds' budgets and fault run: identity and the tables' readers take no
// allocation. identity over every fixture entry's header and content, an
// imported checkpoint's for_lane and a question's question_id among them,
// held in an arena, leaves its use and its peak where they were; with the
// arena's countdown armed at its first allocation, identity completes, no
// call failed. The runner's count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/kinds_identity.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace header = qiven3::entry::header;
namespace kinds = qiven3::entry::kinds;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

constexpr std::size_t budget_for(std::size_t bytes) { return 262144 + 8 * bytes; }

struct Stored {
    std::string name;
    std::string header;
    std::string content;
};

std::vector<Stored> stored() {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<Stored> out;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        out.push_back(Stored{std::string(e.name), std::string(e.header), std::string(e.content.value_or(""))});
    }
    return out;
}

// The identities the fixtures lack: an imported checkpoint by its
// for_lane, and a question by its question_id.
std::vector<Stored> synthetic() {
    std::vector<Stored> out;
    for (const auto& [kind, writer, content] :
         {std::tuple<std::string_view, std::string_view, std::string_view>{
              "checkpoint", "import", R"({"for_lane":"implementer","next_action":"n","progress":"p"})"},
          {"question", "reviewer/s70", R"({"question":"x","question_id":"q1"})"}}) {
        header::FieldsView f;
        f.scope = "project:qiven";
        f.lane = header::role(writer);
        f.kind = kind;
        f.writer = writer;
        f.basis = "claimed";
        f.at = "2026-10-17T09:30:00.000Z";
        f.id = "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        std::string line;
        header::make(f).line(line);
        out.push_back(Stored{std::string(kind), line, std::string(content)});
    }
    return out;
}

}  // namespace

QIVEN_CASE(kinds, budget) {
    std::size_t read = 0;
    std::vector<Stored> all = stored();
    for (Stored& s : synthetic()) {
        all.push_back(std::move(s));
    }
    for (const Stored& s : all) {
        Arena& arena = qiven_ctx.counted.arena(budget_for(s.header.size() + s.content.size()));
        const header::Header<Allocator<char>> h = header::parse_line(s.header, Allocator<char>(arena));
        canon::Basic<Allocator<char>> content{Allocator<char>(arena)};
        if (!s.content.empty()) {
            canon::parse(s.content, content);
        }
        const std::size_t used = arena.used();
        const std::size_t peak = arena.peak();
        const std::optional<kinds::IdentityView> id = kinds::identity(h, content);
        const std::string_view k = h.value(header::Slot::kind);
        CHECK_EQ(id.has_value(), k == "map" || k == "checkpoint" || k == "question");
        static_cast<void>(kinds::served(k));
        static_cast<void>(kinds::project_only(k));
        CHECK_EQ(arena.used(), used);
        CHECK_EQ(arena.peak(), peak);
        ++read;
    }
    std::cout << "kinds.budget: identity over " << read << " entries, 0 allocations each\n";
    CHECK_EQ(read, 14U);
}

QIVEN_CASE(kinds, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::vector<Stored> all = stored();
    std::optional<Stored> map;
    for (const Stored& s : all) {
        if (s.name == "M1") {
            map = s;
        }
    }
    REQUIRE(map.has_value());
    std::vector<std::byte> pages(budget_for(map->header.size() + map->content.size()));
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    bool done = false;
    const auto op = [&] {
        arena.emplace(pages);
        const header::Header<Allocator<char>> h = header::parse_line(map->header, Allocator<char>(*arena));
        canon::Basic<Allocator<char>> content{Allocator<char>(*arena)};
        canon::parse(map->content, content);
        arena->attach(&countdown);
        done = kinds::identity(h, content).has_value();
        arena->attach(nullptr);
    };
    const auto empty = [&](std::size_t) { return true; };
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<MemoryError>(qiven_ctx.log, "kinds.identity", countdown, op, empty);
    std::cout << "kinds.faults: identity's allocations " << runs.typed << ", completed at " << runs.completed_at << "\n";
    CHECK_EQ(runs.typed, 0U);
    CHECK_EQ(runs.completed_at, 1U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK(done);
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
