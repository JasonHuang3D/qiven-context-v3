// canon's cases: the strict parser's refusals, each code reached by the
// specification's vectors, Chinese among them; the accepted vectors and
// their round trips (parse, then canonical, gives the bytes back); the
// encoder's escapes; the walk of a value built in memory; and the bounds.
// The key rule by place and a layout taken whole are canon_rules_test.cpp's.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven/runner/runner.hpp"
#include "qiven/text/canon.hpp"

namespace {

using qiven::text::ParseCode;
using qiven::text::ParseError;
namespace canon = qiven::text::canon;
namespace json = qiven::text::json;
using Std = canon::Basic<>;
using W = json::Writer<std::allocator<char>>;

// The specification's vectors cv2 and cv3, as their hex gives them and
// with the ASCII escapes the text shows; and the canonical lines of a
// catalogue of the cases' own, books' records and their cards, English and
// Chinese: nesting, arrays, escapes, integers to the bounds and hex
// strings, each with its size. Non-ASCII bytes are octal escapes, so the
// source stays ASCII.
namespace cv {

// cv2: key order and integer bounds; cv3: every escape the encoder emits.
constexpr std::string_view cv2 =
    "{\"a\":\"caf\303\251\",\"a_b\":[true,false],\"aa\":-9007199254740991,\"b\":9007199254740991}";
constexpr std::string_view cv3 =
    "{\"text\":\"a\\u0000b\\bc\\td\\ne\\ff\\rg\\u001fh\\\"i\\\\j\177k\342\200\250l\342\200\251m\357"
    "\273\277n\360\235\204\236o\"}";
constexpr std::string_view cv2_escaped =
    "{\"a\":\"caf\\u00e9\",\"a_b\":[true,false],\"aa\":-9007199254740991,\"b\":9007199254740991}";
constexpr std::string_view cv3_escaped =
    "{\"text\":\"a\\u0000b\\bc\\td\\ne\\ff\\rg\\u001fh\\\"i\\\\j\\u007fk\\u2028l\\u2029m\\ufeffn\\ud8"
    "34\\udd1eo\"}";

struct Line {
    std::string_view name;
    std::string_view bytes;
    std::size_t size = 0;
};

inline constexpr std::array<Line, 7> lines{{
    {"A record",
     "{\"authors\":[{\"name\":\"Donald E. Knuth\",\"role\":\"author\"}],\"edition\":3,\"files\":[{\"bytes"
     "\":1048576,\"checksum\":\"sha256:3a370511089bf3469e8fdce4d4c63bbacff333f50851acc2e94cd8aa040a5314\","
     "\"kind\":\"scan\"}],\"loan\":{\"days\":21,\"renewals\":[7,7]},\"note\":\"Its first chapter is called"
     " \\\"Basic Concepts\\\".\",\"shelf\":{\"offset\":-4,\"room\":\"north\",\"row\":12},\"title\":\"The A"
     "rt of Computer Programming, Volume 1\",\"year\":1997}",
     398},
    {"A card",
     "{\"added\":\"2026-10-17T09:30:00.000Z\",\"clerk\":\"desk/s39\",\"copies\":2,\"number\":\"sha256:67d1"
     "9c28b4e8e1fb37e6f0d049dd6fea17c226daba6e9d4c6c72f5d6e148763d\",\"record\":\"sha256:dfcd931fd5a5b0b5f"
     "a28a4dfbb4ece7b239e1337d56c98616239e36c4ca871d2\",\"series\":\"catalogue/1\",\"status\":\"on-shelf\""
     "}",
     275},
    {"B record",
     "{\"authors\":[{\"name\":\"Edsger W. Dijkstra\",\"role\":\"author\"}],\"note\":\"Typed, then scanned."
     "\\nThe pages run 1 to 87.\",\"pages\":{\"first\":1,\"last\":87,\"missing\":[]},\"path\":\"C:\\\\book"
     "s\\\\ewd249.pdf\",\"tags\":[\"structured\",\"proof\",\"goto\"],\"title\":\"Notes on Structured Progr"
     "amming\",\"year\":1970}",
     278},
    {"C record",
     "{\"authors\":[{\"name\":\"\346\233\271\351\233\252\350\212\271\",\"role\":\"\344\275\234\350\200\205"
     "\"},{\"name\":\"\351\253\230\351\271\227\",\"role\":\"\347\273\255\344\271\246\"}],\"files\":[{\"byt"
     "es\":2621440,\"checksum\":\"sha256:11375d46fb9bc5538c724627e1199f01faca3422fba9c3881b80d3b9d1ab2835"
     "\",\"kind\":\"text\"}],\"loan\":{\"days\":14,\"renewals\":[-1,0,1]},\"summary\":\"\344\273\245\350"
     "\264\276\343\200\201\345\217\262\343\200\201\347\216\213\343\200\201\350\226\233\345\233\233\345\244"
     "\247\345\256\266\346\227\217\344\270\272\350\203\214\346\231\257\357\274\214\345\206\231\344\270\200"
     "\344\270\252\345\256\266\346\227\217\347\232\204\345\205\264\350\241\260\343\200\202\",\"tags\":[\""
     "\345\217\244\345\205\270\",\"\345\260\217\350\257\264\",\"\346\270\205\344\273\243\"],\"title\":\""
     "\347\272\242\346\245\274\346\242\246\",\"volumes\":[[1,2,3,4],[5,6],[]],\"year\":1791}",
     437},
    {"C card",
     "{\"added\":\"2026-10-17T05:02:10.000Z\",\"clerk\":\"desk/s42\",\"copies\":1,\"number\":\"sha256:3cbf"
     "1e229597d349d200bfbfa24580c6a68e44bf7a2bbda5eaa591f8052da070\",\"record\":\"sha256:9864b420ee93a4648"
     "43eca2c86092143cbee338c38ba96040357f094707ccfc8\",\"series\":\"catalogue/1\",\"status\":\"\345\234"
     "\250\346\236\266\"}",
     273},
    {"D record",
     "{\"authors\":[{\"name\":\"\346\233\271\351\233\252\350\212\271\",\"role\":\"\344\275\234\350\200\205"
     "\"},{\"name\":\"\351\253\230\351\271\227\",\"role\":\"\347\273\255\344\271\246\"},{\"name\":\"\344"
     "\270\255\345\233\275\350\211\272\346\234\257\347\240\224\347\251\266\351\231\242\347\272\242\346\245"
     "\274\346\242\246\347\240\224\347\251\266\346\211\200\",\"role\":\"\346\240\241\346\263\250\"}],\"loa"
     "n\":{\"days\":7,\"limits\":{\"fine\":-9007199254740991,\"per_year\":4},\"renewals\":[]},\"note\":\""
     "\347\254\254\344\270\200\350\241\214\357\274\232\345\211\215\345\205\253\345\215\201\345\233\236\\n"
     "\347\254\254\344\272\214\350\241\214\357\274\232\345\220\216\345\233\233\345\215\201\345\233\236\\t"
     "\\\"\347\250\213\351\253\230\346\234\254\\\"\\\\\347\273\255\",\"see\":[{\"number\":\"sha256:3cbf1e2"
     "29597d349d200bfbfa24580c6a68e44bf7a2bbda5eaa591f8052da070\",\"type\":\"card\"},{\"bytes\":3145728,\""
     "checksum\":\"sha256:5f8be010416a6946f6957db332c8ec3933e28c72f7b0a36e79caa9cdfb3a238b\",\"type\":\"fi"
     "le\"}],\"shelf\":{\"offset\":9007199254740991,\"room\":\"east\",\"row\":3},\"summary\":\"This editio"
     "n replaces the 1791 text on the shelf; \346\257\217\344\270\200\345\233\236\347\232\204\346\263\250"
     "\351\207\212\351\203\275\346\224\276\345\234\250\345\233\236\346\234\253\343\200\202\",\"title\":\""
     "\347\272\242\346\245\274\346\242\246\357\274\210\346\240\241\346\263\250\346\234\254\357\274\211\","
     "\"volumes\":[[1,2],[3,4]],\"year\":2008}",
     764},
    {"D card",
     "{\"added\":\"2026-10-17T09:40:00.000Z\",\"clerk\":\"desk/s43\",\"copies\":3,\"number\":\"sha256:3a7e"
     "1230080ad843a7e0ae2ce5084dedcd283e7b2e7b7f73b368183335d48187\",\"parent\":\"sha256:3cbf1e229597d349d"
     "200bfbfa24580c6a68e44bf7a2bbda5eaa591f8052da070\",\"record\":\"sha256:0426f993eac3449c6c6773ce4fa56e"
     "268b1245174bcee64c2d4dfa7944aba97d\",\"replaces\":\"sha256:3cbf1e229597d349d200bfbfa24580c6a68e44bf7"
     "a2bbda5eaa591f8052da070\",\"series\":\"catalogue/2\",\"status\":\"on-shelf\"}",
     443},
}};

}  // namespace cv

std::string repeat(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

template <canon::KeyRule K = canon::Syntax>
std::optional<ParseCode> code(std::string_view data, const K& rule = K{}, canon::Layout layout = canon::Layout::canonical) {
    Std v;
    return canon::try_parse(data, v, rule, layout);
}

std::string shown(std::optional<ParseCode> c) { return c ? std::string(qiven::text::name(*c)) : "accepted"; }

struct Row {
    std::string data;
    std::optional<ParseCode> code;
};

void check_rows(qiven::runner::Context& qiven_ctx, const std::vector<Row>& rows) {
    for (const Row& r : rows) {
        const std::optional<ParseCode> got = code(r.data);
        if (!CHECK(got == r.code)) {
            qiven_ctx.out << "  vector " << r.data.substr(0, 80) << ": " << shown(got) << ", not " << shown(r.code)
                           << "\n";
        }
    }
}

// Parsed, then written: the bytes again.
bool round_trip(std::string_view data) {
    Std v;
    if (canon::try_parse(data, v)) {
        return false;
    }
    std::string out;
    canon::canonical(v, out);
    return out == data;
}

std::string nested(std::size_t levels) { return repeat(R"({"a":)", levels - 1) + "{}" + repeat("}", levels - 1); }

}  // namespace

QIVEN_CASE(canon, refusals) {
    // The first table of the refusals, each row's bytes with its code.
    check_rows(qiven_ctx, {
        {"\xef\xbb\xbf{}", ParseCode::bom},
        {R"({"a":1,"a":2})", ParseCode::duplicate_key},
        {R"({"a":NaN})", ParseCode::constant},
        {R"({"a":Infinity})", ParseCode::constant},
        {R"({"a":1e400})", ParseCode::floating},
        {R"({"a":1.0})", ParseCode::floating},
        {R"({"a":)" + std::string(400, '9') + "}", ParseCode::int_range},
        {R"({"a":9007199254740992})", ParseCode::int_range},
        {R"({"a":"\ud800"})", ParseCode::surrogate},
        {R"({"a":null})", ParseCode::null},
        {R"({"A":1})", ParseCode::key_syntax},
        {R"({"a": 1})", ParseCode::not_canonical},
        {R"({"b":1,"a":2})", ParseCode::not_canonical},
        {R"({"a":"\u00e9"})", ParseCode::not_canonical},
        {R"({"a":"\u8bb0"})", ParseCode::not_canonical},
        {R"({"a":-0})", ParseCode::not_canonical},
        {"{\"a\":\"\xff\"}", ParseCode::utf8},
        {"{\"a\":\"\xed\xa0\x80\"}", ParseCode::utf8},
        {repeat(R"({"a":)", 16) + "{}" + repeat("}", 16), ParseCode::depth},
        {std::string(100000, '[') + std::string(100000, ']'), ParseCode::depth},
        {"{\"a\":\"\xef\xbf\xbe\"}", ParseCode::nonchar},
        {"{\"a\":\"\xf4\x8f\xbf\xbf\"}", ParseCode::nonchar},
        {R"({"a":"\ufdd0"})", ParseCode::nonchar},
        {R"({"\ud800":1})", ParseCode::surrogate},
        {R"({"\ufdd0":1})", ParseCode::nonchar},
    });
    // The thrown form: ParseError with the code, and out not replaced.
    Std v;
    canon::parse(R"({"a":1})", v);
    bool thrown = false;
    try {
        canon::parse(R"({"a":null})", v);
    } catch (const ParseError& e) {
        thrown = e.code == ParseCode::null;
    }
    CHECK(thrown);
    CHECK(v.integer(*v.find(0, "a")) == 1);
}

QIVEN_CASE(canon, edge_refusals) {
    // The edge vectors: the pass's codes win over the walk's wherever each
    // sits, and inside a member the key's characters, then its rule, then
    // its value.
    check_rows(qiven_ctx, {
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
        {R"({"a":"\ud834\u0041"})", ParseCode::surrogate},
        {R"({"a":"\udd1e"})", ParseCode::surrogate},
        {R"({"a":"\udd1e\ud834"})", ParseCode::surrogate},
        {R"({"a":"\uffff"})", ParseCode::nonchar},
        {"{\"a\":\"\xf0\x9f\xbf\xbe\"}", ParseCode::nonchar},
        {"{\"a\":\"\xef\xb7\xaf\"}", ParseCode::nonchar},
        {R"({"1a":1})", ParseCode::key_syntax},
        {R"({"a__b":1})", ParseCode::key_syntax},
        {R"({"a_":1})", ParseCode::key_syntax},
        {R"({"A":null})", ParseCode::key_syntax},
        {R"({"a":"\/"})", ParseCode::not_canonical},
        {R"({"a":"\u001F"})", ParseCode::not_canonical},
        {R"({"a":"\u007f"})", ParseCode::not_canonical},
        {R"({"a":"\ud834\udd1e"})", ParseCode::not_canonical},
        {R"({"aa":2,"a_b":1})", ParseCode::not_canonical},
        {"{}\n", ParseCode::not_canonical},
        {" {}", ParseCode::not_canonical},
        {std::string(17, '['), ParseCode::depth},
        {std::string(17, '[') + "\xff", ParseCode::utf8},
        {"\xef\xbb\xbf\xff", ParseCode::bom},
        // Chinese: a key in it is off the syntax; a value's \u escape is
        // not canonical, its raw form is.
        {"{\"\xe8\xae\xb0\xe5\xbf\x86\":1}", ParseCode::key_syntax},
        {R"({"a":"\u8bb0\u5fc6"})", ParseCode::not_canonical},
        {"{\"a\":\"\xe8\xae\xb0\xe5\xbf\x86\",\"a\":1}", ParseCode::duplicate_key},
    });
}

QIVEN_CASE(canon, accepted_round_trips) {
    const std::vector<std::string> accepted = {
        R"({"a":9007199254740991})",
        R"({"a":-9007199254740991})",
        R"({"a":"\u001f"})",
        "{\"a\":\"\x7f\"}",
        "{\"a\":\"\xe2\x80\xa8\"}",
        "{\"a\":\"\xc2\x85\"}",
        "{\"a\":\"\xf0\x9d\x84\x9e\"}",
        "{\"a\":\"\xef\xb7\xb0\"}",
        R"({"a_b":1,"aa":2})",
        "1",
        "[]",
        nested(16),
        R"({"a":")" + std::string(40, '[') + R"("})",
        // Chinese raw, and a value of each kind.
        "{\"a\":\"\xe8\xae\xb0\xe5\xbf\x86\"}",
        R"({"a":[true,false,0,-1,"",{}],"b":{"c":[[]]}})",
        // The specification's vectors; the catalogue's lines below.
        "{}",
        std::string(cv::cv2),
        std::string(cv::cv3),
    };
    for (const std::string& data : accepted) {
        if (!CHECK(round_trip(data))) {
            qiven_ctx.out << "  vector " << data.substr(0, 80) << ": " << shown(code(data)) << "\n";
        }
    }
    for (const cv::Line& line : cv::lines) {
        if (!CHECK(round_trip(line.bytes)) || !CHECK_EQ(line.bytes.size(), line.size)) {
            qiven_ctx.out << "  line " << line.name << "\n";
        }
    }
}

QIVEN_CASE(canon, encoder) {
    // The specification's vectors as its text shows them, ASCII escapes:
    // not canonical as given; their canonical bytes are the hex's.
    const std::array<std::string_view, 2> escaped = {cv::cv2_escaped, cv::cv3_escaped};
    const std::array<std::string_view, 2> bytes = {cv::cv2, cv::cv3};
    for (std::size_t i = 0; i < escaped.size(); ++i) {
        CHECK(code(escaped.at(i)) == ParseCode::not_canonical);
        Std v;
        REQUIRE(!canon::try_parse(escaped.at(i), v, canon::Syntax{}, canon::Layout::any));
        std::string out;
        canon::canonical(v, out);
        CHECK(out == bytes.at(i));
    }
    CHECK_EQ(cv::cv2.size(), 76U);
    CHECK_EQ(cv::cv3.size(), 66U);
    // \u8bb0\u5fc6 written raw, in UTF-8.
    Std v;
    REQUIRE(!canon::try_parse(R"({"a":"\u8bb0\u5fc6"})", v, canon::Syntax{}, canon::Layout::any));
    std::string out;
    canon::canonical(v, out);
    CHECK(out == "{\"a\":\"\xe8\xae\xb0\xe5\xbf\x86\"}");
}

QIVEN_CASE(canon, walk) {
    // A value built in memory: each walk code, with out unchanged.
    const auto refused = [](const Std& v) {
        std::string out = "kept";
        const std::optional<ParseCode> got = canon::try_canonical(v, out);
        return out == "kept" ? got : std::optional<ParseCode>{};
    };
    Std null;
    {
        W w(null);
        w.null();
    }
    CHECK(refused(null) == ParseCode::null);
    Std lone;
    {
        W w(lone);
        w.begin();
        w.point(0xD800);
        w.end();
    }
    CHECK(refused(lone) == ParseCode::surrogate);
    Std nonchar;
    nonchar.set("a", "\xef\xbf\xbf");
    CHECK(refused(nonchar) == ParseCode::nonchar);
    Std bytes;
    bytes.set("a", "\xc3");
    CHECK(refused(bytes) == ParseCode::utf8);
    Std key;
    key.set("a", std::int64_t{1});
    key.set("B", std::int64_t{1});
    CHECK(refused(key) == ParseCode::key_syntax);
    Std big;
    {
        W w(big);
        w.integer(json::max_integer + 1);
    }
    CHECK(refused(big) == ParseCode::int_range);
    Std small;
    {
        W w(small);
        w.integer(-json::max_integer - 1);
    }
    CHECK(refused(small) == ParseCode::int_range);
    // The key's characters before its rule: \ud800 as a key is surrogate.
    Std surrogate_key;
    {
        W w(surrogate_key);
        w.open(json::Kind::object);
        w.begin();
        w.point(0xD800);
        w.end();
        w.integer(1);
        w.close();
    }
    CHECK(refused(surrogate_key) == ParseCode::surrogate);
    // The thrown form, out unchanged.
    std::string out = "kept";
    bool thrown = false;
    try {
        canon::canonical(null, out);
    } catch (const ParseError& e) {
        thrown = e.code == ParseCode::null;
    }
    CHECK(thrown && out == "kept");
    // Written in key order, appended to out.
    Std fine;
    fine.set("b", "\xe8\xae\xb0");
    fine.set("a", true);
    out = "x";
    CHECK(!canon::try_canonical(fine, out));
    CHECK(out == "x{\"a\":true,\"b\":\"\xe8\xae\xb0\"}");
    // The syntax alone.
    for (const std::string_view k : {"a", "a1", "a_b", "a_1_b2", "abc_def"}) {
        CHECK(canon::key_syntax(k));
    }
    for (const std::string_view k : {"", "_a", "1a", "a_", "a__b", "aB", "a-b", "a b", "\xc3\xa9"}) {
        CHECK(!canon::key_syntax(k));
    }
}
