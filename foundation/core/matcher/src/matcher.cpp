// matcher's instantiation for v3's allocator: compile and hit over an
// arena, compiled once here.

#include "qiven/matcher/matcher.hpp"

#include "qiven/memory/memory.hpp"

namespace qiven::matcher {

template std::optional<PatternError> try_compile(std::string_view, Automaton&);
template Automaton compile(std::string_view, const qiven::memory::Allocator<char>&);
template bool hit(const Automaton&, std::string_view);

}  // namespace qiven::matcher
