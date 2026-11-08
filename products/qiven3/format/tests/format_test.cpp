// format's vectors: the framing, Chinese among it; eleven fields' framed
// bytes, their size and their id, the plain digest of those bytes; keyed's
// text over a Key on the fake platform; two records under their own keys,
// each one's content and output keyed and its id over its framed fields,
// the second's parent and target the first's id; and the key's wipes on the
// fake platform, at its end, on a move and after keyed, with new_key's
// draw.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "qiven/digest/digest.hpp"
#include "qiven/platform/fake/fake_platform.hpp"
#include "qiven/runner/runner.hpp"
#include "qiven3/format/entry_id.hpp"
#include "qiven3/format/key.hpp"

namespace {

namespace digest = qiven::digest;
namespace format = qiven3::format;
using format::Field;
using Fake = qiven::platform::fake::FakePlatform;
using Key = format::Key<Fake>;

std::string unhex(std::string_view text) {
    std::string out;
    const auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        out += static_cast<char>((nibble(text[i]) << 4) | nibble(text[i + 1]));
    }
    return out;
}

std::string hex(std::string_view bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out += digits[b >> 4U];
        out += digits[b & 0x0FU];
    }
    return out;
}

std::string hex(const digest::Bytes& bytes) {
    std::string raw;
    for (const std::uint8_t b : bytes) {
        raw += static_cast<char>(b);
    }
    return hex(raw);
}

std::string framed_hex(std::span<const Field> fields) {
    std::string out;
    format::frame(fields, out);
    return hex(std::string_view(out));
}

// The bytes of n positions, byte i (a i + b) mod 256.
std::string pattern(std::size_t n, std::size_t a, std::size_t b) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += static_cast<char>((a * i + b) % 256);
    }
    return out;
}

// A key's material: 32 bytes counting up from first.
std::array<std::byte, 32> counting(unsigned first) {
    std::array<std::byte, 32> key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(first + i);
    }
    return key;
}

qiven::platform::fake::Config seeded(std::uint64_t seed) {
    qiven::platform::fake::Config config;
    config.seed = seed;
    return config;
}

std::size_t wipes(const Fake& fake) {
    std::size_t n = 0;
    for (const std::string& call : fake.calls()) {
        n += call == "wipe" ? 1U : 0U;
    }
    return n;
}

std::string keyed(const Key& key, std::string_view data) {
    std::string out;
    format::keyed(key, data, out);
    return out;
}

// A scope in English and Chinese (格式).
constexpr std::string_view scope = "scope:\xe6\xa0\xbc\xe5\xbc\x8f";

// The eleven fields' framed bytes, the fifth and tenth absent.
constexpr std::string_view eleven =
    "000000000000000d666f726d61742d636173652f31000000000000000c73636f"
    "70653ae6a0bce5bc8f00000000000000046c616e65000000000000000131ffff"
    "ffffffffffff00000000000000046b696e640000000000000009777269746572"
    "2f7731000000000000000562617369730000000000000018323032362d31312d"
    "30335430383a30303a30302e3030305affffffffffffffff0000000000000048"
    "636f6e74656e743a303132333435363738396162636465663031323334353637"
    "3839616263646566303132333435363738396162636465663031323334353637"
    "3839616263646566";

}  // namespace

QIVEN_CASE(format, framing) {
    const std::vector<std::pair<Field, std::string_view>> rows = {
        {std::nullopt, "ffffffffffffffff"},
        {"", "0000000000000000"},
        {"person", "0000000000000006706572736f6e"},
        {"12", "00000000000000023132"},
        {"caf\xc3\xa9", "0000000000000005636166c3a9"},
        {"\xf0\x9d\x84\x9e", "0000000000000004f09d849e"},
        {"\xe8\xae\xb0\xe5\xbf\x86", "0000000000000006e8aeb0e5bf86"},  // 记忆
    };
    for (const auto& [field, want] : rows) {
        CHECK_EQ(framed_hex(std::span<const Field>(&field, 1)), std::string(want));
    }
}

QIVEN_CASE(format, entry_id) {
    // Eleven fields, the fifth and tenth absent.
    constexpr std::string_view content =
        "content:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::array<Field, 11> fields = {
        "format-case/1", scope, "lane", "1", std::nullopt, "kind", "writer/w1", "basis",
        "2026-11-03T08:00:00.000Z", std::nullopt, content};
    CHECK_EQ(framed_hex(fields), std::string(eleven));
    CHECK_EQ(format::framed_size(fields), 232U);
    std::string id;
    format::entry_id(fields, id);
    CHECK_EQ(id, std::string("sha256:2e0e9791cbf0893a1d0ae06c84807e16866b58a6d953792cb4ecef563d4b5eda"));
    std::string plain;
    digest::plain(unhex(eleven), plain);
    CHECK_EQ(plain, id);
}

QIVEN_CASE(format, keyed) {
    // A key of 32 bytes counting up from a0.
    Fake fake(seeded(0));
    const Key key(fake, counting(0xa0));
    const std::vector<std::pair<std::string, std::string_view>> rows = {
        {"", "fbf90b56e2fdada0fb344af7b7215693b3d40ef94782b2473bce1efa88f9df39"},
        {"format's keyed digest over its own text, \xe6\xa0\xbc\xe5\xbc\x8f",
         "9d8c7c6e4f0d72d89b6ce4353308129d5fa56162183afbdec88179a6e407778a"},
        {"tab\t, newline\n, del \x7f, \xf0\x9d\x84\x9e and \xe2\x80\xa8, a quote \" and \\",
         "24857fcdb2223364656452ef29557e9e491d488fd9cea535cae43b32837121d0"},
        {pattern(64, 131, 7), "9fd35a732c4bc64595738ecbfe31378a3509d88a6830045758e96de67f6244c5"},
        {pattern(1120, 131, 7), "1e308522b99b676d39deb52635506d8c90d03f93f95e77ecf75e45585767703d"},
    };
    CHECK_EQ(rows[1].first.size(), 47U);
    CHECK_EQ(rows[2].first.size(), 52U);
    for (const auto& [data, want] : rows) {
        CHECK_EQ(keyed(key, data), "hmac-sha256:" + std::string(want));
    }
}

QIVEN_CASE(format, records) {
    // Two records, each under its own key: the content's keyed digest, the
    // output's, and the id over its framed fields, parent and target absent
    // on the first and the first's id on the second.
    struct Record {
        std::array<std::byte, 32> key;
        std::string_view content;
        std::array<Field, 11> fields;
        std::string_view output;
        std::string_view evidence;
        std::string_view digest;
        std::string_view id;
        std::size_t content_bytes;
        std::size_t framed_bytes;
    };
    constexpr std::string_view first_id = "sha256:cb5de78fb57f5c96f6fbf10b6b42ed3eeb2d01ec87878dd19963b020b4ac635b";
    constexpr std::string_view first_digest =
        "hmac-sha256:0360d6b03706cc7a334b32e5cde9c320f5a25ea9c3cb803c9eae91022874a1f1";
    constexpr std::string_view second_digest =
        "hmac-sha256:ac0c30a06df9c3c8733500227a9305fe6db732798776564a819bafdaeb598b36";
    const std::vector<Record> records = {
        {counting(0x10), "{\"n\":1,\"note\":\"\xe7\xac\xac\xe4\xb8\x80 first record\"}",  // 第一
         {"format-case/1", scope, "lane", "1", std::nullopt, "record", "writer/w1", "claimed",
          "2026-11-03T08:00:00.000Z", std::nullopt, first_digest},
         "format 1\n", "hmac-sha256:ffdeeebb5788e929b87874162977367237b14988fa680290d86556b403d3b8db", first_digest,
         first_id, 36, 240},
        {counting(0x30), "{\"n\":2,\"note\":\"\xe7\xac\xac\xe4\xba\x8c second record\"}",  // 第二
         {"format-case/1", scope, "lane", "2", first_id, "record", "writer/w2", "claimed",
          "2026-11-03T09:00:00.000Z", first_id, second_digest},
         "format 2\n", "hmac-sha256:ae5c0e22e76ad11e673bd1743dd266d31e4f418f1d47122a2bda93c8c1677c38", second_digest,
         "sha256:7667ad590222bf9263a1942e5f66367606a28a7c2bb10eb5727187c97a5b801a", 37, 382},
    };
    Fake fake(seeded(0));
    for (const Record& record : records) {
        const Key key(fake, record.key);
        CHECK_EQ(record.content.size(), record.content_bytes);
        CHECK_EQ(format::framed_size(record.fields), record.framed_bytes);
        CHECK_EQ(keyed(key, record.content), std::string(record.digest));
        CHECK_EQ(keyed(key, record.output), std::string(record.evidence));
        std::string id;
        format::entry_id(record.fields, id);
        CHECK_EQ(id, std::string(record.id));
    }
}

QIVEN_CASE(format, key_wipes) {
    Fake fake(seeded(0));
    const std::string data = "qiven";
    const std::string zero = "hmac-sha256:" + hex(digest::hmac(std::string(32, '\0'), data));
    const std::string fixed = "hmac-sha256:" + hex(digest::hmac(pattern(32, 1, 0xa0), data));
    {
        Key key(fake, counting(0xa0));
        const Key& from = key;  // read after the move: its bytes read zero
        CHECK_EQ(wipes(fake), 0U);
        CHECK_EQ(keyed(key, data), fixed);
        CHECK_EQ(wipes(fake), 3U);  // keyed's two states and padded block
        Key moved(std::move(key));
        CHECK_EQ(wipes(fake), 4U);  // the moved-from key
        CHECK_EQ(keyed(moved, data), fixed);
        CHECK_EQ(keyed(from, data), zero);
        Key other(fake, counting(0xa0));
        const Key& assigned = moved;
        other = std::move(moved);
        CHECK_EQ(wipes(fake), 12U);  // the assignment's two: the old key's, the moved-from's
        CHECK_EQ(keyed(other, data), fixed);
        CHECK_EQ(keyed(assigned, data), zero);
        CHECK_EQ(wipes(fake), 18U);
    }
    CHECK_EQ(wipes(fake), 21U);  // each key at its end
}

QIVEN_CASE(format, new_key) {
    // The fake seeded 1 draws its first block: the key is those bytes, and
    // the drawn copy is wiped.
    Fake fake(seeded(1));
    const Key key = format::new_key(fake);
    const std::vector<std::string> calls = {"random_bytes", "wipe"};
    CHECK(fake.calls() == calls);
    const std::string drawn = unhex("783825822a6f9e62da2190e828e4c9d2576e5977e3a0b3620b092dfb9e9996fa");
    CHECK_EQ(keyed(key, "qiven"), "hmac-sha256:" + hex(digest::hmac(drawn, "qiven")));
    const Key second = format::new_key(fake);
    const std::string next = unhex("532deabf88729cb43995ab5a9cd49bf9b90a079904dc0645ecda9e47ce7345a9");
    CHECK_EQ(keyed(second, "qiven"), "hmac-sha256:" + hex(digest::hmac(next, "qiven")));
}
