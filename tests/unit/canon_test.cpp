// canon's cases: the strict parser's refusals, each code reached by the
// specification's vectors, Chinese among them; the accepted vectors and
// their round trips (parse, then canonical, gives the bytes back); the
// encoder's escapes; the walk of a value built in memory; the key rule by
// place and the layout the registry takes; and the bounds.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven3/canon.hpp"
#include "runner/runner.hpp"

namespace {

using qiven3::ParseCode;
using qiven3::ParseError;
namespace canon = qiven3::canon;
namespace json = qiven3::json;
using Std = canon::Basic<>;
using W = json::Writer<std::allocator<char>>;

// The specification's vectors: the content digests' objects, as their hex
// gives them and with the ASCII escapes the text shows; and entries'
// canonical lines, English and Chinese, each with its size. Non-ASCII
// bytes are octal escapes, so the source stays ASCII.
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
    {"A content",
     "{\"admit\":{\"mistake\":\"A parser built on json.loads would keep the last of two equal keys.\","
     "\"underivable\":\"A probe of the interpreter, not written in any repository.\"},\"band\":\"high"
     "\",\"claim\":\"Python's json.loads accepts a duplicate key by default.\",\"episode\":{\"client\""
     ":\"claude-code\",\"session\":\"00000000-0000-4000-8000-000000000039\"},\"evidence\":[{\"command"
     "\":\"python -I probe_dup.py\",\"digest\":\"hmac-sha256:5731583eea0d4d7677815b2dea880419d212b0063"
     "5f3a2724dee3e9dc7cd3c2e\",\"type\":\"output\"}],\"subject\":\"entity:cpython\"}",
     511},
    {"A header",
     "{\"at\":\"2026-10-17T09:30:00.000Z\",\"basis\":\"claimed\",\"content\":\"hmac-sha256:df0388fc101"
     "c4c0bc038ea2dc231dd5fadc5376af843e48755de9f638096be5e\",\"id\":\"sha256:902380391ae283f1a88e038c"
     "f525aea22d5e4ed0ca3f852c8acdf43457b3b119\",\"kind\":\"claim\",\"lane\":\"implementer\",\"scope\""
     ":\"project:qiven\",\"seq\":1,\"tag\":\"qiven-entry/1\",\"writer\":\"implementer/s39\"}",
     336},
    {"D content",
     "{\"admit\":{\"mistake\":\"A report would carry claims with no evidence pointer.\",\"underivable"
     "\":\"The owner's instruction, said in a session, not in a repository.\"},\"band\":\"core\",\"epi"
     "sode\":{\"client\":\"claude-code\",\"session\":\"00000000-0000-4000-8000-000000000039\"},\"subje"
     "ct\":\"activity:write-report\",\"words\":\"Give each claim its evidence pointer or mark it unver"
     "ified.\"}",
     360},
    {"L1 content",
     "{\"admit\":{\"mistake\":\"A test would pass on one interpreter and fail on the next pin.\",\"und"
     "erivable\":\"\345\234\250\344\274\232\350\257\235\344\270\255\350\247\202\345\257\237\345\210"
     "\260\347\232\204\347\273\223\346\236\234\357\274\214\346\262\241\346\234\211\345\206\231\350\277"
     "\233\344\273\273\344\275\225\344\273\223\345\272\223\343\200\202\"},\"band\":\"normal\",\"episod"
     "e\":{\"client\":\"claude-code\",\"session\":\"00000000-0000-4000-8000-000000000042\"},\"evidence"
     "\":[{\"command\":\"python -I --version\",\"digest\":\"hmac-sha256:0427965c77d19ad3af90f88a2e288a"
     "fa35e68082d468838b50d416e1e5e38474\",\"type\":\"output\"}],\"lesson\":\"\345\234\250 3.14.7 \344"
     "\270\212\357\274\214str.isspace \346\214\211 Unicode 16.0.0 \345\210\244\346\226\255\357\274\233"
     "pin the interpreter in every test.\",\"pins\":[{\"name\":\"python\",\"type\":\"tool\",\"version"
     "\":\"3.14.7\"},{\"name\":\"PYTHONUTF8\",\"type\":\"env\"}],\"subject\":\"entity:cpython\",\"trig"
     "ger\":{\"activities\":[\"pin-interpreter\"]},\"when\":{\"quality\":\"exact\",\"time\":\"2026-10-"
     "17T05:00:00.000Z\"}}",
     745},
    {"L1 header",
     "{\"at\":\"2026-10-17T05:02:10.000Z\",\"basis\":\"claimed\",\"content\":\"hmac-sha256:ca3d1e31fad"
     "36dce7c2a27c4015091cf85bf069b5929103d6169bc1b2dca8cf3\",\"id\":\"sha256:dbc3708cf045a0a4032f26cd"
     "c403298992821d648136a203a419002cf218334c\",\"kind\":\"lesson\",\"lane\":\"learner\",\"scope\":\""
     "project:qiven\",\"seq\":1,\"tag\":\"qiven-entry/1\",\"writer\":\"learner/s42\"}",
     329},
    {"L2 content",
     "{\"admit\":{\"mistake\":\"A lesson pinned to 3.14.7 would be served after the re-pin to 3.15.0."
     "\",\"underivable\":\"\346\215\242\347\211\210\346\234\254\345\220\216\351\207\215\346\226\260"
     "\350\277\220\350\241\214\347\232\204\350\276\223\345\207\272\357\274\214\346\262\241\346\234\211"
     "\345\206\231\350\277\233\344\273\273\344\275\225\344\273\223\345\272\223\343\200\202\"},\"band\""
     ":\"normal\",\"episode\":{\"client\":\"claude-code\",\"session\":\"00000000-0000-4000-8000-000000"
     "000043\"},\"evidence\":[{\"id\":\"sha256:dbc3708cf045a0a4032f26cdc403298992821d648136a203a419002"
     "cf218334c\",\"type\":\"entry\"},{\"command\":\"python -I --version\",\"digest\":\"hmac-sha256:9b"
     "2b49164905577f1312bffc2f690e450a2152c95c432fa0c52cc01047bef814\",\"type\":\"output\"}],\"lesson"
     "\":\"On 3.15.0, str.isspace follows Unicode 17.0.0 and holds the same 29 code points; \346\257"
     "\217\344\270\252\346\265\213\350\257\225\344\273\215\345\233\272\345\256\232\350\247\243\351\207"
     "\212\345\231\250\343\200\202\",\"pins\":[{\"name\":\"python\",\"type\":\"tool\",\"version\":\"3."
     "15.0\"},{\"name\":\"PYTHONUTF8\",\"type\":\"env\"}],\"subject\":\"entity:cpython\",\"trigger\":{"
     "\"activities\":[\"pin-interpreter\"]},\"when\":{\"quality\":\"session\",\"time\":\"2026-10-17T09"
     ":00:00.000Z\"}}",
     876},
    {"L2 header",
     "{\"at\":\"2026-10-17T09:40:00.000Z\",\"basis\":\"claimed\",\"content\":\"hmac-sha256:c5d45ae33ec"
     "18accb198b6f5a70d5b5421d84e96047d0f2c632ba932c542c356\",\"id\":\"sha256:566240684567f8e028910644"
     "9dddd847ec44a665313c8e09b32c5d48480f31c7\",\"kind\":\"lesson\",\"lane\":\"learner\",\"parent\":"
     "\"sha256:dbc3708cf045a0a4032f26cdc403298992821d648136a203a419002cf218334c\",\"scope\":\"project:"
     "qiven\",\"seq\":2,\"tag\":\"qiven-entry/1\",\"target\":\"sha256:dbc3708cf045a0a4032f26cdc4032989"
     "92821d648136a203a419002cf218334c\",\"writer\":\"learner/s43\"}",
     495},
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

std::string shown(std::optional<ParseCode> c) { return c ? std::string(qiven3::name(*c)) : "accepted"; }

struct Row {
    std::string data;
    std::optional<ParseCode> code;
};

void check_rows(qiven3::test::Context& qiven3_ctx, const std::vector<Row>& rows) {
    for (const Row& r : rows) {
        const std::optional<ParseCode> got = code(r.data);
        if (!CHECK(got == r.code)) {
            qiven3_ctx.out << "  vector " << r.data.substr(0, 80) << ": " << shown(got) << ", not " << shown(r.code)
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

QIVEN3_CASE(canon, refusals) {
    // The first table of the refusals, each row's bytes with its code.
    check_rows(qiven3_ctx, {
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

QIVEN3_CASE(canon, edge_refusals) {
    // The edge vectors: the pass's codes win over the walk's wherever each
    // sits, and inside a member the key's characters, then its rule, then
    // its value.
    check_rows(qiven3_ctx, {
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

QIVEN3_CASE(canon, accepted_round_trips) {
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
        // The content digests' objects, and the entries' lines.
        "{}",
        std::string(cv::cv2),
        std::string(cv::cv3),
    };
    for (const std::string& data : accepted) {
        if (!CHECK(round_trip(data))) {
            qiven3_ctx.out << "  vector " << data.substr(0, 80) << ": " << shown(code(data)) << "\n";
        }
    }
    for (const cv::Line& line : cv::lines) {
        if (!CHECK(round_trip(line.bytes)) || !CHECK_EQ(line.bytes.size(), line.size)) {
            qiven3_ctx.out << "  line " << line.name << "\n";
        }
    }
}

QIVEN3_CASE(canon, encoder) {
    // The digests' objects as the text shows them, with ASCII escapes:
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

QIVEN3_CASE(canon, walk) {
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
