// check's cases: part 1's order, the parser's before the header's, the
// header's before the record's, the record's layout before its binding,
// the binding before the content's, and a header with no body meeting
// basis and scope alone, evaluated by the compiler over entries built
// here; at run time every fixture entry with its record and with none,
// the validator's refusals of A (each row of the format's table, the line's
// id kept), the parser's refusals on the line and on the record, a content
// past 65,536 bytes, and each fault thrown as its part's error.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "entry/body.hpp"
#include "entry/check.hpp"
#include "entry/content.hpp"
#include "entry/header.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace check = qiven3::entry::check;
namespace body = qiven3::entry::body;
namespace header = qiven3::entry::header;
namespace content = qiven3::entry::content;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
namespace fixtures = qiven3::test::fixtures;
using check::Fault;
using check::Part;
using header::Slot;
using Fake = qiven3::platform::fake::FakePlatform;
using Entry = check::Entry<Fake>;

#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(check::Entry<Fake, qiven3::memory::Allocator<char>>) == 288,
              "the entry's size is pinned: its header, 200, and its optional body, 88");
#endif

constexpr std::string_view k0 = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
constexpr std::string_view b_id = "sha256:d31aa78fed463a5bd4ca299004ddd3dd3844f62befd8d819ce8d4fbbe0459460";
constexpr std::string_view at_text = "2026-10-17T09:30:00.000Z";

constexpr unsigned nibble(char c) { return c <= '9' ? static_cast<unsigned>(c - '0') : static_cast<unsigned>(c - 'a' + 10); }

// The keyed digest's text of data under a key given as hex, by hmac.
constexpr std::string keyed(std::string_view key_hex, std::string_view data) {
    std::string key;
    for (std::size_t i = 0; i + 1 < key_hex.size(); i += 2) {
        key += static_cast<char>((nibble(key_hex[i]) << 4U) | nibble(key_hex[i + 1]));
    }
    std::string out;
    digest::detail::write_hex("hmac-sha256:", digest::hmac(key, data), out);
    return out;
}

// The binding as check_entry's, the digest by hmac over the record's key.
struct Bind {
    constexpr bool operator()(const body::detail::RecordView& parts, const header::Header<>& h) const {
        return parts.id == h.value(Slot::id) && h.present(Slot::content) &&
               keyed(parts.key, parts.content) == h.value(Slot::content);
    }
};

// An entry's line and record, its content's digest under k0 and its id
// computed.
struct Made {
    std::string line;
    std::string record;
};

constexpr Made made(header::FieldsView f, std::string_view value, std::optional<std::string_view> digest_of = {}) {
    const std::string d = keyed(k0, digest_of.value_or(value));
    if (f.kind != "forget") {
        f.content = d;
    }
    const header::Header<> h = header::make(f);
    Made m;
    h.line(m.line);
    body::detail::write(
        value, h.value(Slot::id),
        [](std::span<char, body::key_size> hex) {
            for (std::size_t i = 0; i < k0.size(); ++i) {
                hex[i] = k0[i];
            }
        },
        m.record);
    return m;
}

constexpr header::FieldsView direction() {
    header::FieldsView f;
    f.scope = "person";
    f.lane = "import";
    f.kind = "direction";
    f.writer = "import";
    f.basis = "owner-words";
    f.at = at_text;
    return f;
}

constexpr std::string_view direction_words =
    R"({"admit":{"mistake":"m","underivable":"u"},"band":"core","subject":"entity:owner","words":"w"})";
constexpr std::string_view unknown_words =
    R"({"admit":{"mistake":"m","underivable":"u"},"band":"core","subject":"entity:owner","words":"w","zz":1})";

constexpr std::string edited(std::string_view text, std::string_view from, std::string_view to) {
    std::string out(text);
    const std::size_t where = out.find(from);
    return where == std::string::npos ? std::string() : out.replace(where, from.size(), to);
}

constexpr std::optional<Fault> run(std::string_view line, std::optional<std::string_view> record) {
    header::Header<> h;
    return check::try_check(line, record, h, Bind{});
}

template <typename C>
constexpr bool refused(std::string_view line, std::optional<std::string_view> record, Part part, C code) {
    const std::optional<Fault> f = run(line, record);
    return f && f->part == part && f->code == static_cast<std::uint8_t>(code);
}

constexpr bool admitted(std::string_view line, std::optional<std::string_view> record) {
    return !run(line, record).has_value();
}

constexpr header::FieldsView with(header::FieldsView f, std::string_view header::FieldsView::*field,
                                  std::string_view value) {
    f.*field = value;
    return f;
}

// An entry's line and record held in place, so that each vector builds
// its entries once, apart from its run.
struct Fixed {
    std::array<char, 1024> line_bytes{};
    std::size_t line_size = 0;
    std::array<char, 1024> record_bytes{};
    std::size_t record_size = 0;
    [[nodiscard]] constexpr std::string_view line() const { return {line_bytes.data(), line_size}; }
    [[nodiscard]] constexpr std::string_view record() const { return {record_bytes.data(), record_size}; }
};

constexpr Fixed fix(const Made& m) {
    Fixed f;
    for (const char c : m.line) {
        f.line_bytes.at(f.line_size++) = c;
    }
    for (const char c : m.record) {
        f.record_bytes.at(f.record_size++) = c;
    }
    return f;
}

constexpr header::FieldsView kind_of(std::string_view kind, std::string_view basis) {
    header::FieldsView f = direction();
    f.kind = kind;
    f.basis = basis;
    if (kind == "forget") {
        f.target = b_id;
    }
    return f;
}

constexpr Fixed e_plain = fix(made(direction(), direction_words));
constexpr Fixed e_lane = fix(made(with(direction(), &header::FieldsView::lane, "reviewer"), direction_words));
constexpr Fixed e_unknown = fix(made(direction(), unknown_words));
constexpr Fixed e_unbound = fix(made(direction(), unknown_words, direction_words));
constexpr Fixed e_derived = fix(made(kind_of("direction", "derived"), unknown_words));
constexpr Fixed e_map = fix(made(kind_of("map", "derived"), direction_words));
constexpr Fixed e_forget = fix(made(kind_of("forget", "owner-words"), direction_words));

// The vectors, each one run, and what each gives: a part and its code, or
// none.
struct Want {
    std::string_view name;
    bool admitted = false;
    Part part = Part::parse;
    std::uint8_t code = 0;
};
template <typename C>
constexpr Want refusal(std::string_view name, Part part, C code) {
    return Want{name, false, part, static_cast<std::uint8_t>(code)};
}
constexpr Want admission(std::string_view name) { return Want{name, true, Part::parse, 0}; }

constexpr std::array<Want, 20> wants = {
    admission("an entry"),
    admission("its header alone"),
    refusal("the line's bom before the record's fault", Part::parse, qiven3::ParseCode::bom),
    refusal("the record's parse fault", Part::parse, qiven3::ParseCode::not_canonical),
    refusal("the record's parse fault before the header's lane", Part::parse, qiven3::ParseCode::not_canonical),
    refusal("the record's parse fault before a root no object", Part::parse, qiven3::ParseCode::bom),
    refusal("a root no object, with a record", Part::header, header::Code::syntax),
    refusal("a root no object, alone", Part::header, header::Code::syntax),
    refusal("the header's lane before the record's layout", Part::header, header::Code::lane),
    refusal("the record's unknown key", Part::body, body::Code::unknown_key),
    refusal("the record's missing key before its id", Part::body, body::Code::missing_key),
    refusal("the record's id another's", Part::body, body::Code::body_mismatch),
    refusal("the record's content changed", Part::body, body::Code::body_mismatch),
    refusal("the content's unknown field, bound", Part::content, content::Code::unknown_field),
    refusal("the binding before the content's codes", Part::body, body::Code::body_mismatch),
    admission("an unknown field's header alone"),
    refusal("a header alone meets basis", Part::content, content::Code::basis),
    refusal("basis before the content's own codes", Part::content, content::Code::basis),
    refusal("a header alone meets scope", Part::content, content::Code::scope),
    admission("a forget alone"),
};

constexpr std::optional<Fault> vector(std::size_t i) {
    const std::string spaced = edited(e_plain.record(), "{\"content\":", "{\"content\": ");
    const std::string noted = std::string(e_plain.record().substr(0, e_plain.record().size() - 2)) + "\",\"note\":\"x\"}";
    const std::string id(e_plain.line().substr(e_plain.line().find("\"id\":\"") + 6, 71));
    switch (i) {
        case 0: return run(e_plain.line(), e_plain.record());
        case 1: return run(e_plain.line(), std::nullopt);
        case 2: return run("\xef\xbb\xbf" + std::string(e_plain.line()), spaced);
        case 3: return run(e_plain.line(), spaced);
        case 4: return run(e_lane.line(), spaced);
        case 5: return run("[]", "\xef\xbb\xbf{}");
        case 6: return run("[]", e_plain.record());
        case 7: return run("\"x\"", std::nullopt);
        case 8: return run(e_lane.line(), noted);
        case 9: return run(e_plain.line(), noted);
        case 10: {
            const std::string_view r = e_plain.record();
            return run(e_plain.line(), edited(std::string(r.substr(0, r.find(",\"key\":"))) + "}", id, b_id));
        }
        case 11: return run(e_plain.line(), edited(e_plain.record(), id, b_id));
        case 12: return run(e_plain.line(), edited(e_plain.record(), "\"words\":\"w\"", "\"words\":\"x\""));
        case 13: return run(e_unknown.line(), e_unknown.record());
        case 14: return run(e_unbound.line(), e_unbound.record());
        case 15: return run(e_unknown.line(), std::nullopt);
        case 16: return run(e_derived.line(), std::nullopt);
        case 17: return run(e_derived.line(), e_derived.record());
        case 18: return run(e_map.line(), std::nullopt);
        case 19: return run(e_forget.line(), std::nullopt);
        default: return std::nullopt;
    }
}

constexpr bool holds(std::size_t i) {
    const std::optional<Fault> f = vector(i);
    const Want& w = wants.at(i);
    return w.admitted ? !f.has_value() : (f && f->part == w.part && f->code == w.code);
}

static_assert(holds(0));
static_assert(holds(1));
static_assert(holds(2));
static_assert(holds(3));
static_assert(holds(4));
static_assert(holds(5));
static_assert(holds(6));
static_assert(holds(7));
static_assert(holds(8));
static_assert(holds(9));
static_assert(holds(10));
static_assert(holds(11));
static_assert(holds(12));
static_assert(holds(13));
static_assert(holds(14));
static_assert(holds(15));
static_assert(holds(16));
static_assert(holds(17));
static_assert(holds(18));
static_assert(holds(19));

// A refusal thrown, as its code's name.
template <typename Op>
std::string thrown(const Op& op) {
    try {
        op();
    } catch (const qiven3::ParseError& e) {
        return "parse " + std::string(qiven3::name(e.code));
    } catch (const header::HeaderError& e) {
        return "header " + std::string(header::name(e.code));
    } catch (const body::BodyError& e) {
        return "body " + std::string(body::name(e.code));
    } catch (const content::ContentError& e) {
        return "content " + std::string(content::name(e.code));
    }
    return "none";
}

std::string record_of(Fake& fake, const fixtures::EntryView& e) {
    const header::Header<> h = header::parse_line(e.header);
    std::string out;
    body::make(*e.content, digest::key_from_hex(fake, *e.key), h.value(Slot::id)).line(out);
    return out;
}

std::string repeat(std::string_view s, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += s;
    }
    return out;
}

}  // namespace

QIVEN3_CASE(check, vectors) {
    // The compiler's vectors again at run time, each named.
    std::size_t held = 0;
    for (std::size_t i = 0; i < wants.size(); ++i) {
        const std::optional<Fault> f = vector(i);
        const std::string got = f ? std::string(check::name(*f)) : "none";
        if (!holds(i)) {
            std::cout << "check.vectors: " << wants.at(i).name << ": " << got << "\n";
        }
        CHECK(holds(i));
        held += holds(i) ? 1U : 0U;
    }
    std::cout << "check.vectors: " << held << " of " << wants.size() << " as wanted\n";
}

QIVEN3_CASE(check, fixtures) {
    Fake fake(qiven3::platform::fake::Config{});
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::size_t bodies = 0;
    std::size_t none = 0;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        const Entry alone = check::check_entry(fake, e.header, std::nullopt);
        CHECK(!alone.has_body());
        CHECK_EQ(std::string(alone.header().value(Slot::kind)),
                 std::string(header::parse_line(e.header).value(Slot::kind)));
        if (!e.key) {
            ++none;
            continue;
        }
        const std::string record = record_of(fake, e);
        const Entry entry = check::check_entry(fake, e.header, record);
        REQUIRE(entry.has_body());
        CHECK_EQ(std::string(entry.body().content()), std::string(*e.content));
        CHECK_EQ(std::string(entry.body().id()), std::string(entry.header().value(Slot::id)));
        ++bodies;
    }
    std::cout << "check.fixtures: " << bodies << " entries admitted with their records, " << bodies + none
              << " headers with none, " << none << " of them with no content\n";
    CHECK_EQ(bodies + none, 12U);
    CHECK(bodies >= 10U);
}

QIVEN3_CASE(check, refusals) {
    Fake fake(qiven3::platform::fake::Config{});
    canon::Basic<> v;
    fixtures::load_entries(v);
    const std::optional<fixtures::EntryView> a = fixtures::find_entry(v, "A");
    const std::optional<fixtures::EntryView> b = fixtures::find_entry(v, "B");
    REQUIRE(a.has_value() && a->key && a->content && b.has_value());
    const std::string line(a->header);
    const std::string record = record_of(fake, *a);
    const std::string a_id(header::parse_line(line).value(Slot::id));
    const std::string b_id_text(header::parse_line(b->header).value(Slot::id));
    const auto run_check = [&](const std::string& l, const std::string& r) {
        return thrown([&] { (void)check::check_entry(fake, l, r); });
    };
    const std::string lane = "\"lane\":\"implementer\"";
    const std::string writer = "\"writer\":\"implementer/s39\"";
    const auto slot = [&](std::string_view role, std::string_view n) {
        return edited(edited(line, lane, "\"lane\":\"" + std::string(role) + "\""), writer,
                      "\"writer\":\"" + std::string(role) + "/" + std::string(n) + "\"");
    };
    CHECK_EQ(run_check(line, record), "none");
    // The table's rows, each one change to A, the line's id kept.
    CHECK_EQ(run_check(slot("owner", "s1"), record), "header owner");
    CHECK_EQ(run_check(slot("loop", "s1"), record), "header reserved");
    CHECK_EQ(run_check(slot("import", "s1"), record), "header reserved");
    CHECK_EQ(run_check(edited(edited(line, "\"scope\"", "\"parent\":\"\",\"scope\""), "\"seq\":1", "\"seq\":2"), record),
             "header empty");
    CHECK_EQ(run_check(edited(line, "\"seq\":1", "\"seq\":2"), record), "header seq");
    CHECK_EQ(run_check(edited(line, "09:30:00.000Z", "09:30:00Z"), record), "header syntax");
    CHECK_EQ(run_check(edited(line, "2026-10-17T09:30", "2026-02-30T09:30"), record), "header syntax");
    CHECK_EQ(run_check(edited(line, lane, "\"lane\":\"reviewer\""), record), "header lane");
    CHECK_EQ(run_check(edited(slot("reflect", "s39"), "\"kind\":\"claim\"", "\"kind\":\"direction\""), record),
             "header reflect_kind");
    CHECK_EQ(run_check(edited(line, "\"kind\":\"claim\"", "\"kind\":\"check\""), record), "header target");
    CHECK_EQ(run_check(edited(line, "\"basis\":\"claimed\"", "\"basis\":\"observed\""), record), "header id_mismatch");
    CHECK_EQ(run_check(edited(line, lane, "\"lane\":\"implementer\",\"note\":\"x\""), record), "header unknown_key");
    CHECK_EQ(run_check(line, record.substr(0, record.size() - 2) + "\",\"note\":\"x\"}"), "body unknown_key");
    CHECK_EQ(run_check(edited(line, "," + writer, ""), record), "header missing_key");
    CHECK_EQ(run_check(edited(line, ",\"id\":\"" + a_id + "\"", ""), record), "header missing_key");
    CHECK_EQ(run_check(line, edited(record, ",\"key\":\"" + std::string(*a->key) + "\"", "")), "body missing_key");
    CHECK_EQ(run_check(line, edited(record, "\"band\":\"high\"", "\"band\":\"low\"")), "body body_mismatch");
    CHECK_EQ(run_check(line, edited(record, a_id, b_id_text)), "body body_mismatch");
    std::string upper(*a->key);
    for (char& c : upper) {
        c = (c >= 'a' && c <= 'f') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    CHECK_EQ(run_check(line, edited(record, *a->key, upper)), "body syntax");
}

QIVEN3_CASE(check, parser) {
    Fake fake(qiven3::platform::fake::Config{});
    canon::Basic<> v;
    fixtures::load_entries(v);
    const std::optional<fixtures::EntryView> a = fixtures::find_entry(v, "A");
    REQUIRE(a.has_value() && a->key && a->content);
    const std::string line(a->header);
    const std::string record = record_of(fake, *a);
    const std::string nested = repeat("{\"a\":", 16) + "{}" + repeat("}", 16);
    const std::array<std::pair<std::string, std::string_view>, 13> rows = {{
        {"\xef\xbb\xbf{}", "bom"},
        {"{\"a\":1,\"a\":2}", "duplicate_key"},
        {"{\"a\":NaN}", "constant"},
        {"{\"a\":1.0}", "float"},
        {"{\"a\":9007199254740992}", "int_range"},
        {"{\"a\":\"\\ud800\"}", "surrogate"},
        {"{\"a\":null}", "null"},
        {"{\"A\":1}", "key_syntax"},
        {"{\"a\": 1}", "not_canonical"},
        {"{\"a\":\"\xff\"}", "utf8"},
        {"{\"a\":\"\xed\xa0\x80\"}", "utf8"},
        {nested, "depth"},
        {"{\"a\":\"\\ufdd0\"}", "nonchar"},
    }};
    std::size_t refused = 0;
    for (const auto& [bytes, code] : rows) {
        CHECK_EQ(thrown([&] { (void)check::check_entry(fake, bytes, record); }), "parse " + std::string(code));
        CHECK_EQ(thrown([&] { (void)check::check_entry(fake, line, bytes); }), "parse " + std::string(code));
        CHECK_EQ(thrown([&] { (void)check::check_entry(fake, bytes, std::nullopt); }), "parse " + std::string(code));
        refused += 3;
    }
    std::cout << "check.parser: " << refused << " refusals, each row on the line and on the record\n";
}

QIVEN3_CASE(check, large) {
    // A module past 65,536 bytes canonical, each field within its limit,
    // bound to its header: content's too_large through part 1.
    Fake fake(qiven3::platform::fake::Config{});
    const std::string face = "\xf0\x9f\x98\x80";
    const auto items = [](const std::string& item, std::size_t n) {
        std::string out = "[";
        for (std::size_t i = 0; i < n; ++i) {
            out += (i == 0 ? "" : ",") + item;
        }
        return out + "]";
    };
    const std::string path = "\"" + repeat(face, 200) + "\"";
    const std::string subject = "\"entity:" + repeat(face, 153) + "\"";
    const std::string big = R"({"admit":{"mistake":"m","underivable":"u"},"applies_to":{"entities":)" +
                            items(subject, 16) + R"(,"environment":)" + items(path, 8) + R"(,"paths":)" +
                            items(path, 16) + R"(},"band":"normal","interface":)" + items(path, 16) +
                            R"(,"invariants":)" + items("\"" + repeat(face, 300) + "\"", 16) + R"(,"paths":)" +
                            items(path, 32) + R"(,"responsibility":")" + repeat(face, 500) +
                            R"(","subject":"module:m"})";
    header::FieldsView f = direction();
    f.scope = "project:qiven";
    f.kind = "module";
    f.basis = "derived";
    const Made m = made(f, big);
    CHECK(big.size() > 65536U);
    CHECK_EQ(thrown([&] { (void)check::check_entry(fake, m.line, m.record); }), "content too_large");
    CHECK_EQ(thrown([&] { (void)check::check_entry(fake, m.line, std::nullopt); }), "none");
    std::cout << "check.large: " << big.size() << " bytes too_large, its header alone admitted\n";
}

QIVEN3_CASE(check, raise) {
    // Each part's fault thrown as its error, and named as the format does.
    CHECK_EQ(thrown([] { check::raise(Fault{Part::parse, static_cast<std::uint8_t>(qiven3::ParseCode::floating), {}}); }),
             "parse float");
    CHECK_EQ(std::string(check::name(Fault{Part::parse, static_cast<std::uint8_t>(qiven3::ParseCode::floating), {}})),
             "float");
    CHECK_EQ(thrown([] { check::raise(Fault{Part::header, static_cast<std::uint8_t>(header::Code::lane), {}}); }),
             "header lane");
    CHECK_EQ(thrown([] { check::raise(Fault{Part::body, static_cast<std::uint8_t>(body::Code::body_mismatch), {}}); }),
             "body body_mismatch");
    content::ContentError e;
    e.code = content::Code::too_long;
    e.limit = 500;
    e.value = 501;
    const Fault f{Part::content, static_cast<std::uint8_t>(e.code), e};
    CHECK_EQ(std::string(check::name(f)), "too_long");
    try {
        check::raise(f);
    } catch (const content::ContentError& thrown_error) {
        CHECK_EQ(thrown_error.limit, 500U);
        CHECK_EQ(thrown_error.value, 501U);
    }
}
