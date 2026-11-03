// matcher's own cases: the engine's vectors and compile's faults, evaluated
// by the compiler on std::allocator. A case over a caller's patterns is the
// caller's.

#include <cstddef>
#include <optional>
#include <string_view>

#include "qiven/matcher/matcher.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace matcher = qiven::matcher;
using matcher::Fault;

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

}  // namespace

QIVEN_CASE(matcher, vectors) {
    // The static assertions above held, or this binary was not built.
    CHECK(hits("(ab|c)+d", "xababcd"));
}
