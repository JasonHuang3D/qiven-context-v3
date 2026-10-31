// json's cases: the specification's edge vectors at scan's steps (3 and 4),
// those of the later steps scanned into the value their walk reads, the
// canonical bytes, integers, the nesting limit, building, and each
// operation's budget and its allocations failed in turn.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/json.hpp"
#include "qiven3/utf.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

using qiven3::ParseCode;
using qiven3::ParseError;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
namespace json = qiven3::json;
namespace utf = qiven3::utf;
using Std = json::Basic<>;

std::optional<ParseCode> code(std::string_view text) {
    Std v;
    return json::try_scan(text, 16, v);
}

std::string encoded(const Std& v) {
    std::string out;
    json::encode(v, out);
    return out;
}

std::string repeat(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

struct Row {
    std::string_view text;
    ParseCode code;
};

}  // namespace

QIVEN_CASE(json, edge_pass_codes) {
    const std::array<Row, 28> rows{{
        {R"({"a":1,"a":2,"b":1.5})", ParseCode::floating},
        {R"([1.5,{"a":1,"a":2}])", ParseCode::floating},
        {R"([1.5,)", ParseCode::floating},
        {R"({"a":null,"b":1.5})", ParseCode::floating},
        {R"(["\ud800",1.0])", ParseCode::floating},
        {R"({"a":1E5})", ParseCode::floating},
        {R"([{"a":1,"a":2},1.5])", ParseCode::duplicate_key},
        {R"({"a":1,"a":2}})", ParseCode::duplicate_key},
        {R"({"A":1,"A":2})", ParseCode::duplicate_key},
        {R"([NaN,1.5])", ParseCode::constant},
        {R"([-Infinity])", ParseCode::constant},
        {R"([99999999999999999.])", ParseCode::int_range},
        {R"(["\ufdd0",9007199254740992])", ParseCode::int_range},
        {R"({"a":9999999999999999})", ParseCode::int_range},
        {R"({"a":10000000000000000})", ParseCode::int_range},
        {R"({"a":-10000000000000000})", ParseCode::int_range},
        {R"({"a":1,"a":2)", ParseCode::syntax},
        {R"([-NaN])", ParseCode::syntax},
        {R"({"a":01})", ParseCode::syntax},
        {R"({"a":+1})", ParseCode::syntax},
        {R"({"a":1.})", ParseCode::syntax},
        {R"({"a":-})", ParseCode::syntax},
        {"{\"a\":\"\x01\"}", ParseCode::syntax},
        {R"({"a":"\x41"})", ParseCode::syntax},
        {R"({"a":"\u12"})", ParseCode::syntax},
        {R"([1,])", ParseCode::syntax},
        {R"({"a":1,})", ParseCode::syntax},
        {R"({'a':1})", ParseCode::syntax},
    }};
    for (const Row& r : rows) {
        const std::optional<ParseCode> got = code(r.text);
        if (!CHECK(got == r.code)) {
            qiven_ctx.out << "  vector " << r.text << ": " << (got ? qiven3::name(*got) : "accepted") << "\n";
        }
    }
    // The thrown form.
    bool thrown = false;
    try {
        Std v;
        json::scan("[1,]", 16, v);
    } catch (const ParseError& e) {
        thrown = e.code == ParseCode::syntax;
    }
    CHECK(thrown);
    CHECK(qiven3::name(ParseCode::floating) == "float");
}

QIVEN_CASE(json, edge_walk_values) {
    // The walk's codes are canon's; scan accepts each and keeps what the
    // walk reads: a lone surrogate in its three-byte form, a noncharacter,
    // a key off its syntax, a null.
    const std::array<std::string_view, 3> surrogates = {
        R"({"a":"\ud834\u0041"})", R"({"a":"\udd1e"})", R"({"a":"\udd1e\ud834"})"};
    for (const std::string_view s : surrogates) {
        Std v;
        REQUIRE(!json::try_scan(s, 16, v));
        CHECK(utf::check(v.text(*v.find(0, "a")), utf::Form::value) == ParseCode::surrogate);
    }
    Std pair;
    constexpr std::string_view text1 = R"({"a":"\ud834\u0041"})";
    REQUIRE(!json::try_scan(text1, 16, pair));
    CHECK(pair.text(*pair.find(0, "a")) == "\xed\xa0\xb4\x41");
    const std::array<std::string_view, 3> nonchars = {
        R"({"a":"\uffff"})", "{\"a\":\"\xf0\x9f\xbf\xbe\"}", "{\"a\":\"\xef\xb7\xaf\"}"};
    for (const std::string_view s : nonchars) {
        Std v;
        REQUIRE(!json::try_scan(s, 16, v));
        std::vector<char32_t> points;
        utf::decode(v.text(*v.find(0, "a")), points);
        CHECK(points.size() == 1 && utf::is_nonchar(points.front()));
    }
    const std::array<std::string_view, 4> keys = {R"({"1a":1})", R"({"a__b":1})", R"({"a_":1})", R"({"A":null})"};
    for (const std::string_view s : keys) {
        Std v;
        REQUIRE(!json::try_scan(s, 16, v));
        CHECK_EQ(v.count(0), 1U);
        CHECK(s.substr(2, v.text(1).size()) == v.text(1));
    }
    Std null;
    REQUIRE(!json::try_scan(R"({"A":null})", 16, null));
    CHECK(null.kind(2) == json::Kind::null);
    // ["\ufdd0",9007199254740992]: the pass's int_range wins over the
    // walk's nonchar before it.
    constexpr std::string_view text2 = R"(["\ufdd0",9007199254740992])";
    CHECK(code(text2) == ParseCode::int_range);
}

QIVEN_CASE(json, edge_canonical) {
    // Scanned, and their canonical bytes other than the text: step 6's
    // not_canonical.
    const std::array<std::string_view, 8> not_canonical = {
        R"({"a":-0})", R"({"a":"\/"})", R"({"a":"\u001F"})", R"({"a":"\u007f"})", R"({"a":"\ud834\udd1e"})",
        R"({"aa":2,"a_b":1})", "{}\n", " {}"};
    for (const std::string_view s : not_canonical) {
        Std v;
        REQUIRE(!json::try_scan(s, 16, v));
        CHECK(encoded(v) != s);
    }
    // Accepted: scanned, and their canonical bytes the text itself.
    const std::string deep = repeat(R"({"a":)", 15) + "{}" + repeat("}", 15);
    const std::string brackets = R"({"a":")" + repeat("[", 40) + R"("})";
    const std::array<std::string_view, 13> accepted = {
        R"({"a":9007199254740991})", R"({"a":-9007199254740991})", R"({"a":"\u001f"})", "{\"a\":\"\x7f\"}",
        "{\"a\":\"\xe2\x80\xa8\"}", "{\"a\":\"\xc2\x85\"}", "{\"a\":\"\xf0\x9d\x84\x9e\"}",
        "{\"a\":\"\xef\xb7\xb0\"}", R"({"a_b":1,"aa":2})", "1", "[]", deep, brackets};
    for (const std::string_view s : accepted) {
        Std v;
        const std::optional<ParseCode> got = json::try_scan(s, 16, v);
        CHECK(!got);
        if (!got && !CHECK(encoded(v) == s)) {
            qiven_ctx.out << "  vector " << s << ": " << encoded(v) << "\n";
        }
    }
}

QIVEN_CASE(json, depth) {
    // 17 levels are depth, 16 are not; the scan as written counts brackets
    // outside strings alone, and 100,000 levels are depth, no stack fault.
    CHECK(code(repeat("[", 17)) == ParseCode::depth);
    CHECK(code(repeat("[", 17) + "\xff") == ParseCode::depth);
    CHECK(code(repeat("[", 16) + repeat("]", 16)) == std::nullopt);
    CHECK(code(repeat("[", 16)) == ParseCode::syntax);
    CHECK(code(repeat("[", 100000)) == ParseCode::depth);
    CHECK(code(R"([")" + repeat("[", 40) + R"("])") == std::nullopt);
    constexpr std::string_view text3 = R"(["\"[[[[[[[[[[[[[[[[[["])";
    CHECK(code(text3) == std::nullopt);
    // A close below level 0 is the pass's syntax, and the level it left
    // counts again.
    CHECK(code("]" + repeat("[", 17)) == ParseCode::syntax);
    Std v;
    CHECK(json::try_scan("[[1]]", 1, v) == ParseCode::depth);
    CHECK(!json::try_scan("[[1]]", 2, v));
    CHECK_EQ(v.depth(), 2U);
    Std flat;
    CHECK(json::try_scan("[[1]]", 0, flat) == ParseCode::depth);
    CHECK(!json::try_scan("1", 0, flat));
}

QIVEN_CASE(json, integers) {
    const std::array<std::string_view, 7> texts = {"0", "1", "-1", "12", "9007199254740991", "-9007199254740991",
                                                   "1000000"};
    const std::array<long long, 7> values = {0, 1, -1, 12, 9007199254740991, -9007199254740991, 1000000};
    for (std::size_t i = 0; i < texts.size(); ++i) {
        Std v;
        REQUIRE(!json::try_scan(texts.at(i), 16, v));
        CHECK_EQ(v.integer(), values.at(i));
        CHECK(encoded(v) == texts.at(i));
    }
    CHECK(code("+5") == ParseCode::syntax);
    CHECK(code(" 5") == std::nullopt);
    CHECK(code("9007199254740992") == ParseCode::int_range);
    CHECK(code("-9007199254740992") == ParseCode::int_range);
    CHECK(code("00") == ParseCode::syntax);
    CHECK(code("1e") == ParseCode::syntax);
    CHECK(code("1e+") == ParseCode::syntax);
    CHECK(code("-0.0") == ParseCode::floating);
}

QIVEN_CASE(json, strings) {
    // The encoder's escapes, every other byte raw.
    std::string raw;
    for (int c = 0; c < 0x20; ++c) {
        raw.push_back(static_cast<char>(c));
    }
    raw += "\"\\\x7f\xe2\x80\xa8\xe2\x80\xa9\xc2\x85\xef\xbb\xbf\xf0\x9f\x98\x80/";
    Std v;
    json::Writer<std::allocator<char>>(v).string(raw);
    const std::string want = R"("\u0000\u0001\u0002\u0003\u0004\u0005\u0006\u0007\b\t\n\u000b\f\r\u000e\u000f)"
                             R"(\u0010\u0011\u0012\u0013\u0014\u0015\u0016\u0017\u0018\u0019\u001a\u001b\u001c)"
                             R"(\u001d\u001e\u001f\"\\)"
                             "\x7f\xe2\x80\xa8\xe2\x80\xa9\xc2\x85\xef\xbb\xbf\xf0\x9f\x98\x80/\"";
    CHECK(encoded(v) == want);
    // Read back, the same bytes; the escapes json.loads takes.
    Std back;
    REQUIRE(!json::try_scan(want, 16, back));
    CHECK(back.text() == raw);
    Std esc;
    constexpr std::string_view text4 = R"("\"\\\/\b\f\n\r\t\u00e9\u00E9\ud83d\ude00")";
    REQUIRE(!json::try_scan(text4, 16, esc));
    CHECK(esc.text() == "\"\\/\b\f\n\r\t\xc3\xa9\xc3\xa9\xf0\x9f\x98\x80");
    constexpr std::string_view text5 = R"("\u00e")";
    CHECK(code(text5) == ParseCode::syntax);
    constexpr std::string_view text6 = R"("\a")";
    CHECK(code(text6) == ParseCode::syntax);
    CHECK(code(R"("abc)") == ParseCode::syntax);
    CHECK(code("\"a\tb\"") == ParseCode::syntax);
    constexpr std::string_view text7 = R"("\ud834\u00")";
    CHECK(code(text7) == ParseCode::syntax);
    CHECK(code("") == ParseCode::syntax);
    CHECK(code("nul") == ParseCode::syntax);
    CHECK(code("[true,false,null]") == std::nullopt);
    CHECK(code("[true false]") == ParseCode::syntax);
    CHECK(code("[1]\v") == ParseCode::syntax);
    CHECK(code("\xc2\xa0[1]") == ParseCode::syntax);
}

QIVEN_CASE(json, building) {
    // A value built in document order, and members appended to its root:
    // the encoder sorts keys by code point.
    Std inner;
    {
        json::Writer<std::allocator<char>> w(inner);
        w.open(json::Kind::array);
        w.integer(-3);
        w.boolean(true);
        w.null();
        w.open(json::Kind::object);
        w.close();
        w.close();
        CHECK_EQ(w.levels(), 0U);
    }
    Std root;
    root.set("zeta", std::string_view("z"));
    root.set("alpha", std::int64_t{7});
    root.set("\xc3\xa9", false);
    root.set("list", inner);
    CHECK(encoded(root) == "{\"alpha\":7,\"list\":[-3,true,null,{}],\"zeta\":\"z\",\"\xc3\xa9\":false}");
    CHECK_EQ(root.count(), 4U);
    CHECK(root.find(0, "alpha") && root.integer(*root.find(0, "alpha")) == 7);
    CHECK(!root.find(0, "beta"));
    const json::Index list = *root.find(0, "list");
    CHECK_EQ(root.count(list), 4U);
    CHECK_EQ(root.next(list), list + 5);
    Std items;
    items.push(inner);
    items.push(root);
    CHECK(encoded(items) == "[[-3,true,null,{}]," + encoded(root) + "]");
    CHECK_EQ(items.depth(), 4U);
    Std copy;
    REQUIRE(!json::try_scan(encoded(items), 16, copy));
    CHECK(encoded(copy) == encoded(items));
    Std again;
    REQUIRE(!json::try_scan(encoded(copy), 16, again));
    CHECK(again == copy);
}

QIVEN_CASE(json, replaced_on_success) {
    Std v;
    REQUIRE(!json::try_scan(R"({"a":1})", 16, v));
    CHECK(json::try_scan(R"({"a":1,"a":2})", 16, v) == ParseCode::duplicate_key);
    CHECK(json::try_scan("[1.5]", 16, v) == ParseCode::floating);
    CHECK(encoded(v) == R"({"a":1})");
}
