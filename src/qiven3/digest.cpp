// digest's comparison, its size error, and its instantiation for v3's
// string: the framing, the id and the plain digest over an arena, compiled
// once here. A Key is a template on the platform, instantiated by each
// binary for its adapter.

#include "qiven3/digest.hpp"

#include <limits>

namespace qiven3::digest {

// Every byte pair's XOR OR-ed into one accumulator, read once at the end.
// The accumulator is volatile, so the compiler keeps every step.
bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    volatile unsigned char seen = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        seen = static_cast<unsigned char>(seen | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
    }
    return seen == 0;
}

namespace detail {

void too_large(std::size_t held) {
    throw memory::MemoryError::budget("digest", std::numeric_limits<std::size_t>::max(),
                                      std::numeric_limits<std::size_t>::max(), held);
}

}  // namespace detail

template void frame(std::span<const Field>, memory::String&);
template void entry_id(std::span<const Field>, memory::String&);
template void plain(std::string_view, memory::String&);

}  // namespace qiven3::digest
