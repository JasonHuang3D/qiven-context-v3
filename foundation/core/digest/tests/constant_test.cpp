// digest's constant case: a subset of its vectors (the specification's)
// evaluated by the compiler on std::allocator, so a wrong result, or any
// undefined behaviour on the way, fails the build: SHA-256 at the padding's
// edges, RFC 4231's HMAC, a key past the block among them, and HMAC under a
// 32-byte key. The case only reports that the build held them.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "qiven/digest/digest.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace digest = qiven::digest;

// A text of n copies of a byte, built by append so that constant
// evaluation takes it on each standard library the cases build with:
// clang 18 on libstdc++ 13 cannot evaluate std::string(n, c), whose
// constructor's helper it finds undefined there.
constexpr std::string filled(std::size_t n, char c) {
    std::string text;
    text.append(n, c);
    return text;
}

constexpr std::string hexed(const digest::Bytes& bytes) {
    std::string out;
    for (const std::uint8_t b : bytes) {
        out += "0123456789abcdef"[b >> 4U];
        out += "0123456789abcdef"[b & 0x0FU];
    }
    return out;
}

// A 32-byte key, byte i (7 i + 3) mod 256.
constexpr std::string key32() {
    std::string key;
    for (std::size_t i = 0; i < 32; ++i) {
        key += static_cast<char>((7 * i + 3) % 256);
    }
    return key;
}

static_assert(hexed(digest::sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
static_assert(hexed(digest::sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
static_assert(hexed(digest::sha256(filled(56, 'a'))) ==
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
static_assert(hexed(digest::hmac("Jefe", "what do ya want for nothing?")) ==
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
static_assert(hexed(digest::hmac(filled(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")) ==
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
static_assert(hexed(digest::hmac(key32(), "qiven")) ==
              "d6efe38afc50e96f896cfcf4abd9e833d339c049b3d5a782fe0cbe60a010caae");

}  // namespace

QIVEN_CASE(digest, constant) {
    // Each static_assert above held when this binary was built.
    CHECK(true);
}
