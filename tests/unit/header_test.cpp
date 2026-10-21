// header's cases: A's line and the refusals of A's header, one change each,
// each giving its code, and the order of the codes, one pair of faults
// each, the earlier code winning; the syntaxes' edges; make's id and the
// line written back byte for byte; stamp's raise. Each is evaluated by the
// compiler on std::allocator, and the rows again at run time from
// tests/fixtures/, where every entry's header passes, is written back to
// its own line and gives its own id; the refusals parse_line and line
// throw; stamp on the fake platform's clock.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "entry/header.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/utc.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
namespace utc = qiven3::utc;
using header::Code;
using header::FieldsView;
using header::Slot;
using Fake = qiven3::platform::fake::FakePlatform;

// A's stored header line, as the fixtures hold it.
constexpr std::string_view a_line =
    "{\"at\":\"2026-10-17T09:30:00.000Z\",\"basis\":\"claimed\",\"content\":\"hmac-sha256:df0388fc101c4c0bc038ea2"
    "dc231dd5fadc5376af843e48755de9f638096be5e\",\"id\":\"sha256:902380391ae283f1a88e038cf525aea22d5e4ed0ca3f852c8"
    "acdf43457b3b119\",\"kind\":\"claim\",\"lane\":\"implementer\",\"scope\":\"project:qiven\",\"seq\":1,\"tag\":\"q"
    "iven-entry/1\",\"writer\":\"implementer/s39\"}";
constexpr std::string_view a_id = "sha256:902380391ae283f1a88e038cf525aea22d5e4ed0ca3f852c8acdf43457b3b119";
constexpr std::string_view b_id = "sha256:d31aa78fed463a5bd4ca299004ddd3dd3844f62befd8d819ce8d4fbbe0459460";

// A's fields, its id kept.
constexpr FieldsView a_fields() {
    FieldsView f;
    f.scope = "project:qiven";
    f.lane = "implementer";
    f.kind = "claim";
    f.writer = "implementer/s39";
    f.basis = "claimed";
    f.at = "2026-10-17T09:30:00.000Z";
    f.content = "hmac-sha256:df0388fc101c4c0bc038ea2dc231dd5fadc5376af843e48755de9f638096be5e";
    f.id = a_id;
    return f;
}

constexpr std::optional<Code> code_of(const FieldsView& f) { return header::try_check(header::make(f)); }
constexpr std::optional<Code> code_of_line(std::string_view line) {
    return header::try_check(header::parse_line(line));
}
// A's fields after edit refused with code.
template <typename Edit>
constexpr bool refused_as(Code code, const Edit& edit) {
    FieldsView f = a_fields();
    edit(f);
    return code_of(f) == code;
}
constexpr bool line_refused(std::string_view line, Code code) { return code_of_line(line) == code; }

// line with its first from replaced by to.
constexpr std::string edited(std::string_view line, std::string_view from, std::string_view to) {
    std::string out(line);
    const std::size_t at = out.find(from);
    return at == std::string::npos ? std::string() : out.replace(at, from.size(), to);
}
constexpr std::string_view note = "\"lane\":\"implementer\",\"note\":\"x\",";

// A unchanged: no fault; its id computed equals the line's; written back.
static_assert(!code_of_line(a_line));
static_assert(!code_of(a_fields()));
static_assert([] {
    FieldsView f = a_fields();
    f.id.reset();
    return header::make(f).text(Slot::id) == a_id;
}());
static_assert([] {
    std::string out;
    header::parse_line(a_line).line(out);
    return out == a_line;
}());

// The refusals of A's header, one change each, its id kept.
static_assert(refused_as(Code::owner, [](FieldsView& f) { f.writer = "owner/s1"; f.lane = "owner"; }));
static_assert(refused_as(Code::reserved, [](FieldsView& f) { f.writer = "loop/s1"; f.lane = "loop"; }));
static_assert(refused_as(Code::reserved, [](FieldsView& f) { f.writer = "import/s1"; f.lane = "import"; }));
static_assert(refused_as(Code::empty, [](FieldsView& f) { f.parent = ""; f.seq = 2; }));
static_assert(refused_as(Code::seq, [](FieldsView& f) { f.seq = 2; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.at = "2026-10-17T09:30:00Z"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.at = "2026-02-30T09:30:00.000Z"; }));
static_assert(refused_as(Code::lane, [](FieldsView& f) { f.lane = "reviewer"; }));
static_assert(refused_as(Code::reflect_kind, [](FieldsView& f) {
    f.writer = "reflect/s39";
    f.lane = "reflect";
    f.kind = "direction";
}));
static_assert(refused_as(Code::target, [](FieldsView& f) { f.kind = "check"; }));
static_assert(refused_as(Code::id_mismatch, [](FieldsView& f) { f.basis = "observed"; }));
static_assert(line_refused(edited(a_line, "\"lane\":\"implementer\",", note), Code::unknown_key));
static_assert(line_refused(edited(a_line, ",\"writer\":\"implementer/s39\"", ""), Code::missing_key));
static_assert(line_refused(edited(a_line, ",\"id\":\"sha256:902380391ae283f1", ",\"ie\":\"sha256:902380391ae283f1"),
                          Code::unknown_key));
static_assert(line_refused(edited(a_line, std::string("\"id\":\"") + std::string(a_id) + "\",", ""),
                           Code::missing_key));

// The order: two faults each, the earlier code deciding.
constexpr std::string_view writer_member = ",\"writer\":\"implementer/s39\"";
static_assert(line_refused(edited(edited(a_line, "\"lane\":\"implementer\",", note), writer_member, ""),
                           Code::unknown_key));
static_assert(line_refused(edited(edited(a_line, "\"lane\":\"implementer\"", "\"lane\":\"\""), writer_member, ""),
                           Code::missing_key));
static_assert(refused_as(Code::empty, [](FieldsView& f) { f.lane = ""; f.writer = "owner/s1"; }));
static_assert(refused_as(Code::owner, [](FieldsView& f) { f.writer = "owner/s1"; f.at = "x"; }));
static_assert(refused_as(Code::owner, [](FieldsView& f) { f.lane = "owner"; f.at = "x"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.writer = "loop/s1"; f.lane = "loop"; f.at = "x"; }));
static_assert(refused_as(Code::reserved, [](FieldsView& f) { f.writer = "loop/s1"; }));
static_assert(refused_as(Code::lane, [](FieldsView& f) { f.writer = "reflect/s39"; f.kind = "direction"; }));
static_assert(refused_as(Code::reflect_kind, [](FieldsView& f) {
    f.writer = "reflect/s39";
    f.lane = "reflect";
    f.kind = "direction";
    f.seq = 2;
}));
static_assert(refused_as(Code::seq, [](FieldsView& f) { f.seq = 2; f.kind = "check"; }));
static_assert(refused_as(Code::target, [](FieldsView& f) { f.kind = "forget"; }));
static_assert(refused_as(Code::content, [](FieldsView& f) { f.kind = "forget"; f.target = b_id; }));
static_assert(refused_as(Code::content, [](FieldsView& f) { f.content.reset(); }));

// The syntaxes' edges, and the types a line may hold.
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.tag = "qiven-entry/2"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.kind = "note"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.basis = "said"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.scope = "project:"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.scope = "project:Qiven"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.scope = "project:-qiven"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.scope = "people"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.scope = "project:\xe8\xae\xb0"; }));
// A slug of 49 characters, then of 48.
static_assert(refused_as(Code::syntax, [](FieldsView& f) {
    f.scope = "project:a234567890123456789012345678901234567890123456789";
}));
static_assert(refused_as(Code::id_mismatch, [](FieldsView& f) {
    f.scope = "project:a23456789012345678901234567890123456789012345678";
}));
static_assert(refused_as(Code::id_mismatch, [](FieldsView& f) { f.scope = "life"; }));
static_assert(refused_as(Code::id_mismatch, [](FieldsView& f) { f.scope = "person"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.writer = "implementer/s0"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.writer = "implementer/s1234567890"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.writer = "implementer"; }));
static_assert(refused_as(Code::id_mismatch, [](FieldsView& f) { f.writer = "implementer/s123456789"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.lane = "a234567890123456789012345678901bc"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.seq = 0; f.parent = a_id; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.seq = 9007199254740992; f.parent = a_id; }));
static_assert(refused_as(Code::id_mismatch, [](FieldsView& f) { f.seq = 9007199254740991; f.parent = a_id; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) {
    f.parent = "sha256:902380391AE283f1a88e038cf525aea22d5e4ed0ca3f852c8acdf43457b3b119";
    f.seq = 2;
}));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.target = "sha256:90238039"; }));
static_assert(refused_as(Code::syntax, [](FieldsView& f) {
    f.content = "hmac-sha256:DF0388fc101c4c0bc038ea2dc231dd5fadc5376af843e48755de9f638096be5e";
}));
static_assert(refused_as(Code::syntax, [](FieldsView& f) { f.id = "sha256:902380391ae283f1"; }));
static_assert(line_refused(edited(a_line, "\"seq\":1", "\"seq\":\"1\""), Code::syntax));
static_assert(line_refused(edited(a_line, "\"kind\":\"claim\"", "\"kind\":true"), Code::syntax));
static_assert(line_refused(edited(a_line, "\"lane\":\"implementer\"", "\"lane\":[\"implementer\"]"), Code::syntax));
static_assert(line_refused(edited(a_line, "\"basis\":\"claimed\"", "\"basis\":7"), Code::syntax));

// stamp's raise: the clock, raised to the parent's, then past the target's.
constexpr utc::Millis ms(std::int64_t n) { return utc::Millis(std::chrono::milliseconds(n)); }
static_assert(header::raise(ms(100), std::nullopt, std::nullopt) == ms(100));
static_assert(header::raise(ms(100), ms(150), std::nullopt) == ms(150));
static_assert(header::raise(ms(100), ms(50), std::nullopt) == ms(100));
static_assert(header::raise(ms(100), std::nullopt, ms(100)) == ms(101));
static_assert(header::raise(ms(100), std::nullopt, ms(99)) == ms(100));
static_assert(header::raise(ms(100), ms(150), ms(150)) == ms(151));
static_assert(header::raise(ms(100), ms(150), ms(120)) == ms(150));

struct StillClock {
    utc::Millis at;
    [[nodiscard]] constexpr utc::Millis now() const noexcept { return at; }
};
static_assert(header::Clock<StillClock> && header::Clock<Fake>);
static_assert([] {
    StillClock c{*utc::parse_at("2026-10-17T09:30:00.000Z")};
    const auto t = header::stamp(c, "2026-10-17T09:31:15.250Z", "2026-10-17T09:31:15.250Z");
    return std::string_view(t.data(), t.size()) == "2026-10-17T09:31:15.251Z";
}());

// A refusal thrown, as a code's name.
template <typename Op>
std::string thrown(const Op& op) {
    try {
        op();
    } catch (const header::HeaderError& e) {
        return std::string(header::name(e.code));
    } catch (const qiven3::ParseError&) {
        return "parse";
    }
    return "none";
}

}  // namespace

QIVEN3_CASE(header, fixtures) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::size_t checked = 0;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        const header::Header<> h = header::parse_line(e.header);
        const std::optional<Code> fault = header::try_check(h);
        if (fault) {
            std::cout << "header.fixtures: " << e.name << " gave " << header::name(*fault) << "\n";
        }
        CHECK(!fault);
        std::string line;
        h.line(line);
        CHECK_EQ(line, std::string(e.header));
        CHECK_EQ(std::string(header::id_of(h).view()), std::string(*h.text(Slot::id)));
        CHECK_EQ(h.present(Slot::content), e.content.has_value());
        ++checked;
    }
    const std::optional<fixtures::EntryView> a = fixtures::find_entry(v, "A");
    REQUIRE(a.has_value());
    CHECK_EQ(std::string(a->header), std::string(a_line));
    std::cout << "header.fixtures: " << checked << " headers passed and written back\n";
    CHECK_EQ(checked, 12U);
}

QIVEN3_CASE(header, refusals) {
    const auto check_fields = [](const FieldsView& f) { return [f] { header::check(header::make(f)); }; };
    const auto check_line = [](std::string line) { return [line] { header::check(header::parse_line(line)); }; };
    FieldsView f = a_fields();
    CHECK_EQ(thrown(check_fields(f)), "none");
    f.writer = "owner/s1";
    f.lane = "owner";
    CHECK_EQ(thrown(check_fields(f)), "owner");
    f = a_fields();
    f.parent = "";
    f.seq = 2;
    CHECK_EQ(thrown(check_fields(f)), "empty");
    f = a_fields();
    f.basis = "observed";
    CHECK_EQ(thrown(check_fields(f)), "id_mismatch");
    CHECK_EQ(thrown(check_line(edited(a_line, "\"lane\":\"implementer\",", note))), "unknown_key");
    CHECK_EQ(thrown(check_line(edited(a_line, ",\"writer\":\"implementer/s39\"", ""))), "missing_key");
    // No header line: the parser's refusal, or a root that is no object.
    CHECK_EQ(thrown(check_line(edited(a_line, "\"seq\":1", "\"seq\": 1"))), "parse");
    CHECK_EQ(thrown(check_line("{\"a\":1,\"a\":2}")), "parse");
    for (const std::string_view root : {"[]", "\"x\"", "1", "true"}) {
        CHECK_EQ(thrown(check_line(std::string(root))), "syntax");
    }
    // line writes what it can type: a slot of another type is syntax.
    const header::Header<> wrong = header::parse_line(edited(a_line, "\"kind\":\"claim\"", "\"kind\":true"));
    CHECK_EQ(thrown([&wrong] {
                 std::string out = "kept";
                 wrong.line(out);
             }),
             "syntax");
    const header::Header<> seven = header::parse_line(edited(a_line, "\"basis\":\"claimed\"", "\"basis\":7"));
    CHECK_EQ(seven.form(Slot::basis), header::Form::integer);
    CHECK_EQ(std::string(*seven.text(Slot::basis)), "7");
    std::string out = "kept";
    CHECK_EQ(thrown([&seven, &out] { seven.line(out); }), "syntax");
    CHECK_EQ(out, "kept");
    // The codes' names, in their order.
    CHECK_EQ(header::code_names.size(), 12U);
    CHECK_EQ(std::string(header::name(Code::reflect_kind)), "reflect_kind");
}

QIVEN3_CASE(header, stamp) {
    Fake fake(qiven3::platform::fake::Config{});
    const auto set = [&fake](std::string_view at, std::int64_t micros) {
        fake.set_clock(std::chrono::time_point_cast<std::chrono::microseconds>(*utc::parse_at(at)) +
                       std::chrono::microseconds(micros));
    };
    const auto text = [](const std::array<char, utc::at_size>& t) { return std::string(t.data(), t.size()); };
    // The clock truncated to the millisecond.
    set("2026-10-17T09:30:00.000Z", 999);
    CHECK_EQ(text(header::stamp(fake, std::nullopt, std::nullopt)), "2026-10-17T09:30:00.000Z");
    // Raised to the parent's at, then to 1 ms past the target's.
    CHECK_EQ(text(header::stamp(fake, "2026-10-17T09:31:15.250Z", std::nullopt)), "2026-10-17T09:31:15.250Z");
    CHECK_EQ(text(header::stamp(fake, std::nullopt, "2026-10-17T09:30:00.000Z")), "2026-10-17T09:30:00.001Z");
    CHECK_EQ(text(header::stamp(fake, "2026-10-17T09:29:00.000Z", "2026-10-17T09:29:59.999Z")),
             "2026-10-17T09:30:00.000Z");
    // An at that is none, or past year 9999, is syntax.
    CHECK_EQ(thrown([&fake] { static_cast<void>(header::stamp(fake, "2026-10-17T09:30:00Z", std::nullopt)); }),
             "syntax");
    CHECK_EQ(thrown([&fake] { static_cast<void>(header::stamp(fake, std::nullopt, "9999-12-31T23:59:59.999Z")); }),
             "syntax");
}
