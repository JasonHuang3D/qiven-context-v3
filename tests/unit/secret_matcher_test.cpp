// The matcher over its callers' patterns, secret's: the syntaxes' vectors,
// copies of the entry's, evaluated by the compiler on std::allocator; the
// secret check's twelve patterns on their tables' fixtures and the clearing
// fixtures (support/fixtures), each built when the case runs so that no file
// holds a live-looking token; a string that makes a backtracking engine
// square its time, read in linear time; compile's budget over them, its two
// allocations failed in turn, and hit's none. The engine's own vectors and
// compile's faults are matcher's (foundation/core/matcher/tests/).

#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/secret.hpp"
#include "qiven/matcher/matcher.hpp"
#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/faults/faults.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fixtures/patterns.hpp"

namespace {

namespace matcher = qiven::matcher;
namespace secret = qiven3::entry::secret;
namespace fixtures = qiven::test::fixtures;
using matcher::Fault;
using qiven::memory::Allocator;
using qiven::memory::Arena;
using qiven::memory::MemoryCode;
using qiven::memory::MemoryError;

constexpr bool hits(std::string_view pattern, std::string_view text) {
    return matcher::hit(matcher::compile(pattern), text);
}

constexpr std::optional<Fault> fault_of(std::string_view pattern) {
    matcher::Basic<> out;
    const std::optional<matcher::PatternError> fault = matcher::try_compile(pattern, out);
    return fault ? std::optional<Fault>(fault->code) : std::nullopt;
}

// The syntaxes, whole-value.
constexpr std::string_view lane = "^[a-z][a-z0-9-]{0,31}$";
constexpr std::string_view number = "^[1-9][0-9]{0,8}$";
constexpr std::string_view key_syntax = "^[a-z][a-z0-9]*(_[a-z0-9]+)*$";
constexpr std::string_view case_id = "^[A-Z][A-Z0-9]*(-[A-Z0-9]+)*$";
constexpr std::string_view symbol = "^[A-Za-z_][A-Za-z0-9_]*((\\.|::|/)[A-Za-z_][A-Za-z0-9_]*)*$";
static_assert(hits(lane, "writer") && hits(lane, "a-") && !hits(lane, "Writer") && !hits(lane, ""));
static_assert(hits(lane, "a234567890123456789012345678901b") && !hits(lane, "a234567890123456789012345678901bc"));
static_assert(!hits(lane, "writer\n") && !hits(lane, "1writer") && !hits(lane, "wr ter"));
static_assert(hits(number, "1") && hits(number, "123456789") && !hits(number, "1234567890") && !hits(number, "0"));
static_assert(hits(key_syntax, "a_b") && hits(key_syntax, "ab1_c2_d") && !hits(key_syntax, "a__b") && !hits(key_syntax, "a_") && !hits(key_syntax, "_a"));
static_assert(hits(case_id, "FA2") && hits(case_id, "ACC-2") && !hits(case_id, "FA--2") && !hits(case_id, "fa2"));
static_assert(hits(symbol, "a::b/c.d") && hits(symbol, "_x") && !hits(symbol, "a:b") && !hits(symbol, "a..b"));

static_assert(!fault_of(symbol));

std::string rep(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

// The secret check's twelve patterns, in its table's order.
std::vector<std::string_view> patterns() {
    std::vector<std::string_view> out;
    for (const secret::Pattern& p : secret::PATTERNS) {
        out.push_back(p.text.view());
    }
    return out;
}

bool budget_error(const MemoryError& e) { return e.code == MemoryCode::budget; }

}  // namespace

QIVEN_CASE(matcher, patterns) {
    std::vector<matcher::Basic<>> compiled;
    for (const std::string_view p : patterns()) {
        compiled.push_back(matcher::compile(p));
        CHECK(!compiled.back().whole());
    }
    std::size_t checked = 0;
    for (const fixtures::RecipeView& f : fixtures::recipes) {
        const bool got = matcher::hit(compiled[f.pattern], fixtures::text(f));
        if (got != f.hit) {
            std::cout << "matcher.patterns: pattern " << f.pattern << " gave " << got << " on fixture " << checked
                      << "\n";
        }
        CHECK_EQ(got, f.hit);
        ++checked;
    }
    std::cout << "matcher.patterns: " << compiled.size() << " patterns, " << checked << " fixtures\n";
    CHECK_EQ(compiled.size(), 12U);
}

QIVEN_CASE(matcher, linear) {
    // url-password over a://, 30,000 x, : and 30,000 p, with no @: no hit.
    const matcher::Basic<> url = matcher::compile(patterns()[7]);
    for (const std::size_t n : {std::size_t{10000}, std::size_t{30000}}) {
        const std::string text = "a://" + rep("x", n) + ":" + rep("p", n);
        const auto begun = std::chrono::steady_clock::now();
        const bool got = matcher::hit(url, text);
        const auto took =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begun).count();
        std::cout << "matcher.linear: " << text.size() << " bytes, " << url.positions() << " positions, " << took
                  << " ms\n";
        CHECK(!got);
        CHECK(took < 2000);
        CHECK(matcher::hit(url, text + "@"));
    }
}

QIVEN_CASE(matcher, compile_budget) {
    std::vector<std::string_view> all = patterns();
    for (const std::string_view s : {lane, number, key_syntax, case_id, symbol}) {
        all.push_back(s);
    }
    all.emplace_back("^[A-Za-z0-9][A-Za-z0-9._/-]{0,199}$");
    all.emplace_back("[a-z]{255}");
    std::size_t largest = 0;
    for (const std::string_view p : all) {
        Arena& arena = qiven_ctx.counted.arena(matcher::compile_budget);
        std::size_t peak = 0;
        {
            matcher::Automaton a{Allocator<char>(arena)};
            matcher::try_compile(p, a);
            peak = arena.peak();
            CHECK(a.compiled());
            CHECK_EQ(a.bytes(), matcher::automaton_bytes(a.positions()));
            const std::size_t held = arena.used();
            CHECK(!matcher::hit(a, "\t"));
            CHECK_EQ(arena.used(), held);
            CHECK_EQ(arena.peak(), peak);
        }
        CHECK(peak <= matcher::compile_budget);
        CHECK_EQ(arena.used(), 0U);
        largest = peak > largest ? peak : largest;
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        // A byte short: MemoryError budget, the caller's automaton unchanged.
        Arena& shorter = qiven_ctx.counted.arena(peak - 1);
        matcher::Automaton a{Allocator<char>(shorter)};
        bool refused = false;
        try {
            matcher::try_compile(p, a);
        } catch (const MemoryError& e) {
            refused = budget_error(e);
        }
        CHECK(refused);
        CHECK(!a.compiled());
#endif
    }
    std::cout << "matcher.compile_budget: " << all.size() << " patterns, largest peak " << largest << ", budget "
              << matcher::compile_budget << ", scratch " << sizeof(matcher::detail::Scratch) << "\n";
}

QIVEN_CASE(matcher, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(matcher::compile_budget + 4096);
    qiven::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    bool compiled = false;
    const auto compile = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        const matcher::Automaton a = matcher::compile(patterns()[9], Allocator<char>(*arena));
        compiled = a.compiled() && matcher::hit(a, "Authorization: Bearer " + rep("a", 8));
    };
    const qiven::test::FaultRuns runs =
        qiven::test::run_faults<MemoryError>(qiven_ctx.log, "matcher.compile", countdown, compile, check);
    std::cout << "matcher.faults: compile's allocations " << runs.typed << ", completed at " << runs.completed_at
              << "\n";
    // The scratch, then the automaton's words.
    CHECK_EQ(runs.typed, 2U);
    CHECK_EQ(runs.completed_at, 3U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK(compiled);
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
