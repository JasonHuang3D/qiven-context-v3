// served's cases. Evaluated by the compiler: a claim's text escaped onto
// one line (\ LF CR TAB, C0, 0x7f, U+0085, U+2028, U+2029, U+FEFF and the
// trailing spaces), its state line with when, expires, forked and
// duplicate; an entity's as of; a relation named by its from; a map's
// object as its name, unchecked with no mark and fresh with its map
// check's at; a reflection writer's lesson served as a claim until lifted;
// an object's and a list's forms and a deeper value's canonical JSON; a
// checkpoint's detail not shown; an item's state last on line 3 and not
// among its fields; the longest label served whole on line 2; a path's
// U+FEFF escaped alike in a map's object on line 1 and in an owner's
// words' label on line 2, 200 of them served whole; a
// stub, its cost the block's units, not its bytes; cost in UTF-16 units;
// map_mark's syntax; BUDGETS. At run time,
// over tests/fixtures/: M1 stale and M2 fresh with their map checks' at,
// 387 and 402 units, M1 superseded by M2; L1 alone and L2 after L1, its
// when and pins in a lesson in Chinese and English, L1 superseded and not
// served; MX's block of 42,658 units over a page and its stub of 201; and
// the refusals: a kind not served, a state not served, a mark on another
// kind, a content without its band, its name or a repo object's path, a
// when without its quality, a content not an object, an item without its
// state.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "entry/fold.hpp"
#include "entry/header.hpp"
#include "entry/labels.hpp"
#include "entry/refs.hpp"
#include "entry/served.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace served = qiven3::entry::served;
namespace fold = qiven3::entry::fold;
namespace header = qiven3::entry::header;
namespace labels = qiven3::entry::labels;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using served::Mark;
using served::MapMark;

#define ID_HEX "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

constexpr served::State SERVED{fold::State{fold::Status::served, 0, 0, fold::none}, 0};

// A header line of these, its id ID_HEX's.
constexpr std::string head(std::string_view kind, std::string_view basis, std::string_view writer) {
    return std::string(R"({"basis":")") + std::string(basis) + R"(","id":"sha256:)" ID_HEX R"(","kind":")" +
           std::string(kind) + R"(","writer":")" + std::string(writer) + R"("})";
}

// The block, or with stub its stub, of a header and a content, its label
// the version's with the checks given.
constexpr std::string render(const std::string& line, std::string_view content, const served::State& state = SERVED,
                             const std::optional<MapMark>& mark = std::nullopt,
                             std::span<const labels::CheckView> checks = {}, bool stub = false) {
    const header::Header<> h = header::parse_line(line);
    canon::Basic<> v;
    canon::parse(content, v);
    const labels::Label label = labels::label(*labels::version_of(h, v), checks);
    std::string out;
    if (stub) {
        served::stub(h, v, state, label, mark, out);
    } else {
        served::block(h, v, state, label, mark, out);
    }
    return out;
}

// A claim's text escaped onto one line, and its state line.
constexpr served::State FORKED{fold::State{fold::Status::served, 3, 2, fold::none}, 1};
static_assert(render(head("claim", "claimed", "implementer/s39"),
                     "{\"band\":\"core\",\"claim\":\"a\\\\b\\nc\\r\\td\\u0001\x7f\xC2\x85\xE2\x80\xA8\xE2\x80\xA9"
                     "\xEF\xBB\xBF e  \",\"expires\":\"2026-10-18T00:00:00.000Z\",\"subject\":\"entity:x\","
                     "\"when\":{\"quality\":\"approximate\",\"time\":\"2026-10-17T00:00:00.000Z\"}}",
                     FORKED) ==
              "claim entity:x [core] 0123456789abcdef\n"
              "by: claim by implementer/s39\n"
              "state: none; when 2026-10-17T00:00:00.000Z (approximate); expires 2026-10-18T00:00:00.000Z; "
              "forked +2; duplicate +1\n"
              "claim: a\\\\b\\nc\\r\\td\\u0001\\u007f\\u0085\\u2028\\u2029\\ufeff e\\u0020\\u0020\n"
              "\n");

// An entity's as of; its fields in kinds' order, not the content's.
static_assert(render(head("entity", "observed", "tester/s71"),
                     R"({"as_of":"2026-10-17","band":"high","description":"The reference interpreter.",)"
                     R"("entity_type":"other","name":"CPython","subject":"entity:cpython"})") ==
              "entity entity:cpython [high] 0123456789abcdef\n"
              "by: observed by tester/s71\n"
              "state: none; as of 2026-10-17\n"
              "name: CPython\n"
              "entity_type: other\n"
              "description: The reference interpreter.\n"
              "\n");

// A relation with no subject is named by its from.
static_assert(render(head("relation", "claimed", "implementer/s39"),
                     R"({"band":"low","from":"module:a","relation":"calls","to":"module:b"})") ==
              "relation module:a [low] 0123456789abcdef\n"
              "by: claim by implementer/s39\n"
              "state: none\n"
              "from: module:a\n"
              "relation: calls\n"
              "to: module:b\n"
              "\n");

// A map: its object as its name, not shown; unchecked with no mark, the
// mark and its map check's at when given; relations, a list of objects.
constexpr std::string_view MAP_ENTRY =
    R"({"band":"normal","does":"Holds the plan.","object":{"digest":"hmac-sha256:)" ID_HEX
    R"(","id":"sha256:)" ID_HEX R"(","type":"entry"},"object_type":"item",)"
    R"("relations":[{"relation":"waits-on","to":"item:b"},{"relation":"under","to":"item:c"}]})";
static_assert(render(head("map", "derived", "reviewer/s70"), MAP_ENTRY) ==
              "map entry:sha256:" ID_HEX " [normal] 0123456789abcdef\n"
              "by: derived by reviewer/s70, non-authoritative\n"
              "state: unchecked\n"
              "object_type: item\n"
              "does: Holds the plan.\n"
              "relations:\n"
              "  - relation=waits-on; to=item:b\n"
              "  - relation=under; to=item:c\n"
              "\n");
constexpr std::string_view MAP_REPO =
    R"({"band":"core","contract":"Parses.","does":"Reads.","object":{"digest":"hmac-sha256:)" ID_HEX
    R"(","name":"parse","path":"src/a.py","repository":"r","type":"repo"},"object_type":"file"})";
static_assert(render(head("map", "derived", "reviewer/s70"), MAP_REPO, SERVED,
                     served::map_mark(Mark::fresh, "2026-10-17T12:06:00.000Z")) ==
              "map repo:r/src/a.py#parse [core] 0123456789abcdef\n"
              "by: derived by reviewer/s70, non-authoritative\n"
              "state: fresh; map check 2026-10-17T12:06:00.000Z\n"
              "object_type: file\n"
              "does: Reads.\n"
              "contract: Parses.\n"
              "\n");

// A reflection writer's lesson is served as a claim until a verified or
// accepted label lifts it; an object's lists joined by ", ", a list of
// objects' pairs by "; ".
constexpr std::string_view LESSON =
    R"({"band":"normal","lesson":"Pin it.","pins":[{"name":"python","type":"tool","version":"3.15.0"}],)"
    R"("subject":"entity:cpython","trigger":{"activities":["pin","run"],"paths":["a.py"]}})";
constexpr std::string_view LESSON_TAIL = "lesson: Pin it.\n"
                                         "trigger:\n"
                                         "  activities: pin, run\n"
                                         "  paths: a.py\n"
                                         "pins:\n"
                                         "  - name=python; type=tool; version=3.15.0\n"
                                         "\n";
constexpr labels::CheckView rerun() {
    labels::CheckView c;
    c.writer = "reviewer/s70";
    c.method = labels::Method::rerun;
    c.result = labels::Result::verified;
    return c;
}
constexpr std::array<labels::CheckView, 1> RERUN = {rerun()};
static_assert(render(head("lesson", "claimed", "reflect/s5"), LESSON) ==
              std::string("claim entity:cpython [normal] 0123456789abcdef\n"
                          "by: claim by reflect/s5\n"
                          "state: none\n") +
                  std::string(LESSON_TAIL));
static_assert(render(head("lesson", "claimed", "reflect/s5"), LESSON, SERVED, std::nullopt, RERUN) ==
              std::string("lesson entity:cpython [normal] 0123456789abcdef\n"
                          "by: claim by reflect/s5; verified by reviewer/s70 (rerun)\n"
                          "state: none\n") +
                  std::string(LESSON_TAIL));

// A value deeper than the forms is its canonical JSON, a container closed
// before its sibling; a list's items each by their own form.
static_assert(render(head("decision", "claimed", "implementer/s39"),
                     R"({"band":"high","decides":{"a":{"b":[1,true,"x"],"d":false},"c":[1,2]},)"
                     R"("reasons":[{"k":["a","b"],"n":-1},"plain",[1,{"z":false}]],"subject":"item:x"})") ==
              "decision item:x [high] 0123456789abcdef\n"
              "by: claim by implementer/s39\n"
              "state: none\n"
              "decides:\n"
              "  a: {\"b\":[1,true,\"x\"],\"d\":false}\n"
              "  c: 1, 2\n"
              "reasons:\n"
              "  - k=a, b; n=-1\n"
              "  - plain\n"
              "  - [1,{\"z\":false}]\n"
              "\n");

// A checkpoint's detail is not shown; its other fields are, for_lane among
// them. An item's state ends line 3 and is not repeated among its fields;
// its common item field is never shown.
static_assert(render(head("checkpoint", "claimed", "implementer/s39"),
                     R"({"band":"core","detail":"The rest of the record.","for_lane":"reviewer",)"
                     R"("next_action":"Run tier 1.","progress":"Built.","subject":"activity:x"})") ==
              "checkpoint activity:x [core] 0123456789abcdef\n"
              "by: claim by implementer/s39\n"
              "state: none\n"
              "progress: Built.\n"
              "next_action: Run tier 1.\n"
              "for_lane: reviewer\n"
              "\n");
static_assert(render(head("item", "claimed", "implementer/s39"),
                     R"({"band":"core","item":"x","item_id":"x","state":"active","subject":"item:x",)"
                     R"("title":"Do x.","when":{"quality":"exact","time":"2026-10-17T00:00:00.000Z"}})",
                     FORKED) ==
              "item item:x [core] 0123456789abcdef\n"
              "by: claim by implementer/s39\n"
              "state: none; when 2026-10-17T00:00:00.000Z (exact); forked +2; duplicate +1; item active\n"
              "item_id: x\n"
              "title: Do x.\n"
              "\n");

// The longest label (labels' capacity) served whole on line 2: the owner's words
// as quoted in a path of 200 four-byte characters, refuted by transcript.
constexpr std::string_view W1 = "rrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrr/s123456789";
constexpr std::string_view W2 = "qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq/s987654321";
constexpr std::string_view REV = "0123456789abcdef0123456789abcdef01234567";
constexpr std::string wide_path() {
    std::string p;
    for (std::size_t i = 0; i < 200; ++i) {
        p += "\xF0\x9F\x98\x80";
    }
    return p;
}
constexpr labels::CheckView refuting() {
    labels::CheckView c;
    c.writer = W2;
    c.method = fold::Method::transcript;
    c.result = fold::Result::refuted;
    c.transcript = fold::Transcript::reverified;
    return c;
}
constexpr std::array<labels::CheckView, 1> REFUTING = {refuting()};
constexpr std::string long_label_line() {
    return "by: the owner's words as quoted in " + wide_path() + " (" + std::string(REV) + "), recorded by " +
           std::string(W1) + "; refuted by " + std::string(W2) + " (transcript)\n";
}
constexpr std::string quoted_block() {
    return render(head("direction", "owner-words", W1),
                  R"({"band":"core","source":[{"digest":"d1","path":")" + wide_path() +
                      R"(","repository":"r","revision":")" + std::string(REV) +
                      R"(","type":"repo"}],"subject":"activity:x","words":"Keep it."})",
                  SERVED, std::nullopt, REFUTING);
}
static_assert(long_label_line().size() == 4 + labels::capacity + 1);
static_assert(quoted_block() == "direction activity:x [core] 0123456789abcdef\n" + long_label_line() +
                                    "state: none\n"
                                    "words: Keep it.\n"
                                    "\n");

// A path's U+FEFF reads the same on line 1 and in a label, escaped as a
// field's text is: in a map's object, and in the owner's words' document
// of record (its label's path); at refs' limit, 200 of them, the label
// refuted by transcript, served whole, six bytes for each three.
constexpr std::string_view BOM = "\xEF\xBB\xBF";
constexpr std::string_view BOM_ESCAPED = "\\" "ufeff";
constexpr std::string_view BOM_PATH = "docs/\xEF\xBB\xBF" "a.md";
constexpr std::string bom_path_escaped() { return "docs/" + std::string(BOM_ESCAPED) + "a.md"; }
constexpr std::string repeated(std::string_view unit, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += unit;
    }
    return out;
}
static_assert(qiven3::entry::refs::check_path(BOM_PATH) && qiven3::entry::refs::check_path(repeated(BOM, 200)) &&
              qiven3::entry::refs::path_limit == 200);
constexpr std::string direction_at(std::string_view path, std::span<const labels::CheckView> checks = {}) {
    return render(head("direction", "owner-words", W1),
                  R"({"band":"core","source":[{"digest":"d1","path":")" + std::string(path) +
                      R"(","repository":"r","revision":")" + std::string(REV) +
                      R"(","type":"repo"}],"subject":"activity:x","words":"Keep it."})",
                  SERVED, std::nullopt, checks);
}
constexpr std::string map_at(std::string_view path) {
    return render(head("map", "derived", "reviewer/s70"),
                  R"({"band":"core","contract":"Parses.","does":"Reads.","object":{"digest":"hmac-sha256:)" ID_HEX
                  R"(","path":")" + std::string(path) + R"(","repository":"r","type":"repo"},"object_type":"file"})");
}
static_assert(direction_at(BOM_PATH) == "direction activity:x [core] 0123456789abcdef\n"
                                        "by: the owner's words as quoted in " +
                                            bom_path_escaped() + " ("+ std::string(REV) + "), recorded by " +
                                            std::string(W1) +
                                            "\n"
                                            "state: none\n"
                                            "words: Keep it.\n"
                                            "\n");
static_assert(map_at(BOM_PATH).starts_with("map repo:r/" + bom_path_escaped() +" [core] 0123456789abcdef\n"
                                                                              "by: derived by reviewer/s70, "
                                                                              "non-authoritative\n"));
constexpr std::string bom_label_line() {
    return "by: the owner's words as quoted in " + repeated(BOM_ESCAPED, 200) + " (" + std::string(REV) +
           "), recorded by " + std::string(W1) + "; refuted by " + std::string(W2) + " (transcript)\n";
}
static_assert(direction_at(repeated(BOM, 200), REFUTING) == "direction activity:x [core] 0123456789abcdef\n" +
                                                                bom_label_line() +
                                                                "state: none\n"
                                                                "words: Keep it.\n"
                                                                "\n");
static_assert(bom_label_line().size() == 4 + labels::capacity - 200 * 4 + 200 * 6 + 1);

// A stub: lines 1 to 3, the block's cost and full id, the empty line.
static_assert(render(head("claim", "claimed", "implementer/s39"), R"({"band":"core","claim":"x","subject":"entity:x"})",
                     SERVED, std::nullopt, {}, true) ==
              "claim entity:x [core] 0123456789abcdef\n"
              "by: claim by implementer/s39\n"
              "state: none\n"
              "stub: 90 units; read by id sha256:" ID_HEX "\n"
              "\n");
static_assert(served::cost(render(head("claim", "claimed", "implementer/s39"),
                                  R"({"band":"core","claim":"x","subject":"entity:x"})")) == 90);
// A stub's cost is its block's UTF-16 units, not its bytes: U+8BB0 one,
// U+20000 two.
constexpr std::string_view WIDE =
    "{\"band\":\"core\",\"claim\":\"\xE8\xAE\xB0\xF0\xA0\x80\x80\",\"subject\":\"entity:x\"}";
static_assert(served::cost(render(head("claim", "claimed", "implementer/s39"), WIDE)) == 92 &&
              render(head("claim", "claimed", "implementer/s39"), WIDE).size() == 96);
static_assert(render(head("claim", "claimed", "implementer/s39"), WIDE, SERVED, std::nullopt, {}, true)
                  .ends_with("state: none\nstub: 92 units; read by id sha256:" ID_HEX "\n\n"));

// cost: UTF-16 units; a BMP character one, one past it two.
static_assert(served::cost("") == 0 && served::cost("ab\n") == 3);
static_assert(served::cost("\xE8\xAE\xB0\xE5\xBF\x86") == 2);
static_assert(served::cost("\xF0\xA0\x80\x80") == 2 && served::cost("a\xF0\xA0\x80\x80\xC3\xA9") == 4);

// map_mark: fresh or stale, with an at in its syntax.
static_assert(served::map_mark(Mark::stale, "2026-10-17T12:00:00.000Z")->when() == "2026-10-17T12:00:00.000Z");
static_assert(!served::map_mark(Mark::unchecked, "2026-10-17T12:00:00.000Z").has_value());
static_assert(!served::map_mark(Mark::none, "2026-10-17T12:00:00.000Z").has_value());
static_assert(!served::map_mark(Mark::fresh, "2026-10-17T12:00:00Z").has_value());
static_assert(!served::map_mark(Mark::fresh, "2026-13-17T12:00:00.000Z").has_value());

// The budgets, in tokens and UTF-16 units at 3 a token.
static_assert(served::BUDGETS[1].units == 8250 && served::BUDGETS[2].tokens == 2750);
static_assert(served::BUDGETS[4].units == 1500 && served::BUDGETS[5].units == 30000);
static_assert(served::BUDGETS[0].units + served::BUDGETS[1].units + served::BUDGETS[2].units +
                  served::BUDGETS[3].units + served::BUDGETS[4].units ==
              served::BUDGETS[5].units);
static_assert(served::name(served::Code::content) == "content" && served::name(Mark::stale) == "stale");

// A fixture entry: its header and its content parsed.
struct Held {
    header::Header<> h;
    canon::Basic<> v;
};
Held held(const canon::Basic<>& all, std::string_view name) {
    const std::optional<fixtures::EntryView> x = fixtures::find_entry(all, name);
    if (!x || !x->content) {
        throw std::runtime_error("no fixture entry");
    }
    Held e{header::parse_line(x->header), canon::Basic<>()};
    canon::parse(*x->content, e.v);
    return e;
}

// A record's fold at a time after every fixture.
served::State state(std::span<const Held* const> record) {
    std::vector<fold::ItemView> items;
    for (const Held* x : record) {
        items.push_back(fold::item(x->h, x->v, true));
    }
    return served::State{fold::fold(std::span<const fold::ItemView>(items), "2026-10-18T00:00:00.000Z"), 0};
}

labels::Label own(const Held& x) { return labels::label(*labels::version_of(x.h, x.v), {}); }

std::string block(const Held& x, const served::State& s, const std::optional<MapMark>& mark = std::nullopt) {
    std::string out;
    served::block(x.h, x.v, s, own(x), mark, out);
    return out;
}

constexpr std::string_view M_TAIL = "object_type: file\n"
                                    "does: %\n"
                                    "contract: entry_id(header) returns sha256: and 64 lowercase hex digits.\n"
                                    "constraints:\n"
                                    "  - Any change to the framing changes every id; the vectors pin it.\n"
                                    "\n";
std::string m_block(std::string_view first, std::string_view state_line, std::string_view does) {
    std::string tail(M_TAIL);
    tail.replace(tail.find('%'), 1, does);
    return std::string(first) + "\nby: derived by reviewer/s70, non-authoritative\n" + std::string(state_line) +
           "\n" + tail;
}

}  // namespace

QIVEN3_CASE(served, map_blocks) {
    canon::Basic<> all;
    fixtures::load_entries(all);
    const Held m1 = held(all, "M1");
    const Held m2 = held(all, "M2");
    const std::array<const Held*, 1> alone = {&m1};
    const std::array<const Held*, 2> both = {&m1, &m2};
    const served::State s1 = state(alone);
    const served::State s2 = state(both);
    CHECK(s1.fold.served() && s1.fold.current == 0);
    // M1 superseded by M2: the record's current is M2.
    CHECK(s2.fold.served() && s2.fold.current == 1 && s2.fold.forks == 0);
    const std::string b1 = block(m1, s1, served::map_mark(Mark::stale, "2026-10-17T12:00:00.000Z"));
    const std::string b2 = block(m2, s2, served::map_mark(Mark::fresh, "2026-10-17T12:06:00.000Z"));
    CHECK_EQ(b1, m_block("map repo:qiven-context-v3/qiven3/entry.py [high] cdc1ccfaaa779c70",
                         "state: stale; map check 2026-10-17T12:00:00.000Z",
                         "Computes an entry's id from its framed header."));
    CHECK_EQ(b2, m_block("map repo:qiven-context-v3/qiven3/entry.py [high] 4d910a4eee1c81bc",
                         "state: fresh; map check 2026-10-17T12:06:00.000Z",
                         "Checks a header, then computes its id from the framed header."));
    CHECK_EQ(served::cost(b1), 387U);
    CHECK_EQ(served::cost(b2), 402U);
    CHECK_EQ(served::block_cost(m1.h, m1.v, s1, own(m1), served::map_mark(Mark::stale, "2026-10-17T12:00:00.000Z")),
             387U);
    // No mark from the marks file: unchecked.
    const std::string unchecked = block(m1, s1);
    CHECK_EQ(unchecked.substr(unchecked.find("state:"), 17), "state: unchecked\n");
    std::cout << "served.map_blocks: M1 " << served::cost(b1) << " units, M2 " << served::cost(b2) << "\n";
}

QIVEN3_CASE(served, lesson_when) {
    canon::Basic<> all;
    fixtures::load_entries(all);
    const Held l1 = held(all, "L1");
    const Held l2 = held(all, "L2");
    const std::array<const Held*, 1> alone = {&l1};
    const std::array<const Held*, 2> both = {&l1, &l2};
    const served::State s1 = state(alone);
    const served::State s2 = state(both);
    CHECK_EQ(block(l1, s1),
             "lesson entity:cpython [normal] dbc3708cf045a0a4\n"
             "by: claim by learner/s42\n"
             "state: none; when 2026-10-17T05:00:00.000Z (exact)\n"
             "lesson: \xE5\x9C\xA8 3.14.7 \xE4\xB8\x8A\xEF\xBC\x8C"
             "str.isspace \xE6\x8C\x89 Unicode 16.0.0 \xE5\x88\xA4\xE6\x96\xAD\xEF\xBC\x9B"
             "pin the interpreter in every test.\n"
             "trigger:\n"
             "  activities: pin-interpreter\n"
             "pins:\n"
             "  - name=python; type=tool; version=3.14.7\n"
             "  - name=PYTHONUTF8; type=env\n"
             "\n");
    // After L2, L1 is superseded and not served: the record serves L2.
    REQUIRE(s2.fold.served());
    CHECK_EQ(s2.fold.current, 1U);
    const std::string b2 = block(l2, s2);
    CHECK_EQ(b2, "lesson entity:cpython [normal] 566240684567f8e0\n"
                 "by: claim by learner/s43\n"
                 "state: none; when 2026-10-17T09:00:00.000Z (session)\n"
                 "lesson: On 3.15.0, str.isspace follows Unicode 17.0.0 and holds the same 29 code points; "
                 "\xE6\xAF\x8F\xE4\xB8\xAA\xE6\xB5\x8B\xE8\xAF\x95\xE4\xBB\x8D\xE5\x9B\xBA\xE5\xAE\x9A"
                 "\xE8\xA7\xA3\xE9\x87\x8A\xE5\x99\xA8\xE3\x80\x82\n"
                 "trigger:\n"
                 "  activities: pin-interpreter\n"
                 "pins:\n"
                 "  - name=python; type=tool; version=3.15.0\n"
                 "  - name=PYTHONUTF8; type=env\n"
                 "\n");
    // The Chinese characters count one unit each.
    CHECK_EQ(served::cost(b2), b2.size() - 2 * 11);
    std::cout << "served.lesson_when: L2 " << served::cost(b2) << " units, " << b2.size() << " bytes\n";
}

QIVEN3_CASE(served, mx_stub) {
    canon::Basic<> all;
    fixtures::load_entries(all);
    const Held mx = held(all, "MX");
    const std::array<const Held*, 1> alone = {&mx};
    const served::State s = state(alone);
    const std::string whole = block(mx, s);
    CHECK_EQ(served::cost(whole), 42658U);
    CHECK(served::cost(whole) > served::block_max && served::cost(whole) > served::BUDGETS[5].units);
    // Each U+0001 of invariants escaped to six units, on one line.
    CHECK(whole.find("\\u0001\\u0001") != std::string::npos);
    std::string out;
    served::stub(mx.h, mx.v, s, own(mx), std::nullopt, out);
    CHECK_EQ(out, "module module:x [low] 8e82a1bef05464a4\n"
                  "by: derived by reviewer/s70, non-authoritative\n"
                  "state: none\n"
                  "stub: 42658 units; read by id "
                  "sha256:8e82a1bef05464a4985e42ac029955ba4a00b43d68b31311aa8f050c7dd1ec08\n"
                  "\n");
    CHECK_EQ(served::cost(out), 201U);
    std::cout << "served.mx_stub: block " << served::cost(whole) << " units, " << whole.size() << " bytes; stub "
              << served::cost(out) << "\n";
}

QIVEN3_CASE(served, refusals) {
    const auto code = [](const std::string& line, std::string_view content, const served::State& s,
                         const std::optional<MapMark>& mark) -> std::optional<served::Code> {
        try {
            static_cast<void>(render(line, content, s, mark));
        } catch (const served::ServedError& e) {
            return e.code;
        }
        return std::nullopt;
    };
    constexpr std::string_view claim = R"({"band":"core","claim":"x","subject":"entity:x"})";
    const std::string line = head("claim", "claimed", "implementer/s39");
    CHECK(!code(line, claim, SERVED, std::nullopt).has_value());
    // A kind not served: a check, an episode.
    CHECK(code(head("check", "observed", "reviewer/s70"), R"({"method":"rerun","result":"verified"})", SERVED,
               std::nullopt) == served::Code::kind);
    CHECK(code(head("episode", "observed", "reviewer/s70"), R"({"client":"claude-code"})", SERVED, std::nullopt) ==
          served::Code::kind);
    // A state the fold does not serve: expired, refuted, retired.
    for (const fold::Status st : {fold::Status::expired, fold::Status::refuted, fold::Status::retired}) {
        CHECK(code(line, claim, served::State{fold::State{st, 0, 0, fold::none}, 0}, std::nullopt) ==
              served::Code::state);
    }
    // A map's mark on another kind.
    CHECK(code(line, claim, SERVED, served::map_mark(Mark::fresh, "2026-10-17T12:06:00.000Z")) ==
          served::Code::mark);
    // A content without its band, its name, or with a band of another type.
    CHECK(code(line, R"({"claim":"x","subject":"entity:x"})", SERVED, std::nullopt) == served::Code::content);
    CHECK(code(line, R"({"band":"core","claim":"x"})", SERVED, std::nullopt) == served::Code::content);
    CHECK(code(line, R"({"band":1,"claim":"x","subject":"entity:x"})", SERVED, std::nullopt) ==
          served::Code::content);
    CHECK(code(head("map", "derived", "reviewer/s70"), R"({"band":"core","does":"x","object":{"type":"repo"}})",
               SERVED, std::nullopt) == served::Code::content);
    // A repo object without its path, a when without its quality, a content
    // that is not an object.
    CHECK(code(head("map", "derived", "reviewer/s70"),
               R"({"band":"core","does":"x","object":{"repository":"r","type":"repo"}})", SERVED,
               std::nullopt) == served::Code::content);
    CHECK(code(line, R"({"band":"core","claim":"x","subject":"entity:x","when":{"time":"2026-10-17T00:00:00.000Z"}})",
               SERVED, std::nullopt) == served::Code::content);
    CHECK(code(line, R"(["x"])", SERVED, std::nullopt) == served::Code::content);
    // An item without its state, or with a state of another type.
    CHECK(code(head("item", "claimed", "implementer/s39"), R"({"band":"core","item_id":"x","subject":"item:x"})",
               SERVED, std::nullopt) == served::Code::content);
    CHECK(code(head("item", "claimed", "implementer/s39"),
               R"({"band":"core","item_id":"x","state":1,"subject":"item:x"})", SERVED,
               std::nullopt) == served::Code::content);
}
