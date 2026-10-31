// write/refuse: a session's file read, built and refused against a fake
// fold (every name, slot, scope, id and text invented). Each refusal code
// of the write command with its cases and its nearest pass beside it: the
// slot (import, owner, loop, a reserved role, an unknown one, an older
// slot); the person scope; an owner's entry retired, checked, accepted,
// revised, closed, or served by a relation as replaced, retired or refuted
// from either side, named by entry: or by the subject it carries; the
// owner's words of each kind; the kinds a session may not write; a
// checkpoint's first refs naming no item; an under cycle. A session revises
// the review's claimed entries freely. The codes' order, the fold's
// queries counted, and no allocation made.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entry/kinds.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "write/build.hpp"
#include "write/entries.hpp"
#include "write/refuse.hpp"

namespace {

namespace wb = qiven3::write::build;
namespace we = qiven3::write::entries;
namespace wr = qiven3::write::refuse;
using qiven3::memory::Arena;
using qiven3::platform::fake::FakePlatform;

constexpr std::size_t mib = std::size_t{1} << 20U;
const std::string home = "project:example";
const std::string stamp = "2026-10-29T07:00:00.000Z";

std::string id_of(char c) { return "sha256:" + std::string(64, c); }

// The fold's ids: an owner's direction and goal, the review's imported
// claim and decision, a session's item and claim.
const std::string direction = id_of('d');
const std::string goal = id_of('9');
const std::string imported = id_of('c');
const std::string imported_decision = id_of('e');
const std::string held_item = id_of('a');
const std::string held_claim = id_of('b');

const std::string admit = R"("admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."})";
const std::string evidence = R"("evidence": [{"type": "entry", "id": ")" + id_of('1') + R"("}])";
std::string served(const std::string& subject) {
    return R"("subject": ")" + subject + R"(", "band": "normal", )" + admit + ", " + evidence;
}
std::string entry(const std::string& kind, const std::string& basis, const std::string& content,
                  const std::string& head = {}) {
    return "{" + head + R"("kind": ")" + kind + R"(", "basis": ")" + basis + R"(", "content": {)" + content + "}}";
}
std::string target(const std::string& id) { return R"("target": ")" + id + R"(", )"; }
std::string named(const std::string& name) { return R"("name": ")" + name + R"(", )"; }
std::string file_of(const std::string& writer, const std::string& entries, const std::string& in = home,
                    const std::string& pieces = {}) {
    const std::string episode =
        writer == "import" ? "" : R"("episode": {"client": "claude-code", "session": "synthetic-session-7"}, )";
    return R"({"form": "qiven3-entries/1", "writer": ")" + writer + R"(", "scope": ")" + in + R"(", )" + episode +
           (pieces.empty() ? "" : R"("pieces": {)" + pieces + "}, ") + R"("entries": [)" + entries + "]}";
}
std::string file_of(const std::string& entries) { return file_of("implementer/s7", entries); }

const std::string claim = entry("claim", "claimed", R"("claim": "An invented claim.", )" + served("entity:example"));
std::string item(const std::string& id, const std::string& under = {}) {
    return entry("item", "claimed",
                 R"("item_id": ")" + id + R"(", "state": "open", )" +
                     (under.empty() ? "" : R"("under": ")" + under + R"(", )") + served("item:" + id),
                 named(id));
}
std::string checkpoint(const std::string& refs) {
    return entry("checkpoint", "claimed",
                 R"("progress": "Built.", "next_action": "Review.", )" + refs + served("item:core"));
}
std::string retire(const std::string& id) {
    return entry("retire", "claimed", R"("reason": "Settled in review.", )" + evidence, target(id));
}
std::string check(const std::string& id) {
    return entry("check", "claimed", R"("result": "refuted", "method": "review", )" + evidence, target(id));
}
std::string relation(const std::string& from, const std::string& slug, const std::string& to) {
    return entry("relation", "claimed",
                 R"("from": ")" + from + R"(", "relation": ")" + slug + R"(", "to": ")" + to +
                     R"(", "band": "normal", )" + admit + ", " + evidence);
}
std::string decision(const std::string& closes) {
    return entry("decision", "claimed",
                 R"("decides": "An invented decision.", "closes": [")" + closes + R"("], )" +
                     served("entity:example"));
}

// A fold of rows the case gives: heads by (scope, lane) with their writer,
// current versions by (scope, kind, key), entries held by (scope, id) with
// their kind, subject and whether an owner's, items' under by item_id; and
// each query counted.
struct Head {
    std::string lane, id, writer;
    std::int64_t seq = 1;
};
struct Current {
    std::string kind, key, id;
};
struct Held {
    std::string id, kind, subject;
    bool owner = false;
};
struct Under {
    std::string item_id, under;
};
struct Counts {
    int writer = 0, kind = 0, owner = 0, owner_subject = 0, under = 0;
};
struct FakeFold {
    std::vector<Head> heads;
    std::vector<Current> currents;
    std::vector<Held> held{{direction, "direction", "entity:owner-rule", true},
                           {goal, "goal", "activity:release", true},
                           {imported, "claim", "entity:example", false},
                           {imported_decision, "decision", "entity:example", false},
                           {held_item, "item", "item:other", false},
                           {held_claim, "claim", "entity:example", false}};
    std::vector<Under> unders;
    Counts counts;

    std::optional<wb::VersionView> head(std::string_view s, std::string_view lane) const {
        for (const Head& h : heads) {
            if (s == home && h.lane == lane) {
                return wb::VersionView{h.id, stamp,h.seq};
            }
        }
        return std::nullopt;
    }
    std::optional<wb::VersionView> current(std::string_view s, const qiven3::entry::kinds::IdentityView& i) const {
        for (const Current& c : currents) {
            if (s == home && qiven3::entry::kinds::index_of(c.kind) == i.kind && c.key == i.parts[0]) {
                return wb::VersionView{c.id, stamp,1};
            }
        }
        return std::nullopt;
    }
    std::optional<wb::VersionView> find(std::string_view s, std::string_view id) const {
        const Held* h = by_id(s, id);
        return h == nullptr ? std::nullopt : std::optional<wb::VersionView>(wb::VersionView{h->id, stamp,1});
    }
    const Held* by_id(std::string_view s, std::string_view id) const {
        for (const Held& h : held) {
            if (s == home && h.id == id) {
                return &h;
            }
        }
        return nullptr;
    }
    std::optional<std::string_view> writer(std::string_view s, std::string_view lane) {
        ++counts.writer;
        for (const Head& h : heads) {
            if (s == home && h.lane == lane) {
                return h.writer;
            }
        }
        return std::nullopt;
    }
    std::optional<std::string_view> kind(std::string_view s, std::string_view id) {
        ++counts.kind;
        const Held* h = by_id(s, id);
        return h == nullptr ? std::nullopt : std::optional<std::string_view>(h->kind);
    }
    bool owner(std::string_view s, std::string_view id) {
        ++counts.owner;
        const Held* h = by_id(s, id);
        return h != nullptr && h->owner;
    }
    bool owner_subject(std::string_view s, std::string_view subject) {
        ++counts.owner_subject;
        for (const Held& h : held) {
            if (s == home && h.owner && h.subject == subject) {
                return true;
            }
        }
        return false;
    }
    std::optional<std::string_view> under(std::string_view s, std::string_view item_id) {
        ++counts.under;
        for (const Under& u : unders) {
            if (s == home && u.item_id == item_id) {
                return u.under.empty() ? std::nullopt : std::optional<std::string_view>(u.under);
            }
        }
        return std::nullopt;
    }
};
static_assert(wb::Fold<FakeFold>);
static_assert(wr::Fold<FakeFold>);

qiven3::platform::fake::Config config() {
    qiven3::platform::fake::Config c;
    c.seed = 7;
    c.clock = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::sys_days{std::chrono::year{2026} / 10 / 29} + std::chrono::hours{8});
    return c;
}

// The outcome of a file read, built and refused: ok, or
// code[/why]@path#entry[:name], entry - for the file's own fields.
std::string run(const std::string& bytes, FakeFold& fold) {
    std::vector<std::byte> pages(16 * mib);
    Arena arena(pages);
    FakePlatform platform(config());
    const auto where = [](std::string_view path, std::uint32_t e, std::string_view name) {
        return "@" + std::string(path) + "#" + (e == we::none ? std::string("-") : std::to_string(e)) +
               (name.empty() ? std::string() : ":" + std::string(name));
    };
    try {
        const we::File f = we::read(bytes, arena);
        qiven3::memory::Vector<wb::Built> out{qiven3::memory::Allocator<wb::Built>(arena)};
        wb::build(f, fold, platform, arena, out);
        wr::write(f, std::span<const wb::Built>(out.data(), out.size()), fold);
        return "ok";
    } catch (const we::EntriesError& e) {
        return std::string(we::name(e.code)) + "/" + std::string(we::name(e.why)) +
               where(e.path.view(), e.entry, e.name.view());
    } catch (const wr::Refusal& r) {
        return std::string(wr::name(r.code)) + where(r.path.view(), r.entry, r.name.view());
    }
}
std::string run(const std::string& bytes) {
    FakeFold fold;
    return run(bytes, fold);
}

}  // namespace

QIVEN_CASE(write_refuse, passes_a_session_file_and_counts_the_queries) {
    FakeFold fold;
    fold.currents.push_back(Current{"question", "unit-one-shape", id_of('3')});
    const std::string entries = item("core") + ", " + checkpoint(R"("refs": ["@name:core"], )") + ", " +
                                entry("retire", "claimed", R"("reason": "Settled.", )" + evidence,
                                      target("@question:unit-one-shape"));
    CHECK_EQ(run(file_of(entries), fold), std::string("ok"));
    // writer once; owner once, on the retire's target; nothing else.
    CHECK_EQ(fold.counts.writer, 1);
    CHECK_EQ(fold.counts.owner, 1);
    CHECK_EQ(fold.counts.kind + fold.counts.owner_subject + fold.counts.under, 0);
    // A file of no fold query past the slot: a claim alone.
    FakeFold quiet;
    CHECK_EQ(run(file_of(claim), quiet), std::string("ok"));
    CHECK_EQ(quiet.counts.writer + quiet.counts.owner + quiet.counts.kind + quiet.counts.owner_subject +
                 quiet.counts.under,
             1);
}

QIVEN_CASE(write_refuse, revises_the_reviews_claimed_entries) {
    // The review's imported claim and decision, basis claimed: a session
    // revises, retires, checks, closes and replaces them by a relation.
    const std::string revised =
        entry("claim", "claimed", R"("claim": "A revised claim.", )" + served("entity:example"), target(imported));
    CHECK_EQ(run(file_of(revised)), std::string("ok"));
    CHECK_EQ(run(file_of(retire(imported))), std::string("ok"));
    CHECK_EQ(run(file_of(check(imported_decision))), std::string("ok"));
    CHECK_EQ(run(file_of(decision(imported_decision))), std::string("ok"));
    CHECK_EQ(run(file_of(relation("entry:" + imported, "replaced-by", "entity:example"))), std::string("ok"));
    CHECK_EQ(run(file_of(relation("entity:other", "replaces", "entry:" + imported_decision))), std::string("ok"));
}

QIVEN_CASE(write_refuse, refuses_each_slot) {
    CHECK_EQ(run(file_of("import", claim)), std::string("slot@writer#-"));
    CHECK_EQ(run(file_of("owner/s1", claim)), std::string("slot@writer#-"));
    CHECK_EQ(run(file_of("loop", claim)), std::string("slot@writer#-"));
    CHECK_EQ(run(file_of("loop/s2", claim)), std::string("slot@writer#-"));      // reserved
    CHECK_EQ(run(file_of("import/s2", claim)), std::string("slot@writer#-"));    // reserved
    // A reflection's claim holds a transcript among its evidence.
    const std::string said = R"("said": {"text": "The session said so."})";
    const std::string reflected =
        entry("claim", "claimed",
              R"("claim": "An invented claim.", "subject": "entity:example", "band": "normal", )" + admit +
                  R"(, "evidence": [{"type": "transcript", "client": "claude-code", "session": "s-1",)"
                  R"( "message": "m-1", "piece": "said"}])");
    CHECK_EQ(run(file_of("reflect/s3", reflected, home, said)), std::string("slot@writer#-"));  // not the work line's
    CHECK_EQ(run(file_of("implementer/s7", reflected, home, said)), std::string("ok"));
    CHECK_EQ(run(file_of("designer/s3", claim)), std::string("slot@writer#-"));  // unknown
    CHECK_EQ(run(file_of("reviewer/s2", claim)), std::string("ok"));
    // An older slot than the writer of its lane's head; the same or a newer
    // one passes, and another lane's head is not its.
    FakeFold fold;
    fold.heads.push_back(Head{"implementer", id_of('5'), "implementer/s9"});
    CHECK_EQ(run(file_of("implementer/s7", claim), fold), std::string("slot@writer#-"));
    CHECK_EQ(run(file_of("implementer/s9", claim), fold), std::string("ok"));
    CHECK_EQ(run(file_of("implementer/s10", claim), fold), std::string("ok"));
    CHECK_EQ(run(file_of("reviewer/s2", claim), fold), std::string("ok"));
    // The bare owner is off the writer's syntax: the reader's form.
    CHECK_EQ(run(file_of("owner", claim)), std::string("form/syntax@writer#-"));
}

QIVEN_CASE(write_refuse, refuses_the_person_scope) {
    CHECK_EQ(run(file_of("implementer/s7", claim, "person")), std::string("person_scope@scope#-"));
    CHECK_EQ(run(file_of("implementer/s7", claim, "life")), std::string("person_scope@scope#-"));
    CHECK_EQ(run(file_of("implementer/s7", claim, home)), std::string("ok"));
    // A work kind there: the content check's scope fault comes first.
    CHECK_EQ(run(file_of("implementer/s7", item("core"), "person")),
             std::string("invalid/content@entries[0].content#0:core"));
    // The slot before the scope.
    CHECK_EQ(run(file_of("loop", claim, "person")), std::string("slot@writer#-"));
}

QIVEN_CASE(write_refuse, refuses_acts_on_an_owners_entry) {
    CHECK_EQ(run(file_of(retire(direction))), std::string("owner_entry@entries[0].target#0"));
    CHECK_EQ(run(file_of(check(direction))), std::string("owner_entry@entries[0].target#0"));
    CHECK_EQ(run(file_of(retire(goal))), std::string("owner_entry@entries[0].target#0"));
    const std::string revision =
        entry("claim", "claimed", R"("claim": "A revised claim.", )" + served("entity:example"), target(goal));
    CHECK_EQ(run(file_of(revision)), std::string("owner_entry@entries[0].target#0"));
    CHECK_EQ(run(file_of(claim + ", " + decision(direction))),
             std::string("owner_entry@entries[1].content.closes[0]#1"));
    // An accept: owner_entry named before its basis's owner_words.
    const std::string message = R"("message": {"type": "transcript", "client": "claude-code", "session": "s-1",)"
                                R"( "message": "m-1", "piece": "said"})";
    const auto accept = [&](const std::string& id) {
        return file_of("implementer/s7", entry("accept", "owner-words", message, target(id)), home,
                       R"("said": {"text": "The owner said so."})");
    };
    CHECK_EQ(run(accept(direction)), std::string("owner_entry@entries[0].target#0"));
    CHECK_EQ(run(accept(held_claim)), std::string("owner_words@entries[0].basis#0"));
    // The nearest passes: the same acts on a session's claim.
    CHECK_EQ(run(file_of(retire(held_claim))), std::string("ok"));
    CHECK_EQ(run(file_of(check(held_claim))), std::string("ok"));
    CHECK_EQ(run(file_of(decision(held_claim))), std::string("ok"));
}

QIVEN_CASE(write_refuse, refuses_a_relation_serving_an_owners_entry) {
    const std::string owner = "entry:" + direction;
    for (const char* slug : {"replaced-by", "superseded-by", "retired-by", "refuted-by"}) {
        CHECK_EQ(run(file_of(relation(owner, slug, "entity:example"))),
                 std::string("owner_entry@entries[0].content.from#0"));
        CHECK_EQ(run(file_of(relation("entity:example", slug, owner))), std::string("ok"));  // it serves the other
    }
    for (const char* slug : {"replaces", "supersedes", "retires", "refutes"}) {
        CHECK_EQ(run(file_of(relation("entity:example", slug, owner))),
                 std::string("owner_entry@entries[0].content.to#0"));
        CHECK_EQ(run(file_of(relation(owner, slug, "entity:example"))), std::string("ok"));
    }
    // Named by the subject an owner's entry carries, the direction's and
    // the goal's; by entry: of the goal.
    CHECK_EQ(run(file_of(relation("entity:owner-rule", "replaced-by", "entity:example"))),
             std::string("owner_entry@entries[0].content.from#0"));
    CHECK_EQ(run(file_of(relation("entity:example", "refutes", "activity:release"))),
             std::string("owner_entry@entries[0].content.to#0"));
    CHECK_EQ(run(file_of(relation("entry:" + goal, "retired-by", "entity:example"))),
             std::string("owner_entry@entries[0].content.from#0"));
    // A relation that serves it as nothing of the three passes.
    CHECK_EQ(run(file_of(relation(owner, "depends-on", "entity:example"))), std::string("ok"));
    CHECK_EQ(run(file_of(relation("entity:example", "cites", owner))), std::string("ok"));
}

QIVEN_CASE(write_refuse, refuses_the_owners_words) {
    const std::string words = R"("words": "Keep the tests pinned.", "subject": "entity:owner", "band": "core", )";
    CHECK_EQ(run(file_of(entry("direction", "owner-words", words + admit))),
             std::string("owner_words@entries[0].basis#0"));
    CHECK_EQ(run(file_of(entry("account", "owner-words", words + R"("as_of": "2026-10-29", )" + admit))),
             std::string("owner_words@entries[0].basis#0"));
    // A forget: the content check refuses the slot's episode in a content
    // its kind does not have, first.
    CHECK_EQ(run(file_of(claim + ", " + entry("forget", "owner-words", "", target(held_claim)))),
             std::string("invalid/content@entries[1].content.episode#1"));
    // Its nearest pass: the same words, basis claimed, as a claim.
    CHECK_EQ(run(file_of(entry("claim", "claimed", R"("claim": "Keep the tests pinned.", )" + served("entity:owner")))),
             std::string("ok"));
}

QIVEN_CASE(write_refuse, refuses_the_kinds_of_import_or_none) {
    const std::string kind = "kind@entries[0].kind#0";
    CHECK_EQ(run(file_of(entry("goal", "claimed", R"("objective": "Ship one unit.", )" + served("activity:ship")))),
             kind);
    const std::string source = R"("source": {"text": "An invented clause."})";
    CHECK_EQ(run(file_of("implementer/s7",
                         entry("clause", "derived",
                               R"("authority": "law", "band": "core", "clause_id": "ENG-110",)"
                               R"( "subject": "clause:ENG-110", "text": "An invented clause.", "source": [{"type": "repo", "repository": "example",)"
                               R"( "path": "rules/clauses.txt", "piece": "source"}])"),
                         home, source)),
             kind);
    const std::string in_repo = R"({"piece": "source", "path": "src/a.py", "repository": "example", "type": "repo"})";
    CHECK_EQ(run(file_of("implementer/s7",
                         entry("map", "derived",
                               admit + R"(, "band": "high", "contract": "c", "does": "d", "object": )" + in_repo +
                                   R"(, "object_type": "file")"),
                         home, source)),
             kind);
    CHECK_EQ(run(file_of(entry("module", "claimed",
                               R"("responsibility": "An invented module.", "paths": ["src/a.py"], )" +
                                   served("module:a")))),
             kind);
    CHECK_EQ(run(file_of(entry("usage", "observed", R"("served": [")" + held_claim + R"("])"))), kind);
    // An episode and a checkpoint with for_lane: the content check refuses
    // a slot's episode on the one and for_lane on the other first.
    CHECK_EQ(run(file_of(entry("episode", "observed",
                               R"("client": "claude-code", "session": "s-1", "started": ")" + stamp + R"(")"))),
             std::string("invalid/content@entries[0].content.episode#0"));
    CHECK_EQ(run(file_of(checkpoint(R"("refs": [")" + held_item + R"("], "for_lane": "reviewer", )"))),
             std::string("invalid/content@entries[0].content.for_lane#0"));
    // The nearest pass: a claim, a session's own kind.
    CHECK_EQ(run(file_of(claim)), std::string("ok"));
}

QIVEN_CASE(write_refuse, refuses_a_checkpoint_naming_no_item) {
    FakeFold fold;
    fold.currents.push_back(Current{"item", "core", id_of('4')});
    fold.currents.push_back(Current{"checkpoint", "implementer", id_of('5')});
    fold.currents.push_back(Current{"question", "unit-one-shape", id_of('6')});
    fold.held.push_back(Held{id_of('4'), "item", "item:core", false});
    const auto with = [&](const std::string& refs) {
        return run(file_of(claim + ", " + item("core") + ", " + checkpoint(refs)), fold);
    };
    CHECK_EQ(with(R"("refs": ["@name:core"], )"), std::string("ok"));
    CHECK_EQ(with(R"("refs": ["@item:core"], )"), std::string("ok"));
    CHECK_EQ(with(R"("refs": [")" + held_item + R"(", ")" + held_claim + R"("], )"), std::string("ok"));
    const std::string refused = "refs_item@entries[2].content.refs[0]#2";
    CHECK_EQ(with(R"("refs": ["@lane:implementer"], )"), refused);
    CHECK_EQ(with(R"("refs": ["@question:unit-one-shape"], )"), refused);
    CHECK_EQ(with(R"("refs": [")" + held_claim + R"(", ")" + held_item + R"("], )"), refused);  // the first
    CHECK_EQ(with(R"("refs": [")" + id_of('7') + R"("], )"), refused);                          // none held
    const std::string said =
        entry("claim", "claimed", R"("claim": "An invented claim.", )" + served("entity:example"), named("said"));
    CHECK_EQ(run(file_of(said + ", " + checkpoint(R"("refs": ["@name:said"], )")), fold),
             std::string("refs_item@entries[1].content.refs[0]#1"));
    CHECK_EQ(with(""), std::string("refs_item@entries[2].content.refs#2"));
}

QIVEN_CASE(write_refuse, refuses_an_under_cycle) {
    FakeFold fold;
    CHECK_EQ(run(file_of(item("core", "core")), fold), std::string("cycle@entries[0].content.under#0:core"));
    fold.unders = {{"b", "core"}};
    CHECK_EQ(run(file_of(item("core", "b")), fold), std::string("cycle@entries[0].content.under#0:core"));
    fold.unders = {{"b", "c"}, {"c", "d"}, {"d", "core"}};
    CHECK_EQ(run(file_of(item("core", "b")), fold), std::string("cycle@entries[0].content.under#0:core"));
    // Within the file: the file's b over the fold's.
    fold.unders = {{"b", "x"}};
    CHECK_EQ(run(file_of(item("core", "b") + ", " + item("b", "core")), fold),
             std::string("cycle@entries[0].content.under#0:core"));
    // The nearest passes: a chain ending; a loop the item is not in, the
    // walk ending at it; the file's last version of core with no under.
    fold.unders = {{"b", "c"}, {"c", "d"}};
    CHECK_EQ(run(file_of(item("core", "b")), fold), std::string("ok"));
    fold.unders = {{"x", "y"}, {"y", "z"}, {"z", "x"}};
    CHECK_EQ(run(file_of(item("core", "x")), fold), std::string("ok"));
    fold.unders = {{"b", "core"}};
    const std::string later = entry("item", "claimed", R"("item_id": "core", "state": "done", )" + served("item:core"));
    CHECK_EQ(run(file_of(item("core", "b") + ", " + later), fold), std::string("ok"));
}

QIVEN_CASE(write_refuse, allocates_nothing) {
#if defined(QIVEN_FAULTS)
    // Read and built first; then the countdown on the arena, each of its
    // allocations failed in turn over a pass and a refusal: none is made.
    FakePlatform platform(config());
    qiven3::memory::Countdown countdown;
    for (const std::string& bytes : {file_of(claim + ", " + item("core")), file_of(retire(direction))}) {
        std::vector<std::byte> pages(16 * mib);
        Arena arena(pages);
        FakeFold fold;
        const we::File f = we::read(bytes, arena);
        qiven3::memory::Vector<wb::Built> out{qiven3::memory::Allocator<wb::Built>(arena)};
        wb::build(f, fold, platform, arena, out);
        std::optional<wr::Code> refused;
        const auto op = [&] {
            refused.reset();
            try {
                wr::write(f, std::span<const wb::Built>(out.data(), out.size()), fold);
            } catch (const wr::Refusal& r) {
                refused = r.code;
            }
        };
        arena.attach(&countdown);
        const qiven3::test::FaultRuns runs = qiven3::test::run_faults<qiven3::memory::MemoryError>(
            qiven_ctx.log, "write_refuse.write", countdown, op, [](std::size_t) { return true; });
        arena.attach(nullptr);
        CHECK_EQ(runs.typed, 0U);
        CHECK_EQ(runs.completed_at, 1U);
        CHECK_EQ(runs.untyped + runs.unchecked, 0U);
        CHECK_EQ(refused.has_value(), bytes.find("retire") != std::string::npos);
    }
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}

QIVEN_CASE(write_refuse, names_the_first_code_in_its_order) {
    // The slot and the scope before any entry; within an entry owner_entry,
    // owner_words, kind, refs_item, cycle; an earlier entry first.
    const std::string words = R"("words": "w", "subject": "entity:owner", "band": "core", )" + admit;
    CHECK_EQ(run(file_of("loop", entry("direction", "owner-words", words), "person")), std::string("slot@writer#-"));
    CHECK_EQ(run(file_of("implementer/s7", entry("direction", "owner-words", words), "person")),
             std::string("person_scope@scope#-"));
    CHECK_EQ(run(file_of(entry("direction", "owner-words", words) + ", " + retire(direction))),
             std::string("owner_words@entries[0].basis#0"));
    CHECK_EQ(run(file_of(retire(direction) + ", " + entry("direction", "owner-words", words))),
             std::string("owner_entry@entries[0].target#0"));
}
