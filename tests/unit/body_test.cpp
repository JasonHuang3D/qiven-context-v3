// body's cases: the record's layout and its codes in parse's order, one
// change each and one pair of faults for each adjacent pair; the record
// written from its parts and read back at their places; the id's and the
// key's syntaxes, the id's against header's pattern. Each is evaluated by
// the compiler on std::allocator. At run time every body of
// tests/fixtures/ is made from its content, key and header, its digest the
// header's content, written, parsed back to the same record and checked
// against its header; A's record refused by each body row of the
// refusals' table with its code, or shorter than its frame; make's refusals;
// the key's hex read and written back, its stack copy wiped; the pinned sizes.

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "entry/matcher.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace body = qiven3::entry::body;
namespace header = qiven3::entry::header;
namespace matcher = qiven3::entry::matcher;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;
using body::Code;
using header::Slot;
using Fake = qiven3::platform::fake::FakePlatform;
using Body = body::Body<Fake>;

static_assert(sizeof(digest::Key<Fake>) == 32 + sizeof(void*), "a key's size is pinned: its 32 and its platform's");
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(body::Body<Fake, qiven3::memory::Allocator<char>>) == 80,
              "the body's size is pinned: its text, 40, and its key, 40");
#endif

constexpr std::string_view a_id = "sha256:902380391ae283f1a88e038cf525aea22d5e4ed0ca3f852c8acdf43457b3b119";
constexpr std::string_view b_id = "sha256:d31aa78fed463a5bd4ca299004ddd3dd3844f62befd8d819ce8d4fbbe0459460";
constexpr std::string_view k0 = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

// A record of the empty content under A's id and the digest vectors' key.
constexpr std::string_view r0 =
    "{\"content\":{},\"id\":\"sha256:902380391ae283f1a88e038cf525aea22d5e4ed0ca3f852c8acdf43457b3b119\",\"key\":\""
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\"}";

constexpr std::string edited(std::string_view text, std::string_view from, std::string_view to) {
    std::string out(text);
    const std::size_t at = out.find(from);
    return at == std::string::npos ? std::string() : out.replace(at, from.size(), to);
}

constexpr std::optional<Code> code_of(std::string_view record) {
    qiven3::json::Basic<> v;
    canon::parse(record, v);
    return body::detail::layout(v);
}
constexpr bool refused(std::string_view record, Code code) { return code_of(record) == code; }

constexpr std::string_view without_key = ",\"key\":\"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\"";
constexpr std::string_view upper_key = "000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F";

// The layout: r0 holds; each change its code.
static_assert(!code_of(r0));
static_assert(refused(edited(r0, "\"}", "\",\"note\":\"x\"}"), Code::unknown_key));
static_assert(refused(edited(r0, without_key, ""), Code::missing_key));
static_assert(refused(edited(r0, k0, upper_key), Code::syntax));
static_assert(refused(edited(r0, k0.substr(0, 2), ""), Code::syntax));
static_assert(refused(edited(r0, a_id, "sha256:00"), Code::syntax));
static_assert(refused(edited(r0, "sha256:", "sha512:"), Code::syntax));
static_assert(refused(edited(r0, "{}", "[]"), Code::syntax));
static_assert(refused(edited(r0, "{}", "\"x\""), Code::syntax));
static_assert(refused("[]", Code::syntax) && refused("1", Code::syntax));
static_assert(refused("{}", Code::missing_key));
// The order: unknown_key before missing_key, missing_key before syntax.
static_assert(refused(edited(edited(r0, without_key, ""), "\"}", "\",\"note\":\"x\"}"), Code::unknown_key));
static_assert(refused(edited(edited(r0, without_key, ""), a_id, "sha256:00"), Code::missing_key));

// The parts at their places, and the record written from them.
static_assert(body::detail::parts(r0).content == "{}");
static_assert(body::detail::parts(r0).id == a_id);
static_assert(body::detail::parts(r0).key == k0);
static_assert([] {
    std::string out = "kept";
    body::detail::write("{}", a_id,
                        [](std::span<char, body::key_size> hex) { std::copy(k0.begin(), k0.end(), hex.begin()); }, out);
    return out == r0;
}());

// The id's syntax, the same as header's pattern; the key's.
constexpr bool id_agrees(std::string_view text) {
    const matcher::Basic<> m = matcher::compile(header::id_syntax);
    return matcher::hit(m, text) == body::detail::id_text(text);
}
static_assert(body::detail::id_text(a_id) && id_agrees(a_id) && id_agrees(b_id));
static_assert(!body::detail::id_text("sha256:00") && id_agrees("sha256:00") && id_agrees(""));
static_assert(id_agrees(edited(a_id, "9023", "9O23")) && id_agrees(edited(a_id, "ae", "AE")));
static_assert(id_agrees(std::string(a_id) + "0") && id_agrees(edited(a_id, "sha256", "sha257")));
static_assert(digest::key_text(k0) && !digest::key_text(upper_key) && !digest::key_text(k0.substr(1)));
static_assert(!digest::key_text(std::string(k0) + "0") && !digest::key_text(edited(k0, "0a", "0g")));

// A refusal thrown, as a code's name.
template <typename Op>
std::string thrown(const Op& op) {
    try {
        op();
    } catch (const body::BodyError& e) {
        return std::string(body::name(e.code));
    } catch (const qiven3::ParseError& e) {
        return e.code == qiven3::ParseCode::depth ? "depth" : "parse";
    }
    return "none";
}

}  // namespace

QIVEN3_CASE(body, fixtures) {
    Fake fake(qiven3::platform::fake::Config{});
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::size_t made = 0;
    std::size_t none = 0;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        const header::Header<> h = header::parse_line(e.header);
        if (!e.key) {
            CHECK(!e.content.has_value());
            CHECK(!h.present(Slot::content));
            ++none;
            continue;
        }
        REQUIRE(e.content.has_value());
        const Body b = body::make(*e.content, digest::key_from_hex(fake, *e.key), h.value(Slot::id));
        std::string d;
        b.content_digest(d);
        CHECK_EQ(d, std::string(h.value(Slot::content)));
        CHECK_EQ(thrown([&] { body::check(b, h); }), "none");
        std::string record;
        b.line(record);
        CHECK_EQ(record, std::string(body::head) + std::string(*e.content) + ",\"id\":\"" +
                             std::string(h.value(Slot::id)) + "\",\"key\":\"" + std::string(*e.key) + "\"}");
        const Body read = body::parse(fake, record);
        CHECK_EQ(std::string(read.content()), std::string(*e.content));
        CHECK_EQ(std::string(read.id()), std::string(h.value(Slot::id)));
        std::string again;
        read.line(again);
        CHECK_EQ(again, record);
        CHECK_EQ(thrown([&] { body::check(read, h); }), "none");
        ++made;
    }
    std::cout << "body.fixtures: " << made << " bodies made, written, read back and checked; " << none
              << " entries with none\n";
    CHECK_EQ(made + none, 12U);
    CHECK(made >= 10U);
}

QIVEN3_CASE(body, refusals) {
    Fake fake(qiven3::platform::fake::Config{});
    canon::Basic<> v;
    fixtures::load_entries(v);
    const std::optional<fixtures::EntryView> a = fixtures::find_entry(v, "A");
    REQUIRE(a.has_value() && a->key && a->content);
    const header::Header<> h = header::parse_line(a->header);
    std::string record;
    body::make(*a->content, digest::key_from_hex(fake, *a->key), h.value(Slot::id)).line(record);
    const auto parse_check = [&](std::string text) {
        return [&fake, &h, text] { body::check(body::parse(fake, text), h); };
    };
    CHECK_EQ(thrown(parse_check(record)), "none");
    // The table's body rows, each one change to A's record.
    CHECK_EQ(thrown(parse_check(record.substr(0, record.size() - 2) + "\",\"note\":\"x\"}")), "unknown_key");
    CHECK_EQ(thrown(parse_check(edited(record, ",\"key\":\"" + std::string(*a->key) + "\"", ""))), "missing_key");
    CHECK_EQ(thrown(parse_check(edited(record, "\"band\":\"high\"", "\"band\":\"low\""))), "body_mismatch");
    CHECK_EQ(thrown(parse_check(edited(record, a_id, b_id))), "body_mismatch");
    std::string upper(*a->key);
    std::transform(upper.begin(), upper.end(), upper.begin(), [](char c) { return static_cast<char>(std::toupper(c)); });
    CHECK(upper != std::string(*a->key));
    CHECK_EQ(thrown(parse_check(edited(record, *a->key, upper))), "syntax");
    // Shorter than its frame: parse's refusal before the parts' places are read.
    for (std::size_t n = 0; n < body::frame_size; ++n) {
        CHECK_EQ(thrown(parse_check(record.substr(0, n))), "parse");
    }
    CHECK_EQ(thrown(parse_check(R"({"content":{},"id":"sha256:0","key":"0"})")), "syntax");
    // No record: the parser's refusal, or a root that is no object.
    CHECK_EQ(thrown(parse_check(edited(record, "{\"content\":", "{\"content\": "))), "parse");
    for (const std::string_view root : {"[]", "\"x\"", "1", "true"}) {
        CHECK_EQ(thrown(parse_check(std::string(root))), "syntax");
    }
    // Another entry's key: its digest is not the header's.
    const std::optional<fixtures::EntryView> b = fixtures::find_entry(v, "B");
    REQUIRE(b.has_value() && b->key);
    CHECK_EQ(thrown(parse_check(edited(record, *a->key, *b->key))), "body_mismatch");
    // A forget's header holds no content: a body against it, its id kept.
    header::FieldsView f;
    f.scope = "project:qiven";
    f.lane = "implementer";
    f.kind = "forget";
    f.writer = "implementer/s39";
    f.basis = "claimed";
    f.at = "2026-10-17T09:30:00.000Z";
    f.target = a_id;
    const header::Header<> forget = header::make(f);
    CHECK(!header::try_check(forget));
    CHECK_EQ(thrown([&] {
                 body::check(body::make("{}", digest::key_from_hex(fake, k0), forget.value(Slot::id)), forget);
             }),
             "body_mismatch");
    // make's refusals: a content that is no object, not canonical or past
    // 15 levels; an id off its syntax.
    const auto make = [&fake](std::string content, std::string id) {
        return [&fake, content, id] { static_cast<void>(body::make(content, digest::key_from_hex(fake, k0), id)); };
    };
    CHECK_EQ(thrown(make("{}", std::string(a_id))), "none");
    CHECK_EQ(thrown(make("[]", std::string(a_id))), "syntax");
    CHECK_EQ(thrown(make("\"x\"", std::string(a_id))), "syntax");
    CHECK_EQ(thrown(make("{\"a\": 1}", std::string(a_id))), "parse");
    CHECK_EQ(thrown(make("{}", "sha256:00")), "syntax");
    CHECK_EQ(thrown(make("{}", edited(a_id, "ae", "AE"))), "syntax");
    std::string deep = "{}";
    for (int i = 0; i < 14; ++i) {
        deep = "{\"a\":" + deep + "}";
    }
    CHECK_EQ(thrown(make(deep, std::string(a_id))), "none");  // 15 levels
    std::string record15;
    body::make(deep, digest::key_from_hex(fake, k0), a_id).line(record15);
    CHECK_EQ(thrown([&] { static_cast<void>(body::parse(fake, record15)); }), "none");
    CHECK_EQ(thrown(make("{\"a\":" + deep + "}", std::string(a_id))), "depth");  // 16
    // The codes' names, in their order.
    CHECK_EQ(body::code_names.size(), 4U);
    CHECK_EQ(std::string(body::name(Code::body_mismatch)), "body_mismatch");
}

QIVEN3_CASE(body, key) {
    Fake fake(qiven3::platform::fake::Config{});
    const std::size_t before = fake.calls().size();
    const digest::Key<Fake> key = digest::key_from_hex(fake, k0);
    // The decoded copy on the stack, wiped: the one platform call.
    REQUIRE(fake.calls().size() == before + 1);
    CHECK_EQ(fake.calls().back(), "wipe");
    std::array<char, body::key_size> hex{};
    digest::key_hex(key, std::span<char, body::key_size>(hex));
    CHECK_EQ(std::string(hex.data(), hex.size()), std::string(k0));
    // Its digest of the empty object, the vectors' cv1.
    const Body b = body::make("{}", digest::key_from_hex(fake, k0), a_id);
    std::string d;
    b.content_digest(d);
    CHECK_EQ(d, "hmac-sha256:86227fe96722a2247dcfee12c07d89c1b0567bbc9aa6a8d804838f66a3da843f");
    std::string record;
    b.line(record);
    CHECK_EQ(record, std::string(r0));
    // parse: the decoded copy's wipe, then the key's move into the body
    // and its parameter's end; content_digest: keyed's one.
    const std::size_t at = fake.calls().size();
    const Body read = body::parse(fake, record);
    CHECK_EQ(fake.calls().size(), at + 3);
    const std::size_t drawn = static_cast<std::size_t>(std::count(fake.calls().begin(), fake.calls().end(), "wipe"));
    std::string again;
    read.content_digest(again);
    CHECK_EQ(again, d);
    CHECK_EQ(static_cast<std::size_t>(std::count(fake.calls().begin(), fake.calls().end(), "wipe")), drawn + 1);
}
