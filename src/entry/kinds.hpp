#pragma once

// kinds: the 22 kinds of this minor version as tables. Each kind has its
// name, the bases it allows, its group (a core kind in every scope, a work
// profile kind or the map in project scopes alone, a change, or usage),
// whether it is served, whether it has content, what its record identity
// holds, and its own fields; the common fields are one table beside them,
// each carried on a kind by its row's rule. A field has its name, its
// value (text, a closed set, a named syntax, a reference, an object of
// exact keys), how it is carried, its limit in characters (per item on a
// list) and, on a list, its most items; an object's keys are a shape's
// fields. A rule that turns on the writer, the basis or a pin's type is
// carried as such, and resolve reads it for one entry. kinds checks no
// content: content reads these tables, its codes and their order its own.
//
// served(kind) is whether a kind is served: all but episode, usage and the
// change kinds. identity(header, content) is a record's identity in its
// scope: a map's object (its type, then repository, path and name, or the
// root's id), an item's item_id, a clause's clause_id, a checkpoint's
// lane; none for every other kind. Its parts view the header's and the
// content's text, the content as content's check passed it; a part the
// content lacks reads empty.
//
// Allocations: none; every operation reads its arguments in place and
// returns views or values. Each is constexpr, so its vectors run in
// constant evaluation too; it reads by index, with no pointer arithmetic.
// kinds logs nothing.
//
// thread: none

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "entry/header.hpp"
#include "qiven3/canon.hpp"

namespace qiven3::entry::kinds {

// The bases, in header's order; a kind's bases are a set of their bits.
enum class Basis : std::uint8_t { owner_words, observed, claimed, derived };
using Bases = std::uint8_t;
constexpr Bases bit(Basis b) noexcept { return static_cast<Bases>(1U << static_cast<unsigned>(b)); }
constexpr std::optional<Basis> basis_of(std::string_view name) noexcept {
    for (std::size_t i = 0; i < header::basis_names.size(); ++i) {
        if (header::basis_names[i] == name) {
            return static_cast<Basis>(i);
        }
    }
    return std::nullopt;
}

// Content is at most this many bytes canonical, apart from the fields'
// limits.
inline constexpr std::size_t content_limit = 65536;

// How a field is carried: refused (not_allowed), optional or required, or
// by a rule resolve reads.
enum class Carried : std::uint8_t {
    refused,
    optional,
    required,
    by_basis,   // required on basis claimed or observed, else optional
    by_writer,  // required for a slot or loop, optional for import
    by_admit,   // required for a slot or import, optional for loop
    by_type,    // required on a pin of type tool, else optional
};

// A field's value, or a list item's. text is a string within its limit;
// one_of a string of its closed set; shape an object of its shape's exact
// keys; the rest a string of the named syntax, a reference (as the
// entry's references are), or true alone.
enum class Value : std::uint8_t {
    text,
    one_of,
    shape,
    subject,
    slug,
    time,
    date,
    id,
    digest,
    revision,
    clause_id,
    test_id,
    path,
    qualified_name,
    client,
    session,
    pattern_name,
    pin_name,
    reference,
    transcript,
    map_object,
    only_true,
};

// The objects of exact keys, each a table of fields.
enum class Shape : std::uint8_t { none, episode, admit, trigger, applies_to, relation, corrected, clears, when, pin };

struct Field {
    std::string_view name;
    Value value = Value::text;
    Carried carried = Carried::optional;
    std::uint16_t items = 0;  // a list's most items; 0, no list
    std::uint16_t limit = 0;  // characters, per item on a list; 0, none
    std::span<const std::string_view> values{};
    Shape shape = Shape::none;
};

namespace detail {

constexpr Field text(std::string_view name, Carried c, std::uint16_t limit) noexcept {
    return Field{name, Value::text, c, 0, limit, {}, Shape::none};
}
constexpr Field texts(std::string_view name, Carried c, std::uint16_t items, std::uint16_t limit) noexcept {
    return Field{name, Value::text, c, items, limit, {}, Shape::none};
}
constexpr Field of(std::string_view name, Value v, Carried c, std::uint16_t limit = 0) noexcept {
    return Field{name, v, c, 0, limit, {}, Shape::none};
}
constexpr Field list(std::string_view name, Value v, Carried c, std::uint16_t items, std::uint16_t limit = 0) noexcept {
    return Field{name, v, c, items, limit, {}, Shape::none};
}
constexpr Field one(std::string_view name, Carried c, std::span<const std::string_view> values) noexcept {
    return Field{name, Value::one_of, c, 0, 0, values, Shape::none};
}
constexpr Field object(std::string_view name, Carried c, Shape s, std::uint16_t items = 0) noexcept {
    return Field{name, Value::shape, c, items, 0, {}, s};
}

constexpr Carried R = Carried::required;
constexpr Carried O = Carried::optional;
constexpr Carried X = Carried::refused;

// The closed sets.
inline constexpr std::array<std::string_view, 4> bands = {"core", "high", "normal", "low"};
inline constexpr std::array<std::string_view, 4> qualities = {"exact", "anchored", "session", "approximate"};
inline constexpr std::array<std::string_view, 4> pin_types = {"tool", "api", "env", "config"};
inline constexpr std::array<std::string_view, 7> entity_types = {"person",     "place",        "device", "repository",
                                                                 "service",    "organization", "other"};
inline constexpr std::array<std::string_view, 5> item_states = {"open", "active", "waiting", "done", "dropped"};
inline constexpr std::array<std::string_view, 5> object_types = {"file", "ruling", "law", "item", "other"};
inline constexpr std::array<std::string_view, 2> results = {"verified", "refuted"};
inline constexpr std::array<std::string_view, 4> methods = {"test", "rerun", "transcript", "review"};

// The shapes' fields.
inline constexpr std::array<Field, 2> episode_keys = {of("client", Value::client, R), of("session", Value::session, R)};
inline constexpr std::array<Field, 2> admit_keys = {text("underivable", R, 200), text("mistake", R, 200)};
inline constexpr std::array<Field, 3> trigger_keys = {list("activities", Value::slug, O, 16, 200),
                                                      list("paths", Value::path, O, 16, 200),
                                                      list("entities", Value::subject, O, 16, 160)};
inline constexpr std::array<Field, 4> applies_keys = {
    list("activities", Value::slug, O, 16, 200), list("paths", Value::path, O, 16, 200),
    list("entities", Value::subject, O, 16, 160), texts("environment", O, 8, 200)};
inline constexpr std::array<Field, 2> relation_keys = {of("relation", Value::slug, R, 48), of("to", Value::subject, R, 160)};
inline constexpr std::array<Field, 2> corrected_keys = {of("entry", Value::id, R), of("by", Value::id, R)};
inline constexpr std::array<Field, 2> clears_keys = {of("content", Value::digest, R), of("pattern", Value::pattern_name, R)};
inline constexpr std::array<Field, 2> when_keys = {of("time", Value::time, R), one("quality", R, qualities)};
inline constexpr std::array<Field, 3> pin_keys = {one("type", R, pin_types), of("name", Value::pin_name, R, 200),
                                                  text("version", Carried::by_type, 64)};

}  // namespace detail

// A shape's exact keys; whether at least one of them must be present.
constexpr std::span<const Field> members(Shape s) noexcept {
    switch (s) {
        case Shape::episode: return detail::episode_keys;
        case Shape::admit: return detail::admit_keys;
        case Shape::trigger: return detail::trigger_keys;
        case Shape::applies_to: return detail::applies_keys;
        case Shape::relation: return detail::relation_keys;
        case Shape::corrected: return detail::corrected_keys;
        case Shape::clears: return detail::clears_keys;
        case Shape::when: return detail::when_keys;
        case Shape::pin: return detail::pin_keys;
        case Shape::none: break;
    }
    return {};
}
constexpr bool at_least_one(Shape s) noexcept { return s == Shape::trigger; }

// The common fields, in the table's order; each kind carries them by
// common_carried. Their carriage here is a placeholder.
enum class Common : std::uint8_t { subject, applies_to, source, evidence, episode, band, admit, expires, when, as_of };
inline constexpr std::array<Field, 10> COMMON = {
    detail::of("subject", Value::subject, Carried::optional, 160),
    detail::object("applies_to", Carried::optional, Shape::applies_to),
    detail::list("source", Value::reference, Carried::optional, 8),
    detail::list("evidence", Value::reference, Carried::optional, 16),
    detail::object("episode", Carried::optional, Shape::episode),
    detail::one("band", Carried::optional, detail::bands),
    detail::object("admit", Carried::optional, Shape::admit),
    detail::of("expires", Value::time, Carried::optional),
    detail::object("when", Carried::optional, Shape::when),
    detail::of("as_of", Value::date, Carried::optional),
};

namespace detail {

// Each kind's own fields, in the tables' order.
inline constexpr std::array<Field, 2> direction_fields = {text("words", R, 2000), object("clears", O, Shape::clears)};
inline constexpr std::array<Field, 1> account_fields = {text("words", R, 2000)};
inline constexpr std::array<Field, 3> entity_fields = {text("name", R, 120), one("entity_type", R, entity_types),
                                                       text("description", O, 500)};
inline constexpr std::array<Field, 4> observation_fields = {text("command", R, 400), text("result", R, 500),
                                                            of("revision", Value::revision, O),
                                                            text("environment", O, 200)};
inline constexpr std::array<Field, 1> claim_fields = {text("claim", R, 1000)};
inline constexpr std::array<Field, 2> decision_fields = {text("decides", R, 500), texts("reasons", O, 8, 300)};
inline constexpr std::array<Field, 3> question_fields = {text("question", R, 500), texts("options", O, 6, 200),
                                                         text("recommendation", O, 500)};
inline constexpr std::array<Field, 4> lesson_fields = {text("lesson", R, 500), object("trigger", R, Shape::trigger),
                                                       list("covered_by", Value::test_id, O, 16),
                                                       object("pins", O, Shape::pin, 8)};
inline constexpr std::array<Field, 4> episode_fields = {of("client", Value::client, R), of("session", Value::session, R),
                                                        of("started", Value::time, R), text("topic", O, 200)};
inline constexpr std::array<Field, 4> relation_fields = {of("from", Value::subject, R, 160),
                                                         of("relation", Value::slug, R, 48),
                                                         of("to", Value::subject, R, 160), text("note", O, 300)};
inline constexpr std::array<Field, 3> goal_fields = {text("objective", R, 500), text("boundary", O, 500),
                                                     texts("test_questions", O, 8, 300)};
inline constexpr std::array<Field, 5> clause_fields = {
    of("clause_id", Value::clause_id, R, 64), text("text", R, 4000), text("authority", R, 120),
    list("cites", Value::clause_id, O, 16, 64), of("superseded_by", Value::clause_id, O, 64)};
inline constexpr std::array<Field, 5> module_fields = {
    text("responsibility", R, 500), list("paths", Value::path, R, 32, 200),
    list("interface", Value::qualified_name, O, 16, 200), texts("invariants", O, 16, 300),
    list("covered_by", Value::test_id, O, 16)};
inline constexpr std::array<Field, 6> item_fields = {
    of("item_id", Value::slug, R, 64), one("state", R, item_states), text("title", O, 200), text("turn", O, 64),
    list("waits_on", Value::slug, O, 16, 64), of("under", Value::slug, O, 64)};
inline constexpr std::array<Field, 4> checkpoint_fields = {text("progress", R, 1000), text("next_action", R, 500),
                                                           text("decision_status", O, 300),
                                                           list("refs", Value::id, O, 32)};
inline constexpr std::array<Field, 3> measure_fields = {text("measure", R, 500), text("success", O, 300),
                                                        texts("results", O, 8, 300)};
inline constexpr std::array<Field, 6> map_fields = {
    of("object", Value::map_object, R), one("object_type", R, object_types), text("does", R, 500),
    object("relations", O, Shape::relation, 16), text("contract", O, 1000), texts("constraints", O, 8, 300)};
inline constexpr std::array<Field, 3> check_fields = {one("result", R, results), one("method", R, methods),
                                                      text("note", O, 500)};
inline constexpr std::array<Field, 2> accept_fields = {of("message", Value::transcript, R), text("words", O, 500)};
inline constexpr std::array<Field, 1> retire_fields = {text("reason", R, 300)};
inline constexpr std::array<Field, 4> usage_fields = {list("served", Value::id, R, 384), list("cited", Value::id, O, 192),
                                                      object("corrected", O, Shape::corrected, 48),
                                                      of("truncated", Value::only_true, O)};

}  // namespace detail

// A kind's group: core in every scope; work, the work profile and the
// map, in project scopes alone; a change; usage.
enum class Group : std::uint8_t { core, work, change, usage };

// What a record's identity holds, or none.
enum class Holds : std::uint8_t { none, object, item_id, clause_id, lane };

struct Kind {
    std::string_view name;
    Bases bases = 0;
    Group group = Group::core;
    bool served = false;
    bool content = true;
    Holds identity = Holds::none;
    std::span<const Field> fields{};
};

namespace detail {

constexpr Bases OW = bit(Basis::owner_words);
constexpr Bases OB = bit(Basis::observed);
constexpr Bases CL = bit(Basis::claimed);
constexpr Bases DE = bit(Basis::derived);
constexpr Group core = Group::core;
constexpr Group work = Group::work;

}  // namespace detail

// The kinds, in header's order of its kind names. A check allows observed
// or claimed, its method choosing (check_basis).
inline constexpr std::array<Kind, 22> KINDS = {{
    {"direction", detail::OW, detail::core, true, true, Holds::none, detail::direction_fields},
    {"account", detail::OW, detail::core, true, true, Holds::none, detail::account_fields},
    {"entity", detail::OB | detail::CL, detail::core, true, true, Holds::none, detail::entity_fields},
    {"observation", detail::OB, detail::core, true, true, Holds::none, detail::observation_fields},
    {"claim", detail::CL, detail::core, true, true, Holds::none, detail::claim_fields},
    {"decision", detail::CL, detail::core, true, true, Holds::none, detail::decision_fields},
    {"question", detail::CL, detail::core, true, true, Holds::none, detail::question_fields},
    {"lesson", detail::CL, detail::core, true, true, Holds::none, detail::lesson_fields},
    {"episode", detail::OB | detail::CL, detail::core, false, true, Holds::none, detail::episode_fields},
    {"relation", detail::CL, detail::core, true, true, Holds::none, detail::relation_fields},
    {"goal", detail::CL, detail::work, true, true, Holds::none, detail::goal_fields},
    {"clause", detail::DE, detail::work, true, true, Holds::clause_id, detail::clause_fields},
    {"module", detail::CL | detail::DE, detail::work, true, true, Holds::none, detail::module_fields},
    {"item", detail::CL, detail::work, true, true, Holds::item_id, detail::item_fields},
    {"checkpoint", detail::CL, detail::work, true, true, Holds::lane, detail::checkpoint_fields},
    {"measure", detail::CL | detail::OB, detail::work, true, true, Holds::none, detail::measure_fields},
    {"map", detail::DE, detail::work, true, true, Holds::object, detail::map_fields},
    {"check", detail::OB | detail::CL, Group::change, false, true, Holds::none, detail::check_fields},
    {"accept", detail::OW, Group::change, false, true, Holds::none, detail::accept_fields},
    {"retire", detail::CL, Group::change, false, true, Holds::none, detail::retire_fields},
    {"forget", detail::OW, Group::change, false, false, Holds::none, {}},
    {"usage", detail::OB | detail::CL, Group::usage, false, true, Holds::none, detail::usage_fields},
}};

// The change kinds, which require a target.
inline constexpr std::array<std::string_view, 4> CHANGE_KINDS = {"check", "accept", "retire", "forget"};

// A kind's place in KINDS, or none outside this minor's 22.
constexpr std::optional<std::size_t> index_of(std::string_view kind) noexcept {
    for (std::size_t i = 0; i < KINDS.size(); ++i) {
        if (KINDS[i].name == kind) {
            return i;
        }
    }
    return std::nullopt;
}
constexpr bool served(std::string_view kind) noexcept {
    const std::optional<std::size_t> i = index_of(kind);
    return i.has_value() && KINDS[*i].served;
}
constexpr bool project_only(std::string_view kind) noexcept {
    const std::optional<std::size_t> i = index_of(kind);
    return i.has_value() && KINDS[*i].group == Group::work;
}
constexpr bool allows(std::string_view kind, Basis basis) noexcept {
    const std::optional<std::size_t> i = index_of(kind);
    return i.has_value() && (KINDS[*i].bases & bit(basis)) != 0;
}
// A check's basis by its method: observed for test, rerun and transcript,
// claimed for review; none off the four, which leaves observed or claimed.
constexpr std::optional<Basis> check_basis(std::string_view method) noexcept {
    if (method == "review") {
        return Basis::claimed;
    }
    if (method == "test" || method == "rerun" || method == "transcript") {
        return Basis::observed;
    }
    return std::nullopt;
}

// How kind k carries common field c, by the common table's rows: none on a
// kind with no content.
constexpr Carried common_carried(const Kind& k, Common c) noexcept {
    if (!k.content) {
        return Carried::refused;
    }
    const bool on_served = k.served;
    switch (c) {
        case Common::subject:
            if (!on_served || k.name == "map") {
                return Carried::refused;
            }
            return k.name == "relation" ? Carried::optional : Carried::required;
        case Common::applies_to:
        case Common::expires:
        case Common::when: return on_served ? Carried::optional : Carried::refused;
        case Common::source: return k.group == Group::usage ? Carried::refused : Carried::optional;
        case Common::evidence:
            if (k.group == Group::usage) {
                return Carried::refused;
            }
            return k.name == "episode" ? Carried::optional : Carried::by_basis;
        case Common::episode: return k.name == "episode" ? Carried::refused : Carried::by_writer;
        case Common::band: return on_served ? Carried::required : Carried::refused;
        case Common::admit: return on_served && k.name != "clause" ? Carried::by_admit : Carried::refused;
        case Common::as_of:
            if (k.name == "account") {
                return Carried::required;
            }
            return k.name == "entity" || k.name == "observation" ? Carried::optional : Carried::refused;
    }
    return Carried::refused;
}

// A rule read for one entry: its writer's role (a slot's, loop or
// import), its basis and, on a pin, its type.
constexpr Carried resolve(Carried c, std::string_view writer, Basis basis, std::string_view pin_type = {}) noexcept {
    const std::string_view role = header::role(writer);
    switch (c) {
        case Carried::by_basis:
            return basis == Basis::claimed || basis == Basis::observed ? Carried::required : Carried::optional;
        case Carried::by_writer: return role == "import" ? Carried::optional : Carried::required;
        case Carried::by_admit: return role == "loop" ? Carried::optional : Carried::required;
        case Carried::by_type: return pin_type == "tool" ? Carried::required : Carried::optional;
        default: return c;
    }
}

// A record's identity in its scope: its kind's place and up to four parts,
// views of the header's and the content's text.
struct Identity {
    std::uint8_t kind = 0;
    std::array<std::string_view, 4> parts{};
    constexpr bool operator==(const Identity&) const = default;
};
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(Identity) == 72, "Identity's size is pinned: its kind, padded, and four views");
#endif

namespace detail {

// A string member of the object at node i, or empty.
template <typename B>
constexpr std::string_view member(const canon::Basic<B>& v, json::Index i, std::string_view key) {
    if (v.empty() || v.kind(i) != json::Kind::object) {
        return {};
    }
    const std::optional<json::Index> m = v.find(i, key);
    return m && v.kind(*m) == json::Kind::string ? v.text(*m) : std::string_view{};
}

}  // namespace detail

template <typename A, typename B>
constexpr std::optional<Identity> identity(const header::Header<A>& h, const canon::Basic<B>& content) {
    const std::optional<std::size_t> k = index_of(h.value(header::Slot::kind));
    if (!k || KINDS[*k].identity == Holds::none) {
        return std::nullopt;
    }
    Identity id;
    id.kind = static_cast<std::uint8_t>(*k);
    switch (KINDS[*k].identity) {
        case Holds::lane: id.parts[0] = h.value(header::Slot::lane); break;
        case Holds::item_id: id.parts[0] = detail::member(content, 0, "item_id"); break;
        case Holds::clause_id: id.parts[0] = detail::member(content, 0, "clause_id"); break;
        case Holds::object: {
            if (content.empty() || content.kind(0) != json::Kind::object) {
                break;
            }
            const std::optional<json::Index> o = content.find(0, "object");
            if (!o) {
                break;
            }
            id.parts[0] = detail::member(content, *o, "type");
            if (id.parts[0] == "entry") {
                id.parts[1] = detail::member(content, *o, "id");
            } else {
                id.parts[1] = detail::member(content, *o, "repository");
                id.parts[2] = detail::member(content, *o, "path");
                id.parts[3] = detail::member(content, *o, "name");
            }
            break;
        }
        case Holds::none: break;
    }
    return id;
}

// The tables agree with header's closed sets.
static_assert([] {
    for (std::size_t i = 0; i < KINDS.size(); ++i) {
        if (KINDS[i].name != header::kind_names[i]) {
            return false;
        }
    }
    return CHANGE_KINDS == header::change_kinds;
}());

}  // namespace qiven3::entry::kinds
