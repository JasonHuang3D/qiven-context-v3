// matcher's own cases, over patterns it writes itself: the engine's vectors
// and compile's faults, evaluated by the compiler on std::allocator; a
// string that makes a backtracking engine square its time, read in linear
// time; compile's budget over the bounds' patterns, its two allocations
// failed in turn, and hit's none. A case over a caller's patterns is the
// caller's.

#include <chrono>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven/faults/faults.hpp"
#include "qiven/matcher/matcher.hpp"
#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace matcher = qiven::matcher;
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

constexpr std::size_t fault_at(std::string_view pattern) {
    matcher::Basic<> out;
    return matcher::try_compile(pattern, out).value_or(matcher::PatternError{Fault::syntax, 9999}).at;
}

// The engine: alternation under a loop, counts, the empty string, search
// against whole-value, the lookbehind and bytes past ASCII.
static_assert(hits("(ab|c)+d", "xababcd") && !hits("(ab|c)+d", "xabad") && hits("^(ab|c)+d$", "abccd"));
static_assert(hits("^a{2,3}$", "aaa") && !hits("^a{2,3}$", "aaaa") && !hits("^a{2,3}$", "a") && hits("a{2,3}", "aaaa"));
static_assert(hits("^(?:x|)$", "") && hits("^$", "") && !hits("^$", "a") && hits("", "abc") && hits("a*", ""));
static_assert(hits("^a{0}b$", "b") && !hits("^a{0}b$", "ab") && hits("^(?:ab){2,}$", "ababab") && !hits("^(?:ab){2,}$", "ab"));
static_assert(hits("(?<![a-z])b", "b") && hits("(?<![a-z])b", "ab b") && !hits("(?<![a-z])b", "ab"));
static_assert(hits("(?<![a-z])b", "\xe4\xb8\xad" "b") && hits("^[^a]+$", "\xe4\xb8\xad") && !hits("^[^x]$", "x"));
static_assert(hits("[\\t ]x", "\tx") && hits("a\\|b", "a|b") && !hits("a\\|b", "a") && hits("[]a]", "]"));
static_assert(hits("[a-]", "-") && hits("[-a]", "-") && hits("^[^ \\t\\r\\n\"'$<%{-]$", "q") && !hits("^[^-]$", "-"));

// compile's faults and where each is found.
static_assert(fault_of("a**") == Fault::syntax && fault_of("*a") == Fault::syntax && fault_of("a{2,1}") == Fault::syntax);
static_assert(fault_of("(a") == Fault::syntax && fault_of("a)") == Fault::syntax && fault_of("[a") == Fault::syntax);
static_assert(fault_of("[z-a]") == Fault::syntax && fault_of("\\") == Fault::syntax && fault_of("a|*") == Fault::syntax);
static_assert(fault_of(".") == Fault::unsupported && fault_of("\\d") == Fault::unsupported);
static_assert(fault_of("a*?") == Fault::unsupported && fault_of("a{,3}") == Fault::unsupported);
static_assert(fault_of("(?=a)") == Fault::unsupported && fault_of("x(?<![a])") == Fault::unsupported);
static_assert(fault_of("^(?<![a])b$") == Fault::unsupported && fault_of("(?<!a)b") == Fault::unsupported);
static_assert(fault_of("^a") == Fault::unsupported && fault_of("a$") == Fault::unsupported && fault_of("a^") == Fault::unsupported);
static_assert(fault_of("\xc3\xa9") == Fault::unsupported && fault_of("[[a]") == Fault::unsupported && fault_of("a{") == Fault::unsupported);
static_assert(fault_of("a{256}") == Fault::size && fault_of("[a-z]{300}") == Fault::size && fault_of("(?:ab){129}") == Fault::size);
static_assert(fault_of("(((((((((a)))))))))") == Fault::size && !fault_of("((((((((a))))))))"));
static_assert(!fault_of("a{255}") && !fault_of("(?:ab){128}"));
static_assert(fault_at("ab)") == 2 && fault_at("ab(cd") == 2 && fault_at("[ab") == 0 && fault_at("a{2,1}") == 1);

std::string rep(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

bool budget_error(const MemoryError& e) { return e.code == MemoryCode::budget; }

}  // namespace

QIVEN_CASE(matcher, vectors) {
    // The static assertions above held, or this binary was not built.
    CHECK(hits("(ab|c)+d", "xababcd"));
}

QIVEN_CASE(matcher, linear) {
    // A scheme, ://, a user and : then a password and @: over a://, 30,000 x,
    // : and 30,000 p, with no @, no hit.
    const matcher::Basic<> url = matcher::compile("[a-z]+://[^/@:]*:[^/@]+@");
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
    // The bounds' patterns: the most copies of one byte and of one class,
    // max_positions in pairs and in a whole value, max_depth groups, and the
    // lookbehind.
    const std::vector<std::string_view> all = {
        "a{255}", "[a-z]{255}", "(?:ab){128}", "((((((((a))))))))", "^[a-z][a-z0-9-]{0,255}$",
        "(?<![a-z])b[a-z]{0,200}", "(ab|c)+d"};
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
        const matcher::Automaton a = matcher::compile("(ab|c)+d", Allocator<char>(*arena));
        compiled = a.compiled() && matcher::hit(a, "x" + rep("ab", 8) + "cd");
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
