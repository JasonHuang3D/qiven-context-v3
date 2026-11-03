// unit/constant: a subset of digest's vectors (the specification's)
// evaluated by the compiler on std::allocator, so a wrong result, or any
// undefined behaviour on the way, fails the build. The case only reports
// that the build held them.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "qiven/runner/runner.hpp"
#include "qiven3/digest.hpp"
#include "support/fixtures/filled.hpp"

namespace {

using qiven::test::fixtures::filled;
namespace digest = qiven3::digest;

// digest: the framing, Chinese among it; entry A's id; SHA-256 at the
// padding's edges; RFC 4231's HMAC, a key past the block among them; and a
// content digest under the fixtures' key.
constexpr std::string hexed(const digest::Bytes& bytes) {
    std::string out;
    for (const std::uint8_t b : bytes) {
        out += "0123456789abcdef"[b >> 4U];
        out += "0123456789abcdef"[b & 0x0FU];
    }
    return out;
}

constexpr bool framed(digest::Field field, std::string_view want) {
    std::string out;
    digest::frame(std::span<const digest::Field>(&field, 1), out);
    std::string text;
    for (const char c : out) {
        const auto b = static_cast<unsigned char>(c);
        text += "0123456789abcdef"[b >> 4U];
        text += "0123456789abcdef"[b & 0x0FU];
    }
    return text == want;
}

constexpr std::string id_of_entry_a() {
    const std::array<digest::Field, 11> fields = {
        "qiven-entry/1", "project:qiven", "implementer", "1", std::nullopt, "claim", "implementer/s39", "claimed",
        "2026-10-17T09:30:00.000Z", std::nullopt,
        "hmac-sha256:df0388fc101c4c0bc038ea2dc231dd5fadc5376af843e48755de9f638096be5e"};
    std::string out;
    digest::entry_id(fields, out);
    return out;
}

constexpr std::string fixtures_key() {
    std::string key;
    for (int i = 0; i < 32; ++i) {
        key += static_cast<char>(i);
    }
    return key;
}

static_assert(framed(std::nullopt, "ffffffffffffffff") && framed("", "0000000000000000"));
static_assert(framed("\xe8\xae\xb0\xe5\xbf\x86", "0000000000000006e8aeb0e5bf86"));
static_assert(id_of_entry_a() == "sha256:902380391ae283f1a88e038cf525aea22d5e4ed0ca3f852c8acdf43457b3b119");
static_assert(hexed(digest::sha256("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
static_assert(hexed(digest::sha256("")) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
static_assert(hexed(digest::sha256(filled(56, 'a'))) ==
              "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
static_assert(hexed(digest::hmac("Jefe", "what do ya want for nothing?")) ==
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
static_assert(hexed(digest::hmac(filled(131, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First")) ==
              "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
static_assert(hexed(digest::hmac(fixtures_key(), "{}")) ==
              "86227fe96722a2247dcfee12c07d89c1b0567bbc9aa6a8d804838f66a3da843f");

}  // namespace

QIVEN_CASE(constant, vectors) {
    // Each static_assert above held when this binary was built.
    CHECK(true);
}
