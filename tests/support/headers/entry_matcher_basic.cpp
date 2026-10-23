// matcher_basic's unit for the analysis preset: the automaton over the
// standard allocator, the constant-evaluated vectors' type; matcher.cpp
// compiles it over v3's.

#include "entry/matcher_basic.hpp"

#include <memory>

#include "entry/matcher.hpp"

namespace qiven3::entry::matcher {

template class Basic<std::allocator<char>>;

}  // namespace qiven3::entry::matcher
