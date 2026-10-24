// content's cases: each code on its field and the first in the table's
// order, basis and scope leading even past an unknown field or with no
// body, the admission test's presence, absence and statements, exact key
// sets, expires after at, and when's faults, evaluated by the compiler on
// std::allocator where no syntax is compiled; at run time when and pins
// each fault its code, a lesson of 500 记 admitted and of 501 too_long,
// the limits at their bounds, references and a map's object, the episode,
// the kinds' rules, the size past 65,536 bytes, and every entry of
// tests/fixtures/ passing.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "entry/content.hpp"
#include "entry/header.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace content = qiven3::entry::content;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using content::Code;
using content::ContentError;

#define AT "2026-10-17T09:30:00.000Z"
#define DIGEST "hmac-sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ID "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ID2 "sha256:1123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ADMIT R"("admit":{"mistake":"A reader would trust it.","underivable":"Said in a session."})"
#define EPISODE R"("episode":{"client":"claude-code","session":"s-1"})"
#define OUTPUT R"({"command":"python -I probe.py","digest":")" DIGEST R"(","type":"output"})"
#define TRANSCRIPT R"({"client":"claude-code","digest":")" DIGEST R"(","message":"m-1","session":"s-1","type":"transcript"})"

// An entry's header of these fields, and its content's verdict.
constexpr std::optional<ContentError> verdict(std::string_view kind, std::string_view writer, std::string_view basis,
                                              std::string_view scope, std::string_view text) {
    header::FieldsView f;
    f.scope = scope;
    f.lane = "implementer";
    f.kind = kind;
    f.writer = writer;
    f.basis = basis;
    f.at = AT;
    f.id = ID;
    const header::Header<> h = header::make(f);
    canon::Basic<> v;
    if (!text.empty()) {
        canon::parse(text, v);
    }
    return content::try_check(h, v);
}
constexpr bool is(const std::optional<ContentError>& e, Code c, std::string_view path) {
    return e.has_value() && e->code == c && e->path.view() == path;
}

// A direction by import compiles no syntax: its vectors run here.
constexpr std::optional<ContentError> direction(std::string_view text, std::string_view basis = "owner-words") {
    return verdict("direction", "import", basis, "project:qiven", text);
}
#define D_BAND R"("band":"core")"
#define D_TAIL R"("subject":"entity:owner","words":"Keep the tests pinned.")"
constexpr std::string_view d_ok = "{" ADMIT "," D_BAND "," D_TAIL "}";

static_assert(!direction(d_ok).has_value());
static_assert(content::name(Code::basis) == "basis" && content::name(Code::too_large) == "too_large");
// basis and scope lead, an unknown field behind them, and with no body.
static_assert(is(direction(d_ok, "claimed"), Code::basis, ""));
static_assert(is(direction("{" ADMIT "," D_BAND "," D_TAIL R"(,"zzz":1})", "claimed"), Code::basis, ""));
static_assert(is(verdict("goal", "import", "claimed", "person", R"({"zzz":1})"), Code::scope, ""));
static_assert(is(verdict("goal", "import", "claimed", "life", ""), Code::scope, ""));
static_assert(is(verdict("claim", "import", "derived", "person", ""), Code::basis, ""));
static_assert(!verdict("claim", "import", "claimed", "person", "").has_value());
static_assert(!verdict("forget", "implementer/s39", "owner-words", "project:qiven", "").has_value());
static_assert(is(verdict("forget", "implementer/s39", "claimed", "project:qiven", ""), Code::basis, ""));
// A check's basis by its method; observed or claimed off the four.
static_assert(is(verdict("check", "import", "claimed", "person", R"({"method":"test","result":"verified"})"),
                 Code::basis, ""));
static_assert(is(verdict("check", "import", "observed", "person", R"({"method":"review","result":"verified"})"),
                 Code::basis, ""));
static_assert(is(verdict("check", "import", "derived", "person", R"({"method":"guess","result":"verified"})"),
                 Code::basis, ""));
// A content that is no object.
static_assert(is(direction("[]"), Code::type, ""));
// unknown_field, then not_allowed: another kind's field, a common one this
// kind does not carry.
static_assert(is(direction("{" ADMIT "," D_BAND "," D_TAIL R"(,"zzz":1})"), Code::unknown_field, "zzz"));
static_assert(is(direction("{" ADMIT R"(,"as_of":"2026-01-01",)" D_BAND "," D_TAIL "}"), Code::not_allowed, "as_of"));
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"claim":"x",)" D_TAIL "}"), Code::not_allowed, "claim"));
static_assert(is(direction("{" ADMIT R"(,"as_of":"2026-01-01",)" D_BAND "," D_TAIL R"(,"zzz":1})"), Code::unknown_field,
                 "zzz"));
// A key of a shape alone is no kind's.
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"mistake":"x",)" D_TAIL "}"), Code::unknown_field, "mistake"));
// missing, the common fields first; not_allowed before it.
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"subject":"entity:owner"})"), Code::missing, "words"));
static_assert(is(direction("{" ADMIT "," D_TAIL "}"), Code::missing, "band"));
static_assert(is(direction(R"({"words":"w"})"), Code::missing, "subject"));
static_assert(is(direction("{" ADMIT R"(,"as_of":"2026-01-01","subject":"entity:owner"})"), Code::not_allowed, "as_of"));
// type, empty, syntax; missing before type, type before syntax.
static_assert(is(direction("{" ADMIT R"(,"band":1,)" D_TAIL "}"), Code::type, "band"));
static_assert(is(direction("{" ADMIT R"(,"band":1,"subject":"entity:owner"})"), Code::missing, "words"));
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"subject":"entity:owner","words":""})"), Code::empty, "words"));
static_assert(is(direction("{" ADMIT R"(,"band":"x",)" D_TAIL "}"), Code::syntax, "band"));
static_assert(is(direction("{" ADMIT R"(,"band":"x","subject":"entity:owner","words":7})"), Code::type, "words"));
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"subject":"owner","words":"w"})"), Code::syntax, "subject"));
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"subject":"entity:a b","words":"w"})"), Code::syntax, "subject"));
// The admission test: required for import, its statements exact, not
// empty, not blank, not equal; refused on an exempt kind.
static_assert(is(direction("{" D_BAND "," D_TAIL "}"), Code::missing, "admit"));
static_assert(is(direction(R"({"admit":{"mistake":"m"},)" D_BAND "," D_TAIL "}"), Code::missing, "admit.underivable"));
static_assert(is(direction(R"({"admit":{"mistake":"m","underivable":"u","x":"y"},)" D_BAND "," D_TAIL "}"),
                 Code::unknown_field, "admit.x"));
static_assert(is(direction(R"({"admit":{"mistake":"","underivable":"u"},)" D_BAND "," D_TAIL "}"), Code::empty,
                 "admit.mistake"));
static_assert(is(direction(R"({"admit":{"mistake":" )" "\xe3\x80\x80" R"(","underivable":"u"},)" D_BAND "," D_TAIL "}"), Code::empty,
                 "admit.mistake"));
static_assert(is(direction(R"({"admit":{"mistake":"same","underivable":"same"},)" D_BAND "," D_TAIL "}"),
                 Code::repeated, "admit"));
static_assert(is(direction(R"({"admit":["m"],)" D_BAND "," D_TAIL "}"), Code::type, "admit"));
static_assert(is(verdict("retire", "import", "claimed", "person", R"({"admit":{"mistake":"m","underivable":"u"},)"
                                                                  R"("reason":"r"})"),
                 Code::not_allowed, "admit"));
// expires later than the header's at; a time of the at's syntax.
static_assert(!direction("{" ADMIT "," D_BAND R"(,"expires":"2026-10-17T09:30:00.001Z",)" D_TAIL "}").has_value());
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"expires":")" AT R"(",)" D_TAIL "}"), Code::syntax, "expires"));
static_assert(is(direction("{" ADMIT "," D_BAND R"(,"expires":"2026-10-17",)" D_TAIL "}"), Code::syntax, "expires"));
// when: exact keys, a time, a quality of the four.
#define WHEN(x) "{" ADMIT "," D_BAND R"(,"subject":"entity:owner","when":)" x R"(,"words":"w"})"
static_assert(!direction(WHEN(R"({"quality":"anchored","time":"2026-10-17T05:00:00.000Z"})")).has_value());
static_assert(is(direction(WHEN(R"({"quality":"guess","time":")" AT R"("})")), Code::syntax, "when.quality"));
static_assert(is(direction(WHEN(R"({"quality":"exact","time":"2026-10-17T13:00:00+08:00"})")), Code::syntax,
                 "when.time"));
static_assert(is(direction(WHEN(R"({"time":")" AT R"("})")), Code::missing, "when.quality"));
static_assert(is(direction(WHEN(R"({"quality":"exact","time":")" AT R"(","zone":"+08:00"})")), Code::unknown_field,
                 "when.zone"));
static_assert(is(direction(WHEN(R"({"quality":1,"time":")" AT R"("})")), Code::type, "when.quality"));
static_assert(is(direction(WHEN(R"("2026-10-17")")), Code::type, "when"));
// applies_to: each key optional, environment at most 8.
static_assert(!direction(R"({"admit":{"mistake":"m","underivable":"u"},"applies_to":{"environment":["linux"]},)" D_BAND
                         "," D_TAIL "}")
                   .has_value());
static_assert(is(direction(R"({"admit":{"mistake":"m","underivable":"u"},"applies_to":{"environment":)"
                           R"(["1","2","3","4","5","6","7","8","9"]},)" D_BAND "," D_TAIL "}"),
                 Code::too_many, "applies_to.environment"));
static_assert(is(direction(R"({"admit":{"mistake":"m","underivable":"u"},"applies_to":{"environment":[]},)" D_BAND
                           "," D_TAIL "}"),
                 Code::empty, "applies_to.environment"));
// An account carries as_of, a real date.
static_assert(is(verdict("account", "import", "owner-words", "person", "{" ADMIT "," D_BAND "," D_TAIL "}"),
                 Code::missing, "as_of"));
static_assert(is(verdict("account", "import", "owner-words", "person",
                         "{" ADMIT R"(,"as_of":"2026-02-30",)" D_BAND "," D_TAIL "}"),
                 Code::syntax, "as_of"));
static_assert(!verdict("account", "import", "owner-words", "person", "{" ADMIT R"(,"as_of":"2024-02-29",)" D_BAND
                                                                      "," D_TAIL "}")
                   .has_value());

}  // namespace

namespace {

// A run-time vector: an entry's fields, its content, and the fault wanted,
// or none.
struct Vector {
    std::string_view name;
    std::string_view kind;
    std::string_view writer;
    std::string_view basis;
    std::string_view scope;
    std::string content;
    std::optional<Code> code;
    std::string_view path;
};

bool agrees(const Vector& t, const std::optional<ContentError>& e) {
    const bool same = e.has_value() == t.code.has_value() && (!e || (e->code == *t.code && e->path.view() == t.path));
    if (!same) {
        std::cout << "content.vectors: " << t.name << ": got "
                  << (e ? std::string(content::name(e->code)) + " at '" + std::string(e->path.view()) + "'"
                        : std::string("none"))
                  << "\n";
    }
    return same;
}

std::string repeat(std::string_view unit, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += unit;
    }
    return out;
}
std::string items(std::string_view item, std::size_t n) {
    std::string out = "[";
    for (std::size_t i = 0; i < n; ++i) {
        out += (i == 0 ? "" : ",");
        out += item;
    }
    return out + "]";
}

#define LESSON_HEAD "{" ADMIT R"(,"band":"normal",)" EPISODE R"(,"evidence":[)" OUTPUT R"(],"lesson":")"
#define TRIGGER R"("subject":"entity:cpython","trigger":{"activities":["pin-interpreter"]})"
#define WHEN_OK R"({"quality":"exact","time":"2026-10-17T05:00:00.000Z"})"
#define PIN_TOOL R"({"name":"python","type":"tool","version":"3.15.0"})"
#define PIN_ENV R"({"name":"PYTHONUTF8","type":"env"})"

// A lesson of this text, pins and when, each left out where empty.
std::string lesson(std::string_view text, std::string_view pins, std::string_view when = WHEN_OK) {
    std::string out = LESSON_HEAD;
    out += text;
    out += "\"";
    if (!pins.empty()) {
        out += R"(,"pins":)";
        out += pins;
    }
    out += "," TRIGGER;
    if (!when.empty()) {
        out += R"(,"when":)";
        out += when;
    }
    return out + "}";
}
Vector lesson_case(std::string_view name, std::string content, std::optional<Code> code, std::string_view path) {
    return Vector{name, "lesson", "learner/s42", "claimed", "project:qiven", std::move(content), code, path};
}

#define CLAIM_BY(evidence) "{" ADMIT R"(,"band":"normal","claim":"c",)" EPISODE R"(,"evidence":[)" evidence R"(],"subject":"entity:x"})"
#define CHECK_OF(method, evidence) "{" EPISODE R"(,"evidence":[)" evidence R"(],"method":")" method R"(","result":"verified"})"
#define REPO R"({"digest":")" DIGEST R"(","path":"rules/clauses.txt","repository":"rules","type":"repo"})"
#define MAP_OF(before, object, after) "{" ADMIT R"(,"band":"high",)" before R"("does":"d",)" EPISODE R"(,"object":)" object R"(,"object_type":"file")" after "}"
#define CONTRACT R"("contract":"c",)"
#define MAP_REPO R"({"digest":")" DIGEST R"(","path":"qiven3/entry.py","repository":"qiven-context-v3","type":"repo"})"
#define USAGE(tail) "{" EPISODE R"(,"served":[")" ID R"(",")" ID2 R"("])" tail "}"
#define EPISODE_KIND(client, session, started) R"({"client":")" client R"(","session":")" session R"(","started":")" started R"("})"
#define CLAUSE(id, tail) R"({"authority":"law","band":"core","clause_id":")" id R"(")" tail R"(,"subject":"clause:ENG-110","text":"t"})"
#define CLEARS(x) R"("band":"core","clears":)" x R"(,"subject":"entity:owner","words":"w"})"

std::vector<Vector> vectors() {
    const std::string ji = "\xe8\xae\xb0";  // 记, one code point of three bytes
    std::vector<Vector> v;
    // when and pins, each fault its code.
    v.push_back(lesson_case("lesson, pins and when", lesson("l", "[" PIN_TOOL "," PIN_ENV "]"), std::nullopt, ""));
    v.push_back(lesson_case("pins, an env with a version", lesson("l", R"([{"name":"HOME","type":"env","version":"1"}])"),
                            std::nullopt, ""));
    v.push_back(lesson_case("pins, a tool with no version", lesson("l", R"([{"name":"python","type":"tool"}])"),
                            Code::missing, "pins[0].version"));
    v.push_back(lesson_case("pins, a type off the four", lesson("l", R"([{"name":"linux","type":"os"}])"), Code::syntax,
                            "pins[0].type"));
    v.push_back(lesson_case("pins, a name with a space", lesson("l", R"([{"name":"a b","type":"env"}])"), Code::syntax,
                            "pins[0].name"));
    v.push_back(lesson_case("pins, a name of 201",
                            lesson("l", R"([{"name":")" + repeat("n", 201) + R"(","type":"env"}])"), Code::too_long,
                            "pins[0].name"));
    v.push_back(lesson_case("pins, a name of 200",
                            lesson("l", R"([{"name":")" + repeat("n", 200) + R"(","type":"env"}])"), std::nullopt, ""));
    v.push_back(lesson_case("pins, a version of 65",
                            lesson("l", R"([{"name":"python","type":"tool","version":")" + repeat("1", 65) + "\"}]"),
                            Code::too_long, "pins[0].version"));
    v.push_back(lesson_case("pins, 9", lesson("l", items(PIN_ENV, 9)), Code::too_many, "pins"));
    v.push_back(lesson_case("pins, 8", lesson("l", items(PIN_ENV, 8)), std::nullopt, ""));
    v.push_back(lesson_case("pins, none", lesson("l", "[]"), Code::empty, "pins"));
    v.push_back(lesson_case("pins, an object", lesson("l", PIN_ENV), Code::type, "pins"));
    v.push_back(lesson_case("pins, another key", lesson("l", R"([{"name":"a","path":"b","type":"env"}])"),
                            Code::unknown_field, "pins[0].path"));
    v.push_back(lesson_case("pins, no name", lesson("l", R"([{"type":"env"}])"), Code::missing, "pins[0].name"));
    v.push_back(lesson_case("pins, no type", lesson("l", R"([{"name":"a"}])"), Code::missing, "pins[0].type"));
    v.push_back(lesson_case("pins, a type of another JSON type", lesson("l", R"([{"name":"a","type":1}])"), Code::type,
                            "pins[0].type"));
    v.push_back(lesson_case("when, the zone's offset",
                            lesson("l", "", R"({"quality":"exact","time":"2026-10-17T13:00:00.000+08:00"})"),
                            Code::syntax, "when.time"));
    v.push_back(lesson_case("when, a quality off the four", lesson("l", "", R"({"quality":"guess","time":")" AT "\"}"),
                            Code::syntax, "when.quality"));
    v.push_back(lesson_case("when, no time", lesson("l", "", R"({"quality":"session"})"), Code::missing, "when.time"));
    v.push_back(lesson_case("when, empty time", lesson("l", "", R"({"quality":"session","time":""})"), Code::empty,
                            "when.time"));
    v.push_back(lesson_case("when and pins, the first code", lesson("l", R"([{"name":"a b","type":"tool"}])",
                                                                    R"({"quality":"guess","time":1})"),
                            Code::missing, "pins[0].version"));
    v.push_back(Vector{"pins on a claim", "claim", "implementer/s39", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","claim":"c",)" EPISODE R"(,"evidence":[)" OUTPUT R"(],"pins":[)" PIN_ENV
                       R"(],"subject":"entity:x"})",
                       Code::not_allowed, "pins"});
    v.push_back(Vector{"when on an episode", "episode", "loop", "observed", "person",
                       R"({"client":"claude-code","session":"s-1","started":")" AT R"(","when":)" WHEN_OK "}",
                       Code::not_allowed, "when"});
    // A lesson of 500 记 admitted, of 501 too_long.
    v.push_back(lesson_case("lesson, 500 of one Chinese character", lesson(repeat(ji, 500), "[" PIN_ENV "]"),
                            std::nullopt, ""));
    v.push_back(lesson_case("lesson, 501 of one Chinese character", lesson(repeat(ji, 501), "[" PIN_ENV "]"),
                            Code::too_long, "lesson"));
    v.push_back(lesson_case("trigger, no key",
                            LESSON_HEAD R"(l","subject":"entity:cpython","trigger":{}})", Code::missing, "trigger"));
    // References: evidence by refs, each code its path.
    v.push_back(lesson_case("evidence, absent on a claimed lesson",
                            "{" ADMIT R"(,"band":"normal",)" EPISODE R"(,"lesson":"l",)" TRIGGER "}", Code::missing,
                            "evidence"));
    v.push_back(Vector{"evidence, another key", "claim", "implementer/s39", "claimed", "person",
                       CLAIM_BY(R"({"command":"c","digest":")" DIGEST R"(","type":"output","x":"y"})"),
                       Code::unknown_field, "evidence[0].x"});
    v.push_back(Vector{"evidence, no digest", "claim", "implementer/s39", "claimed", "person",
                       CLAIM_BY(R"({"command":"c","type":"output"})"), Code::missing, "evidence[0].digest"});
    v.push_back(Vector{"evidence, a bad digest", "claim", "implementer/s39", "claimed", "person",
                       CLAIM_BY(R"({"command":"c","digest":"sha256:00","type":"output"})"), Code::syntax,
                       "evidence[0].digest"});
    v.push_back(Vector{"evidence, a command of 401", "claim", "implementer/s39", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","claim":"c",)" EPISODE R"(,"evidence":[{"command":")" +
                           repeat("c", 401) + R"(","digest":")" DIGEST R"(","type":"output"}],"subject":"entity:x"})",
                       Code::too_long, "evidence[0].command"});
    v.push_back(Vector{"evidence, 17", "claim", "implementer/s39", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","claim":"c",)" EPISODE R"(,"evidence":)" + items(OUTPUT, 17) +
                           R"(,"subject":"entity:x"})",
                       Code::too_many, "evidence"});
    // The episode: a slot's and the loop's, an import's optional.
    v.push_back(Vector{"no episode, a slot", "claim", "implementer/s39", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","claim":"c","evidence":[)" OUTPUT R"(],"subject":"entity:x"})",
                       Code::missing, "episode"});
    v.push_back(Vector{"no episode, import", "claim", "import", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","claim":"c","evidence":[)" OUTPUT R"(],"subject":"entity:x"})",
                       std::nullopt, ""});
    v.push_back(Vector{"no admit, loop", "claim", "loop", "claimed", "person",
                       R"({"band":"normal","claim":"c",)" EPISODE R"(,"evidence":[)" OUTPUT R"(],"subject":"entity:x"})",
                       std::nullopt, ""});
    v.push_back(Vector{"an episode's client", "claim", "loop", "claimed", "person",
                       R"({"band":"normal","claim":"c","episode":{"client":"Claude","session":"s-1"},"evidence":[)" OUTPUT
                       R"(],"subject":"entity:x"})",
                       Code::syntax, "episode.client"});
    v.push_back(Vector{"an episode entry", "episode", "loop", "observed", "person",
                       EPISODE_KIND("zcode", "a.b_c-1", AT), std::nullopt, ""});
    v.push_back(Vector{"an episode entry's session", "episode", "loop", "observed", "person",
                       EPISODE_KIND("zcode", "-a", AT), Code::syntax, "session"});
    v.push_back(Vector{"an episode entry's start", "episode", "loop", "observed", "person",
                       EPISODE_KIND("zcode", "a", "2026-10-17"), Code::syntax, "started"});
    v.push_back(Vector{"an episode entry's admit", "episode", "implementer/s39", "observed", "person",
                       R"({"admit":{"mistake":"m","underivable":"u"},"client":"zcode","session":"a","started":")" AT "\"}",
                       Code::not_allowed, "admit"});
    // The kinds' rules.
    v.push_back(Vector{"reflect, no transcript", "claim", "reflect/s5", "claimed", "person", CLAIM_BY(OUTPUT),
                       Code::missing, "evidence"});
    v.push_back(Vector{"reflect, a transcript", "claim", "reflect/s5", "claimed", "person",
                       CLAIM_BY(OUTPUT "," TRANSCRIPT), std::nullopt, ""});
    v.push_back(Vector{"check by transcript, none", "check", "reviewer/s70", "observed", "person",
                       CHECK_OF("transcript", OUTPUT), Code::missing, "evidence"});
    v.push_back(Vector{"check by transcript", "check", "reviewer/s70", "observed", "person",
                       CHECK_OF("transcript", TRANSCRIPT), std::nullopt, ""});
    v.push_back(Vector{"check by review, claimed", "check", "reviewer/s70", "claimed", "person",
                       CHECK_OF("review", OUTPUT), std::nullopt, ""});
    v.push_back(Vector{"map, neither contract nor constraints", "map", "reviewer/s70", "derived", "project:qiven",
                       MAP_OF("", MAP_REPO, ""), Code::missing, "contract"});
    v.push_back(Vector{"map, constraints alone", "map", "reviewer/s70", "derived", "project:qiven",
                       "{" ADMIT R"(,"band":"high","constraints":["k"],"does":"d",)" EPISODE R"(,"object":)" MAP_REPO
                       R"(,"object_type":"file"})",
                       std::nullopt, ""});
    v.push_back(Vector{"map, a revision", "map", "reviewer/s70", "derived", "project:qiven",
                       MAP_OF(CONTRACT, R"({"digest":")" DIGEST R"(","path":"a.py","repository":"r","revision":")"
                              "0123456789abcdef0123456789abcdef01234567" R"(","type":"repo"})", ""),
                       Code::unknown_field, "object.revision"});
    v.push_back(Vector{"map, an output object", "map", "reviewer/s70", "derived", "project:qiven",
                       MAP_OF(CONTRACT, R"({"command":"c","digest":")" DIGEST R"(","type":"output"})", ""),
                       Code::unknown_field, "object.command"});
    v.push_back(Vector{"map, a transcript object", "map", "reviewer/s70", "derived", "project:qiven",
                       MAP_OF(CONTRACT, R"({"digest":")" DIGEST R"(","type":"transcript"})", ""),
                       Code::syntax, "object.type"});
    v.push_back(Vector{"map, a root", "map", "reviewer/s70", "derived", "project:qiven",
                       MAP_OF(CONTRACT, R"({"digest":")" DIGEST R"(","id":")" ID R"(","type":"entry"})", ""),
                       std::nullopt, ""});
    v.push_back(Vector{"map, a root with no digest", "map", "reviewer/s70", "derived", "project:qiven",
                       MAP_OF(CONTRACT, R"({"id":")" ID R"(","type":"entry"})", ""), Code::missing,
                       "object.digest"});
    v.push_back(Vector{"map, a person scope", "map", "reviewer/s70", "derived", "person",
                       MAP_OF(CONTRACT, MAP_REPO, ""), Code::scope, ""});
    v.push_back(Vector{"map, a subject", "map", "reviewer/s70", "derived", "project:qiven",
                       MAP_OF(CONTRACT, MAP_REPO, R"(,"subject":"entity:x")"), Code::not_allowed, "subject"});
    v.push_back(Vector{"clause, its source", "clause", "import", "derived", "project:qiven",
                       CLAUSE("ENG-110", R"(,"source":[)" REPO "]"), std::nullopt, ""});
    v.push_back(Vector{"clause, no source", "clause", "import", "derived", "project:qiven", CLAUSE("ENG-110", ""),
                       Code::missing, "source"});
    v.push_back(Vector{"clause, its id", "clause", "import", "derived", "project:qiven",
                       CLAUSE("eng-110", R"(,"source":[)" REPO "]"), Code::syntax, "clause_id"});
    v.push_back(Vector{"accept, an output message", "accept", "implementer/s39", "owner-words", "person",
                       "{" EPISODE R"(,"message":)" OUTPUT "}", Code::syntax, "message.type"});
    v.push_back(Vector{"accept, a transcript", "accept", "implementer/s39", "owner-words", "person",
                       "{" EPISODE R"(,"message":)" TRANSCRIPT "}", std::nullopt, ""});
    v.push_back(Vector{"direction, clears", "direction", "import", "owner-words", "person",
                       "{" ADMIT "," CLEARS(R"({"content":")" DIGEST R"(","pattern":"tencent-akid"})"),
                       std::nullopt, ""});
    v.push_back(Vector{"direction, clears a name off the twelve", "direction", "import", "owner-words", "person",
                       "{" ADMIT "," CLEARS(R"({"content":")" DIGEST R"(","pattern":"akid"})"),
                       Code::syntax, "clears.pattern"});
    // Lists of ids, slugs, test ids, paths and names.
    v.push_back(Vector{"usage", "usage", "loop", "observed", "person", USAGE(R"(,"truncated":true)"), std::nullopt, ""});
    v.push_back(Vector{"usage, an id repeated", "usage", "loop", "observed", "person",
                       "{" EPISODE R"(,"served":[")" ID R"(",")" ID2 R"(",")" ID R"("]})", Code::repeated,
                       "served[2]"});
    v.push_back(Vector{"usage, truncated false", "usage", "loop", "observed", "person", USAGE(R"(,"truncated":false)"),
                       Code::syntax, "truncated"});
    v.push_back(Vector{"usage, a source", "usage", "loop", "observed", "person",
                       "{" EPISODE R"(,"served":[")" ID R"("],"source":[)" OUTPUT "]}", Code::not_allowed, "source"});
    v.push_back(Vector{"usage, corrected with no by", "usage", "loop", "observed", "person",
                       "{\"corrected\":[{\"entry\":\"" ID "\"}]," EPISODE R"(,"served":[")" ID R"("]})", Code::missing,
                       "corrected[0].by"});
    v.push_back(Vector{"item, its id", "item", "import", "claimed", "project:qiven",
                       "{" ADMIT R"(,"band":"normal","evidence":[)" OUTPUT R"(],"item_id":"P3","state":"open",)"
                       R"("subject":"item:p3"})",
                       Code::syntax, "item_id"});
    v.push_back(Vector{"module, a test id with a line", "module", "import", "derived", "project:qiven",
                       "{" ADMIT R"(,"band":"normal","covered_by":["tests.unit.Digest.test_id","x.py:12"],)"
                       R"("paths":["qiven3/entry.py"],"responsibility":"r","subject":"module:qiven3.entry"})",
                       Code::syntax, "covered_by[1]"});
    v.push_back(Vector{"module, a path up", "module", "import", "derived", "project:qiven",
                       "{" ADMIT R"(,"band":"normal","paths":["../x.py"],"responsibility":"r","subject":"module:m"})",
                       Code::syntax, "paths[0]"});
    v.push_back(Vector{"relation, a slug of 49", "relation", "import", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","evidence":[)" OUTPUT R"(],"from":"entity:a","relation":")" +
                           repeat("r", 49) + R"(","to":"entity:b"})",
                       Code::too_long, "relation"});
    v.push_back(Vector{"decision, a reason of 301", "decision", "import", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","decides":"d","evidence":[)" OUTPUT R"(],"reasons":[")" +
                           repeat("r", 301) + R"("],"subject":"entity:x"})",
                       Code::too_long, "reasons[0]"});
    v.push_back(Vector{"a subject of 160", "claim", "import", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","claim":"c","evidence":[)" OUTPUT R"(],"subject":"entity:)" +
                           repeat("s", 153) + "\"}",
                       std::nullopt, ""});
    v.push_back(Vector{"a subject of 161", "claim", "import", "claimed", "person",
                       "{" ADMIT R"(,"band":"normal","claim":"c","evidence":[)" OUTPUT R"(],"subject":"entity:)" +
                           repeat("s", 154) + "\"}",
                       Code::too_long, "subject"});
    return v;
}

// A module's content of four-byte characters at every field's limit: past
// 65,536 bytes canonical, within each field's limit.
std::string large_module() {
    const std::string face = "\xf0\x9f\x98\x80";
    const std::string path = "\"" + repeat(face, 200) + "\"";
    const std::string name = "\"" + repeat(face, 200) + "\"";
    const std::string invariant = "\"" + repeat(face, 300) + "\"";
    const std::string subject = "\"entity:" + repeat(face, 153) + "\"";
    return "{" ADMIT R"(,"applies_to":{"entities":)" + items(subject, 16) + R"(,"environment":)" + items(path, 8) +
           R"(,"paths":)" + items(path, 16) + R"(},"band":"normal","interface":)" + items(name, 16) +
           R"(,"invariants":)" + items(invariant, 16) + R"(,"paths":)" + items(path, 32) +
           R"(,"responsibility":")" + repeat(face, 500) + R"(","subject":"module:m"})";
}

std::optional<ContentError> run(const Vector& t) {
    return verdict(t.kind, t.writer, t.basis, t.scope, t.content);
}

}  // namespace

QIVEN3_CASE(content, vectors) {
    std::size_t passed = 0;
    std::size_t refused = 0;
    const std::vector<Vector> all = vectors();
    for (const Vector& t : all) {
        const std::optional<ContentError> e = run(t);
        CHECK(agrees(t, e));
        (e ? refused : passed) += 1;
    }
    std::cout << "content.vectors: " << all.size() << " vectors, " << passed << " passed, " << refused
              << " refused, each as wanted\n";
}

QIVEN3_CASE(content, limits) {
    // A lesson of 501 Chinese characters: the limit and the count measured.
    const std::string ji = "\xe8\xae\xb0";
    const std::optional<ContentError> e = run(lesson_case("501", lesson(repeat(ji, 501), ""), std::nullopt, ""));
    REQUIRE(e.has_value());
    CHECK_EQ(e->limit, 500U);
    CHECK_EQ(e->value, 501U);
    CHECK_EQ(repeat(ji, 500).size(), 1500U);
    const std::optional<ContentError> pins = run(lesson_case("9", lesson("l", items(PIN_ENV, 9)), std::nullopt, ""));
    REQUIRE(pins.has_value());
    CHECK_EQ(pins->limit, 8U);
    CHECK_EQ(pins->value, 9U);
    // Past 65,536 bytes canonical, each field within its limit.
    const std::string big = large_module();
    const std::optional<ContentError> large =
        verdict("module", "import", "derived", "project:qiven", big);
    REQUIRE(large.has_value());
    CHECK_EQ(std::string(content::name(large->code)), "too_large");
    CHECK_EQ(large->limit, 65536U);
    CHECK_EQ(large->value, big.size());
    CHECK(large->path.view().empty());
    std::cout << "content.limits: 501 characters too_long at 500, 9 pins too_many at 8, " << big.size()
              << " bytes too_large at 65536\n";
}

QIVEN3_CASE(content, fixtures) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::size_t n = 0;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        const header::Header<> h = header::parse_line(e.header);
        canon::Basic<> c;
        if (e.content) {
            canon::parse(*e.content, c);
        }
        const std::optional<ContentError> fault = content::try_check(h, c);
        if (fault) {
            std::cout << "content.fixtures: " << e.name << ": " << content::name(fault->code) << " at '"
                      << fault->path.view() << "'\n";
        }
        CHECK(!fault.has_value());
        ++n;
    }
    std::cout << "content.fixtures: " << n << " entries, each passing\n";
    CHECK_EQ(n, 12U);
    // L1's lesson of 500 记 passes, of 501 is too_long, under L1's header.
    const std::optional<fixtures::EntryView> l1 = fixtures::find_entry(v, "L1");
    REQUIRE(l1.has_value() && l1->content.has_value());
    const header::Header<> h = header::parse_line(l1->header);
    const std::string text(*l1->content);
    const std::size_t from = text.find("\"lesson\":\"") + 10;
    const std::size_t to = text.find('"', from);
    for (const std::size_t count : {500U, 501U}) {
        std::string changed = text;
        changed.replace(from, to - from, repeat("\xe8\xae\xb0", count));
        canon::Basic<> c;
        canon::parse(changed, c);
        const std::optional<ContentError> fault = content::try_check(h, c);
        CHECK_EQ(fault.has_value(), count == 501U);
        if (fault) {
            CHECK_EQ(std::string(content::name(fault->code)), "too_long");
            CHECK_EQ(std::string(fault->path.view()), "lesson");
        }
    }
}
