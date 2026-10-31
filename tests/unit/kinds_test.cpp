// kinds' cases: the 22 kinds against header's closed sets; served, the
// project-only kinds, the bases and a check's basis by its method; each
// kind's own fields and limits, a direction's clears, the common when and
// a lesson's pins among them; the format's changes (the checkpoint's and
// question's limits, detail, for_lane by import alone, question_id, a
// decision's closes, the common item in a project scope, when on retire);
// the common fields' carriage on each kind and its rules resolved; record
// identity on a map, an item, a clause, a checkpoint (its for_lane first)
// and a question with a question_id, and none elsewhere. Each is
// evaluated by the compiler on
// std::allocator; identity again at run time over tests/fixtures/, where A
// to E, ACC and ACC2, and every entry but the maps, have none, and M2, M1's
// revision with another digest, keeps M1's.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "entry/kinds_identity.hpp"
#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace header = qiven3::entry::header;
namespace kinds = qiven3::entry::kinds;
namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
using kinds::Basis;
using kinds::Carried;
using kinds::Common;
using kinds::Field;
using kinds::KINDS;
using kinds::Value;

constexpr const kinds::Kind& kind(std::string_view name) { return KINDS.at(*kinds::index_of(name)); }

constexpr std::optional<Field> field(std::string_view k, std::string_view name) {
    for (const Field& f : kinds::fields(kind(k))) {
        if (f.name == name) {
            return f;
        }
    }
    return std::nullopt;
}
constexpr std::optional<Field> key(kinds::Shape s, std::string_view name) {
    for (const Field& f : kinds::members(s)) {
        if (f.name == name) {
            return f;
        }
    }
    return std::nullopt;
}
constexpr std::size_t count(bool (*has)(const kinds::Kind&)) {
    std::size_t n = 0;
    for (const kinds::Kind& k : KINDS) {
        n += has(k) ? 1U : 0U;
    }
    return n;
}

// The kinds: 22, header's, 16 served, 7 in project scopes alone.
static_assert(KINDS.size() == 22 && KINDS.size() == header::kind_names.size());
static_assert(count([](const kinds::Kind& k) { return k.served; }) == 16);
static_assert(!kinds::served("episode") && !kinds::served("usage") && !kinds::served("forget"));
static_assert(kinds::served("direction") && kinds::served("map") && kinds::served("lesson"));
static_assert(!kinds::served("note") && !kinds::index_of("Claim"));
static_assert(count([](const kinds::Kind& k) { return k.group == kinds::Group::work; }) == 7);
static_assert(kinds::project_only("map") && kinds::project_only("checkpoint") && !kinds::project_only("lesson"));
static_assert(kinds::CHANGE_KINDS.size() == 4 && kind("forget").group == kinds::Group::change);
static_assert(!kind("forget").content && kinds::fields(kind("forget")).empty());

// The bases.
static_assert(kinds::allows("direction", Basis::owner_words) && !kinds::allows("direction", Basis::claimed));
static_assert(kinds::allows("accept", Basis::owner_words) && kinds::allows("forget", Basis::owner_words));
static_assert(kinds::allows("entity", Basis::observed) && kinds::allows("entity", Basis::claimed));
static_assert(kinds::allows("module", Basis::derived) && kinds::allows("module", Basis::claimed));
static_assert(kinds::allows("map", Basis::derived) && !kinds::allows("map", Basis::claimed));
static_assert(!kinds::allows("relation", Basis::observed) && !kinds::allows("note", Basis::claimed));
static_assert(kinds::allows("check", Basis::observed) && kinds::allows("check", Basis::claimed));
static_assert(kinds::check_basis("rerun") == Basis::observed && kinds::check_basis("review") == Basis::claimed);
static_assert(!kinds::check_basis("guess") && kinds::basis_of("owner-words") == Basis::owner_words);

// Every kind's fields: unique, none a common field's name, each text with
// its limit, each closed set and shape with its values or keys.
static_assert([] {
    for (const kinds::Kind& k : KINDS) {
        const std::span<const Field> own = kinds::fields(k);
        for (std::size_t i = 0; i < own.size(); ++i) {
            const Field& f = own[i];
            for (std::size_t j = i + 1; j < own.size(); ++j) {
                if (own[j].name == f.name.view()) {
                    return false;
                }
            }
            for (const Field& c : kinds::COMMON) {
                if (c.name == f.name.view()) {
                    return false;
                }
            }
            if ((f.value == Value::text && f.limit == 0) || (f.value == Value::one_of && kinds::values(f).empty()) ||
                (f.value == Value::shape && kinds::members(f.shape).empty())) {
                return false;
            }
        }
    }
    return true;
}());

// Fields and limits, a few of each table's rows.
static_assert(field("direction", "words")->limit == 2000 && field("direction", "words")->carried == Carried::required);
static_assert(field("observation", "revision")->value == Value::revision);
static_assert(field("question", "options")->items == 6 && field("question", "options")->limit == 200);
static_assert(field("clause", "text")->limit == 4000 && field("clause", "cites")->value == Value::clause_id);
static_assert(field("module", "paths")->items == 32 && field("module", "paths")->carried == Carried::required);
static_assert(field("item", "item_id")->value == Value::slug && field("item", "item_id")->limit == 64);
static_assert(kinds::values(*field("item", "state")).size() == 5 &&
              kinds::values(*field("entity", "entity_type")).size() == 7);
static_assert(field("checkpoint", "refs")->items == 32 && field("relation", "relation")->limit == 48);
static_assert(field("usage", "served")->items == 384 && field("usage", "cited")->items == 192);
static_assert(field("usage", "corrected")->items == 48 && field("usage", "truncated")->value == Value::only_true);
static_assert(field("accept", "message")->value == Value::transcript && field("map", "object")->value == Value::map_object);
static_assert(field("map", "relations")->shape == kinds::Shape::relation && !field("map", "subject"));
static_assert(kinds::values(*field("check", "method")).size() == 4 &&
              kinds::values(*field("check", "result")).size() == 2);

// A direction's clears: a content digest and a pattern's name.
static_assert(field("direction", "clears")->carried == Carried::optional && !field("account", "clears"));
static_assert(key(kinds::Shape::clears, "content")->value == Value::digest);
static_assert(key(kinds::Shape::clears, "pattern")->value == Value::pattern_name);
static_assert(kinds::members(kinds::Shape::clears).size() == 2);

// The common when: a time and one of four qualities, optional on served
// kinds and refused elsewhere.
static_assert(kinds::COMMON.at(static_cast<std::size_t>(Common::when)).shape == kinds::Shape::when);
static_assert(key(kinds::Shape::when, "time")->value == Value::time);
static_assert(kinds::values(*key(kinds::Shape::when, "quality")).size() == 4 &&
              kinds::members(kinds::Shape::when).size() == 2);
static_assert(kinds::common_carried(kind("lesson"), Common::when) == Carried::optional);
static_assert(kinds::common_carried(kind("usage"), Common::when) == Carried::refused);
static_assert(kinds::common_carried(kind("retire"), Common::when) == Carried::optional);
static_assert(kinds::common_carried(kind("check"), Common::when) == Carried::refused &&
              kinds::common_carried(kind("forget"), Common::when) == Carried::refused);

// The format's changes: the limits, detail, for_lane, question_id and
// closes.
static_assert(field("checkpoint", "progress")->limit == 1000 && field("checkpoint", "next_action")->limit == 2100);
static_assert(field("checkpoint", "decision_status")->limit == 1600 && field("question", "question")->limit == 1500);
static_assert(field("checkpoint", "detail")->limit == 8000 && field("checkpoint", "detail")->carried == Carried::optional);
static_assert(field("checkpoint", "for_lane")->value == Value::lane &&
              field("checkpoint", "for_lane")->carried == Carried::by_import);
static_assert(kinds::resolve(Carried::by_import, "import", Basis::claimed) == Carried::optional);
static_assert(kinds::resolve(Carried::by_import, "implementer/s39", Basis::claimed) == Carried::refused &&
              kinds::resolve(Carried::by_import, "loop", Basis::claimed) == Carried::refused);
static_assert(field("question", "question_id")->value == Value::slug && field("question", "question_id")->limit == 64);
static_assert(field("decision", "closes")->value == Value::id && field("decision", "closes")->items == 8 &&
              field("decision", "closes")->carried == Carried::optional);
static_assert(!field("claim", "for_lane") && !field("item", "detail") && !field("claim", "closes"));

// The common item: a slug of 64, on served kinds but item and map, in a
// project scope alone.
static_assert(kinds::COMMON.at(static_cast<std::size_t>(Common::item)).value == Value::slug &&
              kinds::COMMON.at(static_cast<std::size_t>(Common::item)).limit == 64);
static_assert(kinds::common_carried(kind("claim"), Common::item) == Carried::by_scope &&
              kinds::common_carried(kind("checkpoint"), Common::item) == Carried::by_scope);
static_assert(kinds::common_carried(kind("item"), Common::item) == Carried::refused &&
              kinds::common_carried(kind("map"), Common::item) == Carried::refused);
static_assert(kinds::common_carried(kind("retire"), Common::item) == Carried::refused &&
              kinds::common_carried(kind("usage"), Common::item) == Carried::refused &&
              kinds::common_carried(kind("episode"), Common::item) == Carried::refused);
static_assert(kinds::resolve(Carried::by_scope, "learner/s1", Basis::claimed, {}, "project:qiven") == Carried::optional);
static_assert(kinds::resolve(Carried::by_scope, "learner/s1", Basis::claimed, {}, "person") == Carried::refused &&
              kinds::resolve(Carried::by_scope, "learner/s1", Basis::claimed, {}, "life") == Carried::refused &&
              kinds::resolve(Carried::by_scope, "learner/s1", Basis::claimed) == Carried::refused);

// A lesson's pins: up to 8 of type (four), name 200 and version 64,
// required on a tool alone.
static_assert(field("lesson", "pins")->items == 8 && field("lesson", "pins")->shape == kinds::Shape::pin);
static_assert(kinds::values(*key(kinds::Shape::pin, "type")).size() == 4 &&
              key(kinds::Shape::pin, "name")->limit == 200);
static_assert(key(kinds::Shape::pin, "version")->limit == 64 && !field("claim", "pins"));
static_assert(kinds::resolve(key(kinds::Shape::pin, "version")->carried, "learner/s1", Basis::claimed, "tool") ==
              Carried::required);
static_assert(kinds::resolve(key(kinds::Shape::pin, "version")->carried, "learner/s1", Basis::claimed, "env") ==
              Carried::optional);
static_assert(kinds::at_least_one(kinds::Shape::trigger) && !kinds::at_least_one(kinds::Shape::applies_to));
static_assert(key(kinds::Shape::applies_to, "environment")->items == 8 && key(kinds::Shape::trigger, "entities")->limit == 160);

// The common fields on each kind, and their rules resolved.
static_assert(kinds::common_carried(kind("claim"), Common::subject) == Carried::required);
static_assert(kinds::common_carried(kind("relation"), Common::subject) == Carried::optional);
static_assert(kinds::common_carried(kind("map"), Common::subject) == Carried::refused);
static_assert(kinds::common_carried(kind("check"), Common::subject) == Carried::refused);
static_assert(kinds::common_carried(kind("usage"), Common::source) == Carried::refused);
static_assert(kinds::common_carried(kind("usage"), Common::evidence) == Carried::refused);
static_assert(kinds::common_carried(kind("episode"), Common::evidence) == Carried::optional);
static_assert(kinds::common_carried(kind("episode"), Common::episode) == Carried::refused);
static_assert(kinds::common_carried(kind("forget"), Common::source) == Carried::refused);
static_assert(kinds::common_carried(kind("check"), Common::band) == Carried::refused);
static_assert(kinds::common_carried(kind("goal"), Common::band) == Carried::required);
static_assert(kinds::common_carried(kind("clause"), Common::admit) == Carried::refused);
static_assert(kinds::common_carried(kind("accept"), Common::admit) == Carried::refused);
static_assert(kinds::common_carried(kind("map"), Common::admit) == Carried::by_admit);
static_assert(kinds::common_carried(kind("account"), Common::as_of) == Carried::required);
static_assert(kinds::common_carried(kind("entity"), Common::as_of) == Carried::optional);
static_assert(kinds::common_carried(kind("direction"), Common::as_of) == Carried::refused);
static_assert(kinds::resolve(Carried::by_basis, "implementer/s39", Basis::claimed) == Carried::required);
static_assert(kinds::resolve(Carried::by_basis, "implementer/s39", Basis::owner_words) == Carried::optional);
static_assert(kinds::resolve(Carried::by_writer, "loop", Basis::observed) == Carried::required);
static_assert(kinds::resolve(Carried::by_writer, "import", Basis::claimed) == Carried::optional);
static_assert(kinds::resolve(Carried::by_admit, "loop", Basis::claimed) == Carried::optional);
static_assert(kinds::resolve(Carried::by_admit, "import", Basis::claimed) == Carried::required);
static_assert(kinds::resolve(Carried::refused, "import", Basis::claimed) == Carried::refused);

// Record identity: a header of kind and lane, and the content given; the
// identity read while both are held.
struct Record {
    header::Header<> h;
    canon::Basic<> v;
    [[nodiscard]] constexpr std::optional<kinds::IdentityView> id() const { return kinds::identity(h, v); }
};
constexpr Record record(std::string_view k, std::string_view lane, std::string_view content) {
    header::FieldsView f;
    f.scope = "project:qiven";
    f.lane = lane;
    f.kind = k;
    f.writer = "reviewer/s70";
    f.basis = "claimed";
    f.at = "2026-10-17T09:30:00.000Z";
    canon::Basic<> v;
    if (!content.empty()) {
        canon::parse(content, v);
    }
    return Record{header::make(f), std::move(v)};
}
constexpr bool same(const Record& a, const Record& b) {
    const std::optional<kinds::IdentityView> x = a.id();
    const std::optional<kinds::IdentityView> y = b.id();
    return x.has_value() && y.has_value() && *x == *y;
}
constexpr bool none(const Record& r) { return !r.id().has_value(); }
constexpr bool part(const Record& r, std::size_t i, std::string_view want) {
    const std::optional<kinds::IdentityView> x = r.id();
    return x.has_value() && x->parts.at(i) == want;
}
constexpr std::string_view repo_a =
    R"({"object":{"digest":"hmac-sha256:01","path":"a/b.py","repository":"r","type":"repo"}})";
constexpr std::string_view repo_b =
    R"({"object":{"digest":"hmac-sha256:02","path":"a/b.py","repository":"r","type":"repo"}})";
constexpr std::string_view repo_named =
    R"({"object":{"digest":"hmac-sha256:01","name":"f","path":"a/b.py","repository":"r","type":"repo"}})";
constexpr std::string_view root_a = R"({"object":{"digest":"hmac-sha256:01","id":"sha256:aa","type":"entry"}})";

static_assert(none(record("claim", "implementer", R"({"claim":"x"})")));
static_assert(none(record("forget", "implementer", "")));
static_assert(same(record("map", "reviewer", repo_a), record("map", "learner", repo_b)));
static_assert(part(record("map", "reviewer", repo_a), 1, "r") && part(record("map", "reviewer", repo_a), 3, ""));
static_assert(!same(record("map", "reviewer", repo_a), record("map", "reviewer", repo_named)));
static_assert(!same(record("map", "reviewer", repo_a), record("map", "reviewer", root_a)));
static_assert(part(record("map", "reviewer", root_a), 0, "entry") && part(record("map", "reviewer", root_a), 1, "sha256:aa"));
static_assert(same(record("item", "a", R"({"item_id":"p3","state":"open"})"),
                   record("item", "b", R"({"item_id":"p3","state":"done"})")));
static_assert(!same(record("item", "a", R"({"item_id":"p3"})"), record("item", "a", R"({"item_id":"p4"})")));
static_assert(!same(record("item", "a", R"({"item_id":"Q14"})"), record("clause", "a", R"({"clause_id":"Q14"})")));
static_assert(part(record("clause", "a", R"({"clause_id":"ENG-110"})"), 0, "ENG-110"));
static_assert(same(record("checkpoint", "implementer", R"({"progress":"a"})"),
                   record("checkpoint", "implementer", R"({"progress":"b"})")));
static_assert(!same(record("checkpoint", "implementer", R"({"progress":"a"})"),
                    record("checkpoint", "reviewer", R"({"progress":"a"})")));
// A checkpoint's for_lane before its header's lane: the import's record of
// lane implementer is that lane's, as a live slot's is.
static_assert(part(record("checkpoint", "import", R"({"for_lane":"implementer"})"), 0, "implementer"));
static_assert(same(record("checkpoint", "import", R"({"for_lane":"implementer","progress":"a"})"),
                   record("checkpoint", "implementer", R"({"progress":"b"})")));
static_assert(!same(record("checkpoint", "import", R"({"for_lane":"implementer"})"),
                    record("checkpoint", "import", R"({"progress":"a"})")));
static_assert(!same(record("checkpoint", "import", R"({"for_lane":"implementer"})"),
                    record("checkpoint", "import", R"({"for_lane":"reviewer"})")));
// A question's question_id, in any lane; none without one.
static_assert(same(record("question", "a", R"({"question":"x","question_id":"q1"})"),
                   record("question", "b", R"({"question":"y","question_id":"q1"})")));
static_assert(!same(record("question", "a", R"({"question_id":"q1"})"), record("question", "a", R"({"question_id":"q2"})")));
static_assert(!same(record("question", "a", R"({"question_id":"p3"})"), record("item", "a", R"({"item_id":"p3"})")));
static_assert(none(record("question", "a", R"({"question":"x"})")) && none(record("question", "a", "")));
// A part the content lacks reads empty.
static_assert(part(record("item", "a", R"({"state":"open"})"), 0, ""));

// A fixture entry's header and content, held while its identity is read.
struct Held {
    header::Header<> h;
    canon::Basic<> v;
};
Held held(const fixtures::EntryView& e) {
    canon::Basic<> content;
    if (e.content) {
        canon::parse(*e.content, content);
    }
    return Held{header::parse_line(e.header), std::move(content)};
}

}  // namespace

QIVEN_CASE(kinds, identity) {
    canon::Basic<> v;
    fixtures::load_entries(v);
    std::size_t none = 0;
    for (std::size_t i = 0; i < fixtures::entry_count(v); ++i) {
        const fixtures::EntryView e = fixtures::entry(v, i);
        const Held x = held(e);
        const std::optional<kinds::IdentityView> id = kinds::identity(x.h, x.v);
        const bool map = e.name == "M1" || e.name == "M2";
        if (id.has_value() != map) {
            std::cout << "kinds.identity: " << e.name << (map ? " has none" : " has an identity") << "\n";
        }
        CHECK_EQ(id.has_value(), map);
        none += id ? 0U : 1U;
    }
    // M2, M1's revision with another digest, keeps M1's identity.
    const std::optional<fixtures::EntryView> m1 = fixtures::find_entry(v, "M1");
    const std::optional<fixtures::EntryView> m2 = fixtures::find_entry(v, "M2");
    REQUIRE(m1.has_value() && m2.has_value());
    const Held x1 = held(*m1);
    const Held x2 = held(*m2);
    const std::optional<kinds::IdentityView> i1 = kinds::identity(x1.h, x1.v);
    const std::optional<kinds::IdentityView> i2 = kinds::identity(x2.h, x2.v);
    REQUIRE(i1.has_value() && i2.has_value());
    CHECK(*i1 == *i2);
    CHECK_EQ(std::string(KINDS.at(i1->kind).name.view()), "map");
    CHECK_EQ(std::string(i1->parts[0]), "repo");
    CHECK_EQ(std::string(i1->parts[1]), "qiven-context-v3");
    CHECK_EQ(std::string(i1->parts[2]), "qiven3/entry.py");
    CHECK(i1->parts[3].empty());
    // A to E, ACC and ACC2 among the ten with none.
    for (const std::string_view name : {"A", "B", "C", "D", "ACC", "E", "ACC2"}) {
        const std::optional<fixtures::EntryView> e = fixtures::find_entry(v, name);
        REQUIRE(e.has_value());
        const Held x = held(*e);
        CHECK(!kinds::identity(x.h, x.v).has_value());
    }
    std::cout << "kinds.identity: " << none << " entries with none, M1 and M2 one identity\n";
    CHECK_EQ(none, 10U);
}

QIVEN_CASE(kinds, tables) {
    // Each kind's served flag, group and identity, printed for the record.
    std::size_t fields = 0;
    for (const kinds::Kind& k : KINDS) {
        fields += kinds::fields(k).size();
        CHECK_EQ(kinds::served(k.name.view()), k.served);
        CHECK_EQ(kinds::index_of(k.name.view()).has_value(), true);
    }
    std::cout << "kinds.tables: " << KINDS.size() << " kinds, " << fields << " fields of their own, "
              << kinds::COMMON.size() << " common\n";
    CHECK_EQ(fields, 74U);
    CHECK_EQ(kinds::COMMON.size(), 11U);
}
