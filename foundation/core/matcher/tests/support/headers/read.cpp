// read's unit for the analysis preset: the builder, compile and hit
// over the standard allocator, the constant-evaluated vectors' type;
// matcher.cpp compiles them over v3's.

#include "qiven/matcher/read.hpp"

#include <memory>

#include "qiven/matcher/matcher.hpp"

namespace qiven::matcher {

using Std = std::allocator<char>;

template class detail::Builder<Std>;
template std::optional<PatternError> try_compile(std::string_view, Basic<Std>&);
template Basic<Std> compile(std::string_view, const Std&);
template bool hit(const Basic<Std>&, std::string_view);

}  // namespace qiven::matcher
