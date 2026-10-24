// matcher's instantiation for v3's allocator: compile and hit over an
// arena, compiled once here.

#include "entry/matcher.hpp"

#include "memory/memory.hpp"

namespace qiven3::entry::matcher {

template std::optional<PatternError> try_compile(std::string_view, Automaton&);
template Automaton compile(std::string_view, const memory::Allocator<char>&);
template bool hit(const Automaton&, std::string_view);

}  // namespace qiven3::entry::matcher
