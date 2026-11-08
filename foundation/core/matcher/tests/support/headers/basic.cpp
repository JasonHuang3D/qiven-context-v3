// basic's unit for the analysis preset: the automaton over the
// standard allocator, the constant-evaluated vectors' type; matcher.cpp
// compiles it over v3's.

#include "qiven/matcher/basic.hpp"

#include <memory>

#include "qiven/matcher/matcher.hpp"

namespace qiven::matcher {

template class Basic<std::allocator<char>>;

}  // namespace qiven::matcher
