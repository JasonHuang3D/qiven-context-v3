// unit/constant: a subset of utf's, json's, canon's and digest's vectors (the
// specification's) evaluated by the compiler on std::allocator, so a wrong
// result, or any undefined behaviour on the way, fails the build. The case
// only reports that the build held them.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "qiven3/json.hpp"
#include "qiven3/utf.hpp"
#include "runner/runner.hpp"
#include "support/fixtures/filled.hpp"

namespace {

using qiven3::ParseCode;
using qiven3::test::fixtures::filled;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace json = qiven3::json;
namespace utf = qiven3::utf;

constexpr std::optional<ParseCode> scanned(std::string_view text) {
    json::Basic<> v;
    return json::try_scan(text, 16, v);
}

// Scanned with no fault, and its canonical bytes want.
constexpr bool canonical(std::string_view text, std::string_view want) {
    json::Basic<> v;
    if (json::try_scan(text, 16, v)) {
        return false;
    }
    std::string out;
    json::encode(v, out);
    return out == want;
}

constexpr std::size_t decoded(std::string_view text) {
    std::vector<char32_t> out;
    utf::decode(text, out);
    return out.size();
}

constexpr bool replaced(std::u16string_view name, std::string_view want) {
    std::string out;
    utf::backslashreplace(name, out);
    return out == want;
}

constexpr bool deep(std::size_t levels, ParseCode want) {
    const std::string text = filled(levels, '[');
    return scanned(text) == want;
}

// utf: valid texts' code points and units, refusals, order.
static_assert(utf::length("\x63\x61\x66\xc3\xa9") == 4 && utf::units16("\x63\x61\x66\xc3\xa9") == 4);
static_assert(utf::length("\xf0\x9d\x84\x9e") == 1 && utf::units16("\xf0\x9d\x84\x9e") == 2);
static_assert(decoded("\x61\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80") == 4);
static_assert(utf::units16("\x61\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80") == 5);
static_assert(!utf::check("\xf4\x8f\xbf\xbf") && !utf::check("\xef\xbf\xbe") && !utf::check(""));
static_assert(utf::check("\xc0\x80") == ParseCode::utf8 && utf::check("\xed\xa0\x80") == ParseCode::utf8);
static_assert(utf::check("\xf4\x90\x80\x80") == ParseCode::utf8 && utf::check("\xe2\x82") == ParseCode::utf8);
static_assert(utf::check("\xf0\x8f\xbf\xbf") == ParseCode::utf8 && utf::check("\x80") == ParseCode::utf8);
static_assert(utf::check("\xed\xa0\x80", utf::Form::value) == ParseCode::surrogate);
static_assert(utf::compare("a_b", "aa") < 0 && utf::compare("\xc3\xa9", "z") > 0);
static_assert(utf::compare("\xef\xbf\xbd", "\xf0\x9f\x98\x80") < 0);
static_assert(replaced(u"a\xdc80" u"b", "a\\udc80b") && replaced(u"\xdc80\xd800", "\\udc80\\ud800"));
static_assert(replaced(u"\xd83d\xde00", "\xf0\x9f\x98\x80") && replaced(u"\x00e9", "\xc3\xa9"));

// json: one vector of each code scan gives, the depth limit, and canonical
// bytes.
static_assert(scanned(R"({"a":null,"b":1.5})") == ParseCode::floating);
static_assert(scanned(R"([{"a":1,"a":2},1.5])") == ParseCode::duplicate_key);
static_assert(scanned(R"({"a":1,"a":2}})") == ParseCode::duplicate_key);
static_assert(scanned(R"([-Infinity])") == ParseCode::constant);
static_assert(scanned(R"([99999999999999999.])") == ParseCode::int_range);
static_assert(scanned(R"({"a":-10000000000000000})") == ParseCode::int_range);
static_assert(scanned(R"({"a":"\x41"})") == ParseCode::syntax && scanned(R"([1,])") == ParseCode::syntax);
static_assert(scanned(R"({"a":01})") == ParseCode::syntax && scanned(R"([-NaN])") == ParseCode::syntax);
static_assert(deep(17, ParseCode::depth) && deep(16, ParseCode::syntax));
static_assert(canonical(R"({"a_b":1,"aa":2})", R"({"a_b":1,"aa":2})"));
static_assert(canonical(R"({"aa":2,"a_b":1})", R"({"a_b":1,"aa":2})"));
static_assert(canonical(R"({"a":-0})", R"({"a":0})") && canonical(R"({"a":"\/"})", R"({"a":"/"})"));
static_assert(canonical(R"({"a":"\u001F"})", R"({"a":"\u001f"})"));
static_assert(canonical(R"({"a":"\ud834\udd1e"})", "{\"a\":\"\xf0\x9d\x84\x9e\"}"));
static_assert(canonical(R"({"a":9007199254740991})", R"({"a":9007199254740991})"));
static_assert(canonical("-9007199254740991", "-9007199254740991") && canonical("1000000", "1000000"));
static_assert(canonical(" {}\n", "{}") && canonical("[]", "[]"));

// canon: the strict parser's code for data, none if accepted.
constexpr std::optional<ParseCode> parsed(std::string_view data) {
    json::Basic<> v;
    return canon::try_parse(data, v);
}

// Accepted, and written back as the same bytes.
constexpr bool round_trip(std::string_view data) {
    json::Basic<> v;
    if (canon::try_parse(data, v)) {
        return false;
    }
    std::string out;
    return !canon::try_canonical(v, out) && out == data;
}

constexpr std::optional<ParseCode> nested(std::size_t levels) {
    std::string text;
    for (std::size_t i = 1; i < levels; ++i) {
        text += R"({"a":)";
    }
    text += "{}";
    text += filled(levels - 1, '}');
    return parsed(text);
}

// canon: one vector of each code, in the steps' order, Chinese among them;
// the levels' bound; accepted bytes and their round trips.
static_assert(parsed("\xef\xbb\xbf{}") == ParseCode::bom && parsed("{\"a\":\"\xed\xa0\x80\"}") == ParseCode::utf8);
static_assert(nested(17) == ParseCode::depth && !nested(16));
static_assert(parsed(R"({"a":1.0})") == ParseCode::floating && parsed(R"({"a":NaN})") == ParseCode::constant);
static_assert(parsed(R"({"a":9007199254740992})") == ParseCode::int_range);
static_assert(parsed(R"({"a":1,"a":2})") == ParseCode::duplicate_key && parsed(R"({"a":null})") == ParseCode::null);
static_assert(parsed(R"({"a":"\ud800"})") == ParseCode::surrogate && parsed(R"({"\ufdd0":1})") == ParseCode::nonchar);
static_assert(parsed(R"({"A":1})") == ParseCode::key_syntax && parsed(R"({"a":"\u8bb0"})") == ParseCode::not_canonical);
static_assert(parsed(R"({"b":1,"a":2})") == ParseCode::not_canonical && parsed(R"({"a":-0})") == ParseCode::not_canonical);
static_assert(parsed("{\"\xe8\xae\xb0\":1}") == ParseCode::key_syntax);
static_assert(round_trip("{\"a\":\"\xe8\xae\xb0\xe5\xbf\x86\"}") && round_trip(R"({"a_b":1,"aa":2})"));
static_assert(round_trip("{\"a\":\"caf\xc3\xa9\",\"a_b\":[true,false],\"aa\":-9007199254740991,\"b\":9007199254740991}"));
static_assert(round_trip("{\"a\":\"\xef\xb7\xb0\xe2\x80\xa8\x7f\"}") && round_trip("1") && round_trip("{}"));

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

QIVEN3_CASE(constant, vectors) {
    // Each static_assert above held when this binary was built.
    CHECK(true);
}
