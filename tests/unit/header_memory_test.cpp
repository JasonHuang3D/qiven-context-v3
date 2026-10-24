// header's budgets and fault runs: parse_line, check, line and make on
// every fixture entry's header within the entry checks' 256 KiB plus 8
// times the line, counted by the arena; a byte short a MemoryError budget
// with nothing written; and each allocation of each failed in turn, a
// typed error with nothing held. The runner's count after each case holds
// too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using header::FieldsView;
using header::Slot;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using Header = header::Header<Allocator<char>>;

constexpr std::size_t budget_for(std::size_t line) { return 262144 + 8 * line; }

bool budget_error(const MemoryError& e) { return e.code == MemoryCode::budget; }

// A header's fields, viewing its text.
template <typename A>
FieldsView fields_of(const header::Header<A>& h) {
    FieldsView f;
    f.tag = *h.text(Slot::tag);
    f.scope = *h.text(Slot::scope);
    f.lane = *h.text(Slot::lane);
    f.seq = h.seq();
    f.parent = h.text(Slot::parent);
    f.kind = *h.text(Slot::kind);
    f.writer = *h.text(Slot::writer);
    f.basis = *h.text(Slot::basis);
    f.at = *h.text(Slot::at);
    f.target = h.text(Slot::target);
    f.content = h.text(Slot::content);
    f.id = h.text(Slot::id);
    return f;
}

// The peaks of one header's operations, each its own beyond what it held.
struct Peaks {
    std::size_t parse = 0;
    std::size_t check = 0;
    std::size_t line = 0;
    std::size_t make = 0;
};

std::vector<std::string> lines() {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::vector<std::string> out;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        out.emplace_back(fixtures::entry(v, i).header);
    }
    return out;
}

}  // namespace

QIVEN3_CASE(header, budget) {
    std::size_t most_parse = 0;
    std::size_t most_check = 0;
    std::size_t most_line = 0;
    std::size_t longest = 0;
    for (const std::string& text : lines()) {
        const header::Header<> plain = header::parse_line(text);
        const FieldsView f = fields_of(plain);
        const std::size_t budget = budget_for(text.size());
        Peaks peaks;
        {
            Arena& arena = qiven3_ctx.counted.arena(budget);
            const Header h = header::parse_line(text, Allocator<char>(arena));
            peaks.parse = arena.peak();
            CHECK_EQ(arena.used(), h.bytes() + 1);  // the text's terminator
        }
        {
            Arena& arena = qiven3_ctx.counted.arena(budget);
            const Header h = header::make(f, Allocator<char>(arena));
            peaks.make = arena.peak();
            const std::size_t held = arena.used();
            CHECK_EQ(held, h.bytes() + 1);
            CHECK(!header::try_check(h));
            peaks.check = arena.peak() - held;
            CHECK_EQ(arena.used(), held);
        }
        {
            Arena& arena = qiven3_ctx.counted.arena(budget);
            const Header h = header::make(f, Allocator<char>(arena));
            const std::size_t held = arena.used();
            String out{Allocator<char>(arena)};
            h.line(out);
            CHECK_EQ(std::string(out), text);
            peaks.line = arena.peak() - held;
        }
        // check's compiles, five at most, each within matcher's budget.
        CHECK(peaks.check <= 5 * qiven3::entry::matcher::compile_budget);
        CHECK(peaks.parse <= budget);
        CHECK(peaks.make + peaks.check <= budget);
        CHECK(peaks.make + peaks.line <= budget);
        most_parse = peaks.parse > most_parse ? peaks.parse : most_parse;
        most_check = peaks.check > most_check ? peaks.check : most_check;
        most_line = peaks.line > most_line ? peaks.line : most_line;
        longest = text.size() > longest ? text.size() : longest;
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        // A byte short of each peak: MemoryError budget.
        bool parse_refused = false;
        try {
            Arena& shorter = qiven3_ctx.counted.arena(peaks.parse - 1);
            static_cast<void>(header::parse_line(text, Allocator<char>(shorter)));
        } catch (const MemoryError& e) {
            parse_refused = budget_error(e);
        }
        CHECK(parse_refused);
        bool check_refused = false;
        try {
            Arena& shorter = qiven3_ctx.counted.arena(peaks.make + peaks.check - 1);
            const Header h = header::make(f, Allocator<char>(shorter));
            static_cast<void>(header::try_check(h));
        } catch (const MemoryError& e) {
            check_refused = budget_error(e);
        }
        CHECK(check_refused);
        Arena& shorter = qiven3_ctx.counted.arena(peaks.make + peaks.line - 1);
        const Header h = header::make(f, Allocator<char>(shorter));
        String out{Allocator<char>(shorter)};
        out = "kept";
        bool line_refused = false;
        try {
            h.line(out);
        } catch (const MemoryError& e) {
            line_refused = budget_error(e);
        }
        CHECK(line_refused);
        CHECK_EQ(std::string(out), "kept");
#endif
    }
    std::cout << "header.budget: longest line " << longest << " bytes, budget " << budget_for(longest)
              << "; peaks: parse_line " << most_parse << ", check " << most_check << ", line " << most_line
              << "; compile_budget " << qiven3::entry::matcher::compile_budget << "\n";
}

QIVEN3_CASE(header, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const std::vector<std::string> all = lines();
    const std::string& text = all.front();
    const header::Header<> plain = header::parse_line(text);
    const FieldsView f = fields_of(plain);
    std::vector<std::byte> pages(budget_for(text.size()));
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto empty = [&](std::size_t) { return arena->used() == 0; };
    bool done = false;
    const auto report = [&](std::string_view op, const qiven3::test::FaultRuns& runs, std::size_t want) {
        std::cout << "header.faults: " << op << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, want);
        CHECK_EQ(runs.completed_at, want + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK(done);
        done = false;
    };
    // canon's four, then the header's text.
    const auto parse = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        const Header h = header::parse_line(text, Allocator<char>(*arena));
        done = h.present(Slot::id);
    };
    report("parse_line", qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "header.parse_line", countdown, parse, empty), 5);
    // Two for each of five syntaxes: scope, lane, writer, the id's, content.
    const auto check = [&] {
        arena.emplace(pages);
        const Header h = header::make(f, Allocator<char>(*arena));
        arena->attach(&countdown);
        done = !header::try_check(h);
    };
    report("check", qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "header.check", countdown, check, empty), 10);
    // The value's nodes and text, then canon's sorted members and bytes.
    const auto line = [&] {
        arena.emplace(pages);
        const Header h = header::make(f, Allocator<char>(*arena));
        arena->attach(&countdown);
        String out{Allocator<char>(*arena)};
        h.line(out);
        done = std::string(out) == text;
    };
    report("line", qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "header.line", countdown, line, empty), 4);
    // The header's text alone.
    const auto make = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        const Header h = header::make(f, Allocator<char>(*arena));
        done = h.bytes() > 0;
    };
    report("make", qiven3::test::run_faults<MemoryError>(qiven3_ctx.log, "header.make", countdown, make, empty), 1);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}
