#pragma once

// matcher: the patterns' and syntaxes' matcher, linear in the string.
// compile reads a pattern of Python's re, in the subset the syntaxes and
// the secret check's patterns write, into a position automaton; hit says
// whether it matches a string's bytes: anywhere, as re.search does; or, a
// pattern printed ^...$, the whole value, as re.fullmatch does with what
// lies between. The subset: printable ASCII literals; a backslash before
// punctuation, and \t, \n, \r; bracket classes, with ranges and ^; groups,
// ( ) and (?: ); |; the greedy quantifiers *, +, ?, {n}, {n,} and {n,m};
// and, first in a search pattern alone, one negative lookbehind of one
// class, (?<![...]), checked against the byte before each start. Anything
// else is a PatternError: syntax where re refuses it too, unsupported where
// re takes it and the matcher does not, size past the bounds below.
//
// The automaton has no empty moves: each position is one class of the
// pattern, a {n,m} written out in copies, and a step from a set of
// positions over a byte is the union of their followers, the first
// positions added where a match may start, masked by the byte's positions.
// So hit takes at most max_positions times max_words word operations a
// byte, and allocates nothing. It reads bytes: a negated class holds every
// byte from 0x80, as it holds every non-ASCII code point. compile takes two
// allocations, its scratch and the automaton (compile_budget). Neither
// function recurses: compile keeps its open groups in a fixed array of
// max_depth + 1. Both are constexpr and templates on the allocator, so their
// vectors run in constant evaluation too; both read by index, with no
// pointer arithmetic. matcher logs nothing.
//
// thread: none

#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "entry/matcher_basic.hpp"
#include "entry/matcher_read.hpp"

namespace qiven3::entry::matcher {

// The pattern's automaton into out, which is replaced only on success;
// the first fault, or none. Two allocations: the scratch, freed before it
// returns, and the automaton's words; a MemoryError leaves out unchanged.
template <typename A>
constexpr std::optional<PatternError> try_compile(std::string_view pattern, Basic<A>& out) {
    detail::Builder<A> builder(out.get_allocator());
    if (const std::optional<PatternError> fault = builder.read(pattern)) {
        return fault;
    }
    Basic<A> value(out.get_allocator());
    builder.write(value);
    out = std::move(value);
    return std::nullopt;
}

// try_compile, a fault thrown as PatternError.
template <typename A = std::allocator<char>>
constexpr Basic<A> compile(std::string_view pattern, const A& allocator = A{}) {
    Basic<A> out(allocator);
    if (const std::optional<PatternError> fault = try_compile(pattern, out)) {
        throw *fault;
    }
    return out;
}

template <typename A>
constexpr bool hit(const Basic<A>& automaton, std::string_view text) {
    return automaton.hit(text);
}

}  // namespace qiven3::entry::matcher
