// body's budgets and fault runs: make, parse, line, content_digest and
// check on every fixture entry's body within the entry checks' 256 KiB plus
// 8 times the record, counted by the arena; a byte short a MemoryError
// budget with nothing written; and each allocation of each failed in turn,
// a typed error with nothing held. The key takes no arena. The runner's
// count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/body.hpp"
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

namespace body = qiven3::entry::body;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;
using header::Slot;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using Fake = qiven3::platform::fake::FakePlatform;
using Body = body::Body<Fake, Allocator<char>>;

constexpr std::size_t budget_for(std::size_t record) { return 262144 + 8 * record; }

bool budget_error(const MemoryError& e) { return e.code == MemoryCode::budget; }

// An entry with a body: its content, key, header line and record.
struct Entry {
    std::string content;
    std::string key;
    std::string header;
    std::string record;
};

std::vector<Entry> entries(Fake& fake) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<Entry> out;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        if (!e.key || !e.content) {
            continue;
        }
        const header::Header<> h = header::parse_line(e.header);
        Entry x{std::string(*e.content), std::string(*e.key), std::string(e.header), {}};
        body::make(x.content, digest::key_from_hex(fake, x.key), h.value(Slot::id)).line(x.record);
        out.push_back(std::move(x));
    }
    return out;
}

// The peaks of one body's operations, each its own beyond what it held.
struct Peaks {
    std::size_t make = 0;
    std::size_t parse = 0;
    std::size_t line = 0;
    std::size_t digest = 0;
};

}  // namespace

QIVEN3_CASE(body, budget) {
    Fake fake(qiven3::platform::fake::Config{});
    Peaks most;
    std::size_t longest = 0;
    for (const Entry& x : entries(fake)) {
        const header::Header<> h = header::parse_line(x.header);
        const std::string_view id = h.value(Slot::id);
        const std::size_t budget = budget_for(x.record.size());
        Peaks peaks;
        {
            Arena& arena = qiven3_ctx.counted.arena(budget);
            const Body b = body::make(x.content, digest::key_from_hex(fake, x.key), id, Allocator<char>(arena));
            peaks.make = arena.peak();
            CHECK_EQ(arena.used(), b.bytes() + 1);  // the text's terminator
            const std::size_t held = arena.used();
            const std::size_t peak = arena.peak();
            body::check(b, h);
            CHECK_EQ(arena.used(), held);
            CHECK_EQ(arena.peak(), peak);  // check takes none
            String out{Allocator<char>(arena)};
            b.line(out);
            CHECK_EQ(std::string(out), x.record);
            peaks.line = arena.used() - held;
            String d{Allocator<char>(arena)};
            b.content_digest(d);
            CHECK_EQ(std::string(d), std::string(h.value(Slot::content)));
            peaks.digest = arena.used() - held - peaks.line;
        }
        {
            Arena& arena = qiven3_ctx.counted.arena(budget);
            const Body b = body::parse(fake, x.record, Allocator<char>(arena));
            peaks.parse = arena.peak();
            CHECK_EQ(arena.used(), b.bytes() + 1);
        }
        CHECK(peaks.make <= budget);
        CHECK(peaks.parse <= budget);
        CHECK(peaks.make + peaks.line + peaks.digest <= budget);
        most.make = peaks.make > most.make ? peaks.make : most.make;
        most.parse = peaks.parse > most.parse ? peaks.parse : most.parse;
        most.line = peaks.line > most.line ? peaks.line : most.line;
        most.digest = peaks.digest > most.digest ? peaks.digest : most.digest;
        longest = x.record.size() > longest ? x.record.size() : longest;
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        // A byte short of each peak: MemoryError budget.
        bool make_refused = false;
        try {
            Arena& shorter = qiven3_ctx.counted.arena(peaks.make - 1);
            static_cast<void>(body::make(x.content, digest::key_from_hex(fake, x.key), id, Allocator<char>(shorter)));
        } catch (const MemoryError& e) {
            make_refused = budget_error(e);
        }
        CHECK(make_refused);
        bool parse_refused = false;
        try {
            Arena& shorter = qiven3_ctx.counted.arena(peaks.parse - 1);
            static_cast<void>(body::parse(fake, x.record, Allocator<char>(shorter)));
        } catch (const MemoryError& e) {
            parse_refused = budget_error(e);
        }
        CHECK(parse_refused);
        // line writes into the caller's string, on its own arena here.
        Arena& whole = qiven3_ctx.counted.arena(budget);
        const Body b = body::make(x.content, digest::key_from_hex(fake, x.key), id, Allocator<char>(whole));
        Arena& shorter = qiven3_ctx.counted.arena(peaks.line - 1);
        String out{Allocator<char>(shorter)};
        bool line_refused = false;
        try {
            b.line(out);
        } catch (const MemoryError& e) {
            line_refused = budget_error(e);
        }
        CHECK(line_refused);
        CHECK(out.empty());
#endif
    }
    std::cout << "body.budget: longest record " << longest << " bytes, budget " << budget_for(longest)
              << "; peaks: make " << most.make << ", parse " << most.parse << ", line " << most.line
              << ", content_digest " << most.digest << ", check 0\n";
}

QIVEN3_CASE(body, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    Fake fake(qiven3::platform::fake::Config{});
    const std::vector<Entry> all = entries(fake);
    const Entry& x = all.front();
    const header::Header<> h = header::parse_line(x.header);
    const std::string_view id = h.value(Slot::id);
    std::vector<std::byte> pages(budget_for(x.record.size()));
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    bool done = false;
    const auto report = [&](std::string_view op, const qiven3::test::FaultRuns& runs, std::size_t want) {
        std::cout << "body.faults: " << op << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, want);
        CHECK_EQ(runs.completed_at, want + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        done = false;
    };
    // canon's four, then the body's text.
    const auto make = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        const Body b = body::make(x.content, digest::key_from_hex(fake, x.key), id, Allocator<char>(*arena));
        done = b.id() == id;
    };
    report("make", qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "body.make", countdown, make, empty), 5);
    const auto parse = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        const Body b = body::parse(fake, x.record, Allocator<char>(*arena));
        done = b.content() == x.content;
    };
    report("parse", qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "body.parse", countdown, parse, empty), 5);
    // The record's one, and the digest's text's one.
    const auto line = [&] {
        arena.emplace(pages);
        const Body b = body::make(x.content, digest::key_from_hex(fake, x.key), id, Allocator<char>(*arena));
        arena->attach(&countdown);
        String out{Allocator<char>(*arena)};
        b.line(out);
        done = std::string(out) == x.record;
    };
    report("line", qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "body.line", countdown, line, empty), 1);
    const auto digest_of = [&] {
        arena.emplace(pages);
        const Body b = body::make(x.content, digest::key_from_hex(fake, x.key), id, Allocator<char>(*arena));
        arena->attach(&countdown);
        String out{Allocator<char>(*arena)};
        b.content_digest(out);
        body::check(b, h);
        done = std::string(out) == std::string(h.value(Slot::content));
    };
    report("content_digest",
           qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "body.content_digest", countdown, digest_of, empty), 1);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
