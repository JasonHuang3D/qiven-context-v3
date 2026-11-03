// digest's comparison and its instantiation for v3's string: the plain
// digest over an arena, compiled once here.

#include "qiven/digest/digest.hpp"

#include "qiven/memory/memory.hpp"

namespace qiven::digest {

// Every byte pair's XOR OR-ed into one accumulator, read once at the end.
// The accumulator is volatile, so the compiler keeps every step.
bool same(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    volatile unsigned char seen = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned x = static_cast<unsigned char>(a[i]);
        const unsigned y = static_cast<unsigned char>(b[i]);
        seen = static_cast<unsigned char>(seen | (x ^ y));
    }
    return seen == 0;
}

template void plain(std::string_view, qiven::memory::String&);

}  // namespace qiven::digest
