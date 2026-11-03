// format's constant case: a subset of its vectors (the specification's)
// evaluated by the compiler on std::allocator, so a wrong result, or any
// undefined behaviour on the way, fails the build: the framing, Chinese
// among it, and an id over eleven fields of the case's own. The case only
// reports that the build held them.

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "qiven/runner/runner.hpp"
#include "qiven3/format/entry_id.hpp"

namespace {

namespace format = qiven3::format;

constexpr bool framed(format::Field field, std::string_view want) {
    std::string out;
    format::frame(std::span<const format::Field>(&field, 1), out);
    std::string text;
    for (const char c : out) {
        const auto b = static_cast<unsigned char>(c);
        text += "0123456789abcdef"[b >> 4U];
        text += "0123456789abcdef"[b & 0x0FU];
    }
    return text == want;
}

// Eleven fields, the fifth and tenth absent, a scope in Chinese (格式).
constexpr std::string id_of_eleven() {
    const std::array<format::Field, 11> fields = {
        "format-case/1", "scope:\xe6\xa0\xbc\xe5\xbc\x8f", "lane", "1", std::nullopt, "kind", "writer/w1", "basis",
        "2026-11-03T08:00:00.000Z", std::nullopt,
        "content:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};
    std::string out;
    format::entry_id(fields, out);
    return out;
}

static_assert(framed(std::nullopt, "ffffffffffffffff") && framed("", "0000000000000000"));
static_assert(framed("\xe8\xae\xb0\xe5\xbf\x86", "0000000000000006e8aeb0e5bf86"));
static_assert(id_of_eleven() == "sha256:2e0e9791cbf0893a1d0ae06c84807e16866b58a6d953792cb4ecef563d4b5eda");

}  // namespace

QIVEN_CASE(format, constant) {
    // Each static_assert above held when this binary was built.
    CHECK(true);
}
