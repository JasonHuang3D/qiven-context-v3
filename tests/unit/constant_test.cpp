// unit/constant: a subset of utf's and json's vectors (the specification's)
// evaluated by the compiler on std::allocator, so a wrong result, or any
// undefined behaviour on the way, fails the build. The case only reports
// that the build held them.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven3/json.hpp"
#include "qiven3/utf.hpp"
#include "runner/runner.hpp"

namespace {

using qiven3::ParseCode;
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
    std::string text(levels, '[');
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

}  // namespace

QIVEN3_CASE(constant, vectors) {
    // Each static_assert above held when this binary was built.
    CHECK(true);
}
