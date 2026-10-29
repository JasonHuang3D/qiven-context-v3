// write/entries' budgets and fault runs over synthetic files. A read takes
// its content checks' work span from the arena and gives it back: once the
// file is read the arena holds the file alone, nothing once the file ends,
// and nothing after a refusal, of the form or of a content, nor after an
// arena too small for the span. Each allocation, the caller's arena's and
// each content check's, failed in turn, is a MemoryError with nothing held
// once the read ends, never a refusal; the run completes past the last,
// which counts them exactly, by MSVC's library, libstdc++ and libc++: a
// write file of three entries with a piece reference and three
// placeholders 86, 73 and 70, an import file of two 53, 43 and 43, a write
// file whose second content is invalid 62, 52 and 50, with its refusal. The runner's count after
// each case holds too.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "write/entries.hpp"
#include "write/entries_reader.hpp"

namespace {

namespace we = qiven3::write::entries;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

constexpr std::size_t budget = std::size_t{8} << 20U;

#define ADMIT R"("admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."})"

const std::string write_file = R"json({
  "form": "qiven3-entries/1", "writer": "implementer/s7", "scope": "project:example",
  "episode": {"client": "claude-code", "session": "synthetic-session-7"},
  "pieces": {"record": {"text": "progress: unit one built\n"}},
  "entries": [
    {"name": "moved", "kind": "claim", "basis": "claimed",
     "content": {"claim": "An invented claim.", "subject": "entity:example", "band": "normal", )json" ADMIT R"json(,
       "evidence": [{"type": "output", "command": "an invented command", "piece": "record"}]}},
    {"kind": "claim", "basis": "claimed",
     "content": {"claim": "Another invented claim.", "subject": "entity:example", "band": "low", )json" ADMIT R"json(,
       "evidence": [{"type": "entry", "id": "@name:moved"}]}},
    {"kind": "retire", "basis": "claimed", "target": "@question:unit-one-shape",
     "content": {"reason": "Settled in review.", "evidence": [{"type": "entry", "id": "@name:moved"}]}}
  ]
})json";

const std::string import_file = R"json({
  "form": "qiven3-entries/1", "writer": "import", "scope": "project:example",
  "entries": [
    {"kind": "claim", "basis": "claimed",
     "content": {"claim": "An invented claim.", "subject": "entity:example", "band": "normal", )json" ADMIT R"json(,
       "evidence": [{"type": "entry", "id": "sha256:1111111111111111111111111111111111111111111111111111111111111111"}]}},
    {"name": "elsewhere", "scope": "project:other", "kind": "claim", "basis": "claimed",
     "content": {"claim": "Another invented claim.", "subject": "entity:example", "band": "low", )json" ADMIT R"json(,
       "evidence": [{"type": "entry", "id": "sha256:1111111111111111111111111111111111111111111111111111111111111111"}]}}
  ]
})json";

std::string swap(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    return at == std::string::npos ? "missing: " + std::string(from) : text.replace(at, from.size(), to);
}

// The second content's band off its set: refused invalid at entry 1.
const std::string invalid_file = swap(write_file, R"("band": "low")", R"("band": "loud")");
// A member the form does not hold: refused before any content check.
const std::string form_file = swap(write_file, R"("writer")", R"("author": 1, "writer")");

// The parse and the stand-ins' writer grow their values unreserved, so a
// read's count follows the library's growth: MSVC's, libstdc++'s, libc++'s.
constexpr std::size_t grown(std::size_t msvc, std::size_t libstdcxx, std::size_t libcxx) {
    static_cast<void>(msvc);
    static_cast<void>(libstdcxx);
    static_cast<void>(libcxx);
#if defined(_MSVC_STL_VERSION)
    return msvc;
#elif defined(_LIBCPP_VERSION)
    return libcxx;
#else
    return libstdcxx;
#endif
}

struct Case {
    std::string_view name;
    const std::string* bytes;
    std::size_t allocations;
    bool refused;
};

// The refusal of bytes over arena, empty for none.
std::optional<we::EntriesError> refusal(std::string_view bytes, Arena& arena, we::Hooks hooks = {}) {
    try {
        static_cast<void>(we::read(bytes, arena, hooks));
        return std::nullopt;
    } catch (const we::EntriesError& e) {
        return e;
    }
}

}  // namespace

QIVEN3_CASE(write_entries, budget) {
    for (const std::string* bytes : {&write_file, &import_file}) {
        Arena& arena = qiven3_ctx.counted.arena(budget);
        {
            const we::File f = we::read(*bytes, arena);
            CHECK(!f.entries().empty());
            CHECK(arena.peak() >= we::detail::work_bytes);
            CHECK(arena.used() > 0);
            CHECK(arena.used() < we::detail::work_bytes);
            std::cout << "write_entries.budget: " << f.entries().size() << " entries held in " << arena.used()
                      << " bytes, the read's peak " << arena.peak() << "\n";
        }
        CHECK_EQ(arena.used(), 0U);
    }
}

QIVEN3_CASE(write_entries, refusal_gives_back) {
    for (const std::string* bytes : {&invalid_file, &form_file}) {
        Arena& arena = qiven3_ctx.counted.arena(budget);
        const std::optional<we::EntriesError> e = refusal(*bytes, arena);
        REQUIRE(e.has_value());
        CHECK(e->code == (bytes == &invalid_file ? we::Code::invalid : we::Code::form));
        CHECK_EQ(arena.used(), 0U);
    }
    // An arena too small for the work span: MemoryError, nothing held.
    Arena& small = qiven3_ctx.counted.arena(we::detail::work_bytes);
    bool budget_error = false;
    try {
        static_cast<void>(refusal(write_file, small));
    } catch (const MemoryError&) {
        budget_error = true;
    }
    CHECK(budget_error);
    CHECK_EQ(small.used(), 0U);
}

QIVEN3_CASE(write_entries, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    std::optional<we::EntriesError> refused;
    bool done = false;
    // The file, its value and the work span given back once the read ends.
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    const std::array<Case, 3> all = {
        Case{"a write file", &write_file, grown(86, 73, 70), false},
        Case{"an import file", &import_file, grown(53, 43, 43), false},
        Case{"an invalid content", &invalid_file, grown(62, 52, 50), true},
    };
    for (const Case& c : all) {
        const auto op = [&] {
            arena.emplace(pages);
            arena->attach(&countdown);
            refused = refusal(*c.bytes, *arena, we::Hooks{.work = &countdown});
            arena->attach(nullptr);
            done = true;
        };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "write_entries.read", countdown, op, empty);
        std::cout << "write_entries.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, c.allocations);
        CHECK_EQ(runs.completed_at, c.allocations + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        CHECK_EQ(refused.has_value(), c.refused);
        if (c.refused && refused) {
            CHECK(refused->code == we::Code::invalid);
            CHECK_EQ(refused->entry, 1U);
        }
        done = false;
    }
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
