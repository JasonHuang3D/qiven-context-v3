// The fake's seed blocks, the bytes its random_bytes gives: digest's
// SHA-256 over the seed and the block's index.

#include "qiven/platform/fake/fake_platform.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "qiven/digest/digest.hpp"

namespace qiven::platform::fake {

std::array<std::uint8_t, 32> seed_block(std::uint64_t seed, std::uint64_t k) {
    std::array<char, 16> message{};
    std::size_t at = 0;
    for (std::uint64_t value : {seed, k}) {
        for (unsigned shift = 64U; shift > 0U; shift -= 8U) {
            message[at] = static_cast<char>((value >> (shift - 8U)) & 0xFFU);
            ++at;
        }
    }
    return qiven::digest::sha256(std::string_view(message.data(), message.size()));
}

}  // namespace qiven::platform::fake
