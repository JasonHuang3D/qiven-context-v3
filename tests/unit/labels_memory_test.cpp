// labels' budgets and fault run: label, version_of and check_of take no
// allocation. Over every fixture entry's header and content, and D twice
// with a document of record (a short path, and the longest path in
// four-byte characters), held in an arena, each check or accept read and
// the label of each version with all of them and with none (the two
// quoting their document of record) leave the arena's use and peak where
// they were; with the arena's
// countdown armed at its first allocation, they complete, no call failed.
// The runner's count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "entry/labels.hpp"
#include "entry/refs.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace header = qiven3::entry::header;
namespace labels = qiven3::entry::labels;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;

constexpr std::size_t budget = 1048576;

struct Stored {
    std::string header;
    std::string content;
};

// D's content with a document of record of path in its source.
std::string recorded(std::string_view content, std::string_view path) {
    std::string body(content);
    const std::size_t at = body.find("\"subject\"");
    if (at == std::string::npos) {
        throw std::runtime_error("no subject in D");
    }
    body.insert(at, R"("source":[{"digest":"d1","path":")" + std::string(path) +
                        R"(","repository":"r","revision":"0123456789abcdef0123456789abcdef01234567","type":"repo"}],)");
    return body;
}

// Every fixture entry, then D twice more with a document of record: a
// short path, and a path of refs' limit in four-byte characters.
std::vector<Stored> stored() {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<Stored> out;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        out.push_back(Stored{std::string(e.header), std::string(e.content.value_or(""))});
    }
    const fixtures::EntryView d = fixtures::find_entry(v, "D").value();
    std::string wide;
    for (std::size_t i = 0; i < qiven3::entry::refs::path_limit; ++i) {
        wide += "\xF0\x9F\x98\x80";
    }
    out.push_back(Stored{std::string(d.header), recorded(d.content.value(), "notes/record.md")});
    out.push_back(Stored{std::string(d.header), recorded(d.content.value(), wide)});
    return out;
}

struct Parsed {
    header::Header<Allocator<char>> h;
    canon::Basic<Allocator<char>> v;
};

std::vector<Parsed> parsed(const std::vector<Stored>& all, Arena& arena) {
    std::vector<Parsed> out;
    for (const Stored& s : all) {
        canon::Basic<Allocator<char>> content{Allocator<char>(arena)};
        if (!s.content.empty()) {
            canon::parse(s.content, content);
        }
        out.push_back(Parsed{header::parse_line(s.header, Allocator<char>(arena)), std::move(content)});
    }
    return out;
}

// The labels made, and those quoting a document of record.
struct Made {
    std::size_t labels = 0;
    std::size_t quoted = 0;
};

// Each check and accept read, then each version's label with all of them,
// and with none: ACC2's binding of D hides its document of record.
Made label_all(const std::vector<Parsed>& all, std::vector<labels::CheckView>& checks) {
    checks.clear();
    for (const Parsed& p : all) {
        const std::optional<labels::CheckView> c = labels::check_of(p.h, p.v, true);
        if (c) {
            checks.push_back(*c);
        }
    }
    Made made;
    for (const Parsed& p : all) {
        const std::optional<labels::VersionView> v = labels::version_of(p.h, p.v);
        if (v) {
            made.labels += labels::label(*v, checks).text().empty() ? 0U : 1U;
            made.quoted += labels::label(*v, {}).text().starts_with("the owner's words as quoted in ") ? 1U : 0U;
        }
    }
    return made;
}

}  // namespace

QIVEN3_CASE(labels, budget) {
    const std::vector<Stored> all = stored();
    Arena& arena = qiven3_ctx.counted.arena(budget);
    const std::vector<Parsed> entries = parsed(all, arena);
    std::vector<labels::CheckView> checks;
    checks.reserve(entries.size());
    const std::size_t used = arena.used();
    const std::size_t peak = arena.peak();
    const Made made = label_all(entries, checks);
    CHECK_EQ(arena.used(), used);
    CHECK_EQ(arena.peak(), peak);
    std::cout << "labels.budget: " << made.labels << " labels, " << made.quoted
              << " quoting a document of record, over " << checks.size() << " checks and accepts, 0 allocations\n";
    CHECK_EQ(made.labels, all.size());
    CHECK_EQ(made.quoted, 2U);
    CHECK_EQ(checks.size(), 3U);
}

QIVEN3_CASE(labels, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::vector<Stored> all = stored();
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    std::vector<labels::CheckView> checks;
    checks.reserve(all.size());
    Made made;
    const auto op = [&] {
        arena.emplace(pages);
        const std::vector<Parsed> entries = parsed(all, *arena);
        arena->attach(&countdown);
        made = label_all(entries, checks);
        arena->attach(nullptr);
    };
    const auto empty = [&](std::size_t) { return true; };
    const qiven3::test::FaultRuns runs =
        qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "labels.label", countdown, op, empty);
    std::cout << "labels.faults: label's allocations " << runs.typed << ", completed at " << runs.completed_at << "\n";
    CHECK_EQ(runs.typed, 0U);
    CHECK_EQ(runs.completed_at, 1U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(made.labels, all.size());
    CHECK_EQ(made.quoted, 2U);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
