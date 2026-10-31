// write/build: the entries of a file read built against a fold (every
// name, slot, scope, id and text invented). A write file on an empty fold:
// its lane the slot's role, a chain from seq 1, @name: the earlier entry's
// id, each entry's own key, a text piece's digest the HMAC under that key,
// each body record checked against its header. Against a fold: the head's
// next seq and parent, an item's revision of the fold's current version,
// at raised past the target's, @item: the file's earlier entry before the
// fold's, @question: the fold's. An import file: lane import, a chain in
// each scope. A path piece read and digested; one unread or past 4 MiB, a
// placeholder or target that resolves to nothing, each refused by its path.
// A forget, with no content digest and no body; a clause's and a map's
// revisions by their identities, the file's own first, a map's object no
// place for a placeholder; a question without question_id a root; expires
// checked against the built at; an import checkpoint by its for_lane.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "entry/body.hpp"
#include "entry/header.hpp"
#include "entry/kinds.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "write/build.hpp"
#include "write/entries.hpp"

namespace {

namespace wb = qiven3::write::build;
namespace we = qiven3::write::entries;
namespace fs = std::filesystem;
using qiven3::memory::Arena;
using qiven3::platform::fake::FakePlatform;

constexpr std::size_t mib = std::size_t{1} << 20U;

const std::string write_file = R"json({
  "form": "qiven3-entries/1",
  "writer": "implementer/s7",
  "scope": "project:example",
  "episode": {"client": "claude-code", "session": "synthetic-session-7"},
  "pieces": {"lane-record": {"text": "progress: unit one built\n"}},
  "entries": [
    {"name": "moved", "kind": "item", "basis": "claimed",
     "content": {"item_id": "core", "state": "active", "turn": "reviewer",
       "subject": "item:core", "band": "normal",
       "admit": {"underivable": "Whose turn it is lives in no file v3 reads.",
                 "mistake": "The next session takes a turn that is not its own."},
       "evidence": [{"type": "output", "command": "v2 transition core executed",
                     "piece": "lane-record"}]}},
    {"name": "lane", "kind": "checkpoint", "basis": "claimed",
     "content": {"progress": "Unit one built.", "next_action": "The reviewer reviews unit one.",
       "refs": ["@name:moved"], "subject": "item:core", "band": "high",
       "admit": {"underivable": "The lane's state as its session left it.",
                 "mistake": "A cold start repeats or skips the turn's work."},
       "evidence": [{"type": "output", "command": "v2 checkpoint implementer",
                     "piece": "lane-record"}]}},
    {"kind": "retire", "basis": "claimed", "target": "@question:unit-one-shape",
     "content": {"reason": "Settled: the reviewer chose the shape in review.",
       "evidence": [{"type": "entry", "id": "@name:moved"}]}}
  ]
})json";

const std::string import_file = R"json({
  "form": "qiven3-entries/1",
  "writer": "import",
  "scope": "project:example",
  "entries": [
    {"kind": "claim", "basis": "claimed",
     "content": {"claim": "An invented claim.", "subject": "entity:example", "band": "normal",
       "admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."},
       "evidence": [{"type": "entry", "id": "sha256:1111111111111111111111111111111111111111111111111111111111111111"}]}},
    {"scope": "project:other", "kind": "claim", "basis": "claimed",
     "content": {"claim": "Another invented claim.", "subject": "entity:example", "band": "low",
       "admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."},
       "evidence": [{"type": "entry", "id": "sha256:1111111111111111111111111111111111111111111111111111111111111111"}]}}
  ]
})json";

std::string swapped(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    return at == std::string::npos ? "missing: " + std::string(from) : text.replace(at, from.size(), to);
}
std::string with(std::string_view from, std::string_view to) { return swapped(write_file, from, to); }

std::string id_of(char c) { return "sha256:" + std::string(64, c); }

// A fold of rows the case gives: heads by (scope, lane), current versions
// by (scope, kind, first part, or first/second where the key holds a
// slash), entries held by (scope, id).
struct Row {
    std::string scope, key, id, at;
    std::int64_t seq = 0;
    std::string kind;
};
bool keyed_by(const std::string& key, const qiven3::entry::kinds::IdentityView& i) {
    return key.find('/') == std::string::npos ? key == i.parts[0]
                                              : key == std::string(i.parts[0]) + "/" + std::string(i.parts[1]);
}
struct FakeFold {
    std::vector<Row> heads, currents, held;
    std::optional<wb::VersionView> head(std::string_view scope, std::string_view lane) const {
        for (const Row& r : heads) {
            if (r.scope == scope && r.key == lane) {
                return wb::VersionView{r.id, r.at, r.seq};
            }
        }
        return std::nullopt;
    }
    std::optional<wb::VersionView> current(std::string_view scope, const qiven3::entry::kinds::IdentityView& i) const {
        for (const Row& r : currents) {
            if (r.scope == scope && qiven3::entry::kinds::index_of(r.kind) == i.kind && keyed_by(r.key, i)) {
                return wb::VersionView{r.id, r.at, r.seq};
            }
        }
        return std::nullopt;
    }
    std::optional<wb::VersionView> find(std::string_view scope, std::string_view id) const {
        for (const Row& r : held) {
            if (r.scope == scope && r.id == id) {
                return wb::VersionView{r.id, r.at, r.seq};
            }
        }
        return std::nullopt;
    }
};
static_assert(wb::Fold<FakeFold>);

qiven3::platform::fake::Config config(const std::string& root = {}) {
    qiven3::platform::fake::Config c;
    c.root = root;
    c.seed = 7;
    c.clock = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::sys_days{std::chrono::year{2026} / 10 / 29} + std::chrono::hours{8});
    return c;
}

// A built entry's texts, copied out of the arena.
struct Out {
    std::string line, record, id, at;
    std::int64_t seq = 0;
    bool derived = false;
};

// The outcome of building bytes: the entries, or code/why@path#index:name.
struct Result {
    std::vector<Out> built;
    std::string refused;
};
Result build(std::string_view bytes, FakeFold& fold, FakePlatform& platform, std::size_t arena_bytes = 16 * mib) {
    std::vector<std::byte> pages(arena_bytes);
    Arena arena(pages);
    Result r;
    try {
        const we::File f = we::read(bytes, arena);
        qiven3::memory::Vector<wb::Built> out{qiven3::memory::Allocator<wb::Built>(arena)};
        wb::build(f, fold, platform, arena, out);
        for (const wb::Built& b : out) {
            r.built.push_back(Out{std::string(b.line), std::string(b.record), std::string(b.id.view()),
                                  std::string(b.at.view()), b.seq, b.derived});
        }
    } catch (const we::EntriesError& e) {
        r.refused = std::string(we::name(e.code)) + "/" + std::string(we::name(e.why)) + "@" +
                    std::string(e.path.view()) + "#" + std::to_string(e.entry) + ":" + std::string(e.name.view());
    }
    return r;
}

bool has(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

// The body record holds against its header line: id and content digest.
bool holds(FakePlatform& platform, const Out& o) {
    try {
        const auto h = qiven3::entry::header::parse_line(o.line);
        qiven3::entry::header::check(h);
        const auto b = qiven3::entry::body::parse(platform, o.record);
        qiven3::entry::body::check(b, h);
        return h.value(qiven3::entry::header::Slot::id) == o.id;
    } catch (...) {
        return false;
    }
}

// The record's key, and the HMAC under it over data, as hmac-sha256:hex.
std::string key_of(const Out& o) {
    const std::size_t at = o.record.find("\"key\":\"");
    return at == std::string::npos ? std::string() : o.record.substr(at + 7, 64);
}
std::string keyed(const std::string& hex, std::string_view data) {
    std::string raw;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        raw.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    const qiven3::digest::Bytes d = qiven3::digest::hmac(raw, data);
    std::string out = "hmac-sha256:";
    constexpr std::string_view digits = "0123456789abcdef";
    for (const std::uint8_t b : d) {
        out.push_back(digits[b >> 4U]);
        out.push_back(digits[b & 15U]);
    }
    return out;
}

// A scratch directory of its own, removed at the scope's end.
struct Scratch {
    Scratch() {
        std::vector<std::random_device> device(1);
        root = fs::temp_directory_path() / ("qiven3-build-" + std::to_string(device.front()()));
        fs::create_directories(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    [[nodiscard]] std::string put(const std::string& name, const std::string& bytes) const {
        const fs::path p = root / name;
        std::ofstream(p, std::ios::binary) << bytes;
        return p.generic_string();
    }
    fs::path root;
};

}  // namespace

QIVEN3_CASE(write_build, builds_a_write_file_on_an_empty_fold) {
    FakeFold fold;
    fold.currents.push_back(Row{"project:example", "unit-one-shape", id_of('3'), "2026-10-29T07:00:00.000Z", 1, "question"});
    FakePlatform platform(config());
    const Result r = build(write_file, fold, platform);
    REQUIRE(r.refused.empty());
    REQUIRE(r.built.size() == 3);
    const Out& item = r.built[0];
    CHECK(has(item.line, "\"lane\":\"implementer\""));
    CHECK(has(item.line, "\"writer\":\"implementer/s7\""));
    CHECK(has(item.line, "\"tag\":\"qiven-entry/1\""));
    CHECK(has(item.line, "\"seq\":1"));
    CHECK(!has(item.line, "\"parent\""));
    CHECK(!has(item.line, "\"target\""));  // a root: the fold holds no item core
    CHECK(!item.derived);
    CHECK(item.at == "2026-10-29T08:00:00.000Z");
    CHECK(has(r.built[1].line, "\"parent\":\"" + item.id + "\""));
    CHECK(r.built[1].seq == 2);
    CHECK(has(r.built[1].record, "\"refs\":[\"" + item.id + "\"]"));     // @name:
    CHECK(has(r.built[2].line, "\"target\":\"" + id_of('3') + "\""));   // @question:
    CHECK(has(r.built[2].record, "\"id\":\"" + item.id + "\",\"type\":\"entry\""));
    CHECK(has(item.record, "\"episode\":{\"client\":\"claude-code\""));
    CHECK(key_of(item) != key_of(r.built[1]));
    CHECK(has(item.record, keyed(key_of(item), "progress: unit one built\n")));
    CHECK(has(r.built[1].record, keyed(key_of(r.built[1]), "progress: unit one built\n")));
    CHECK(!has(item.record, "\"piece\""));
    for (const Out& o : r.built) {
        CHECK(holds(platform, o));
    }
}

QIVEN3_CASE(write_build, revises_against_the_fold) {
    FakeFold fold;
    const std::string scope = "project:example";
    fold.heads.push_back(Row{scope, "implementer", id_of('a'), "2026-10-29T08:30:00.000Z", 4, ""});
    fold.currents.push_back(Row{scope, "core", id_of('b'), "2026-10-29T09:00:00.000Z", 2, "item"});
    fold.currents.push_back(Row{scope, "implementer", id_of('c'), "2026-10-29T07:00:00.000Z", 3, "checkpoint"});
    fold.currents.push_back(Row{scope, "unit-one-shape", id_of('d'), "2026-10-29T10:00:00.000Z", 1, "question"});
    fold.currents.push_back(Row{"project:other", "core", id_of('e'), "2026-10-29T07:00:00.000Z", 1, "item"});
    FakePlatform platform(config());
    const Result r = build(with("\"refs\": [\"@name:moved\"]", "\"refs\": [\"@item:core\"]"), fold, platform);
    REQUIRE(r.refused.empty());
    REQUIRE(r.built.size() == 3);
    CHECK(has(r.built[0].line, "\"parent\":\"" + id_of('a') + "\""));
    CHECK(r.built[0].seq == 5);
    CHECK(has(r.built[0].line, "\"target\":\"" + id_of('b') + "\""));
    CHECK(r.built[0].derived);
    CHECK(r.built[0].at == "2026-10-29T09:00:00.001Z");  // 1 ms past the target's
    CHECK(has(r.built[1].line, "\"target\":\"" + id_of('c') + "\""));
    CHECK(r.built[1].at == r.built[0].at);  // raised to the parent's
    CHECK(has(r.built[1].record, "\"refs\":[\"" + r.built[0].id + "\"]"));  // the file's own first
    CHECK(has(r.built[2].line, "\"target\":\"" + id_of('d') + "\""));
    CHECK(r.built[2].at == "2026-10-29T10:00:00.001Z");
    CHECK(r.built[2].seq == 7);
    for (const Out& o : r.built) {
        CHECK(holds(platform, o));
    }
}

QIVEN3_CASE(write_build, builds_an_import_file_a_chain_per_scope) {
    FakeFold fold;
    FakePlatform platform(config());
    const Result r = build(import_file, fold, platform);
    REQUIRE(r.refused.empty());
    REQUIRE(r.built.size() == 2);
    for (const Out& o : r.built) {
        CHECK(has(o.line, "\"lane\":\"import\""));
        CHECK(has(o.line, "\"writer\":\"import\""));
        CHECK(o.seq == 1);
        CHECK(!has(o.line, "\"parent\""));
        CHECK(!has(o.record, "\"episode\""));
        CHECK(holds(platform, o));
    }
    CHECK(has(r.built[1].line, "\"scope\":\"project:other\""));
}

QIVEN3_CASE(write_build, refuses_what_resolves_to_nothing) {
    FakeFold fold;
    FakePlatform platform(config());
    CHECK(build(write_file, fold, platform).refused == "form/placeholder@entries[2].target#2:");
    fold.currents.push_back(Row{"project:other", "unit-one-shape", id_of('3'), "2026-10-29T07:00:00.000Z", 1, "question"});
    CHECK(build(write_file, fold, platform).refused == "form/placeholder@entries[2].target#2:");  // another scope
    fold.currents.push_back(Row{"project:example", "unit-one-shape", id_of('3'), "2026-10-29T07:00:00.000Z", 1, "question"});
    CHECK(build(write_file, fold, platform).refused.empty());
    CHECK(build(with("\"refs\": [\"@name:moved\"]", "\"refs\": [\"@item:absent\"]"), fold, platform).refused ==
          "form/placeholder@entries[1].content.refs[0]#1:lane");
    CHECK(build(with("\"refs\": [\"@name:moved\"]", "\"refs\": [\"@lane:reviewer\"]"), fold, platform).refused ==
          "form/placeholder@entries[1].content.refs[0]#1:lane");
    const std::string literal = with("@question:unit-one-shape", id_of('4'));
    CHECK(build(literal, fold, platform).refused == "form/placeholder@entries[2].target#2:");
    fold.held.push_back(Row{"project:example", "", id_of('4'), "2026-10-29T07:00:00.000Z", 1, "item"});
    const Result r = build(literal, fold, platform);
    REQUIRE(r.refused.empty());
    CHECK(has(r.built[2].line, "\"target\":\"" + id_of('4') + "\""));
}

QIVEN3_CASE(write_build, reads_and_digests_a_path_piece) {
    const Scratch scratch;
    const std::string bytes = "line one\r\nline two\n";
    const std::string path = scratch.put("piece.txt", bytes);
    const std::string big = scratch.put("big.txt", std::string(we::max_piece_bytes + 1, 'x'));
    const std::string edge = scratch.put("edge.txt", std::string(we::max_piece_bytes, 'y'));
    FakeFold fold;
    fold.currents.push_back(Row{"project:example", "unit-one-shape", id_of('3'), "2026-10-29T07:00:00.000Z", 1, "question"});
    qiven3::platform::fake::Config c = config(scratch.root.generic_string());
    const std::string unread = (scratch.root / "absent.txt").generic_string();
    c.read_errors[unread] = "synthetic";
    FakePlatform platform(c);
    const auto file = [](const std::string& p) {
        return with("{\"text\": \"progress: unit one built\\n\"}", "{\"path\": \"" + p + "\"}");
    };
    const Result r = build(file(path), fold, platform);
    REQUIRE(r.refused.empty());
    CHECK(has(r.built[0].record, keyed(key_of(r.built[0]), bytes)));
    CHECK(build(file(edge), fold, platform, 32 * mib).refused.empty());
    CHECK(build(file(big), fold, platform, 32 * mib).refused ==
          "form/piece@entries[0].content.evidence[0].piece#0:moved");
    CHECK(build(file(unread), fold, platform).refused == "form/piece@entries[0].content.evidence[0].piece#0:moved");
}

namespace {

const std::string admit = R"("admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."})";
const std::string evidence = R"("evidence": [{"type": "entry", "id": "@item:core"}])";

// A file of form, writer and scope, its entries' text given, a piece if one
// refers to it.
std::string file_of(std::string_view writer, const std::string& entries, bool piece = false) {
    const std::string episode = writer == "import"
                                    ? std::string()
                                    : R"("episode": {"client": "claude-code", "session": "synthetic-session-7"}, )";
    return R"({"form": "qiven3-entries/1", "writer": ")" + std::string(writer) +
           R"(", "scope": "project:example", )" + episode +
           (piece ? R"("pieces": {"source": {"text": "An invented source.\n"}}, )" : "") + R"("entries": [)" +
           entries + "]}";
}

const std::string claim = R"({"kind": "claim", "basis": "claimed", "content": {"claim": "An invented claim.",
    "subject": "entity:example", "band": "normal", )" + admit + ", " + evidence + "}}";

}  // namespace

QIVEN3_CASE(write_build, builds_a_forget_with_no_body) {
    FakeFold fold;
    fold.held.push_back(Row{"project:example", "", id_of('4'), "2026-10-29T09:00:00.000Z", 1, "claim"});
    FakePlatform platform(config());
    const std::string forget =
        R"({"kind": "forget", "basis": "owner-words", "target": ")" + id_of('4') + R"(", "content": {}})";
    const Result r = build(file_of("import", forget), fold, platform);
    REQUIRE(r.refused.empty());
    REQUIRE(r.built.size() == 1);
    const Out& f = r.built[0];
    CHECK(f.record.empty());
    CHECK(!has(f.line, "\"content\""));
    CHECK(has(f.line, "\"target\":\"" + id_of('4') + "\""));
    CHECK(f.at == "2026-10-29T09:00:00.001Z");
    const auto h = qiven3::entry::header::parse_line(f.line);
    qiven3::entry::header::check(h);
    CHECK(h.value(qiven3::entry::header::Slot::id) == f.id);
}

QIVEN3_CASE(write_build, revises_a_clause_and_a_map_by_their_identities) {
    FakeFold fold;
    const std::string scope = "project:example";
    fold.currents.push_back(Row{scope, "ENG-110", id_of('b'), "2026-10-29T07:00:00.000Z", 1, "clause"});
    fold.currents.push_back(Row{scope, "repo/example", id_of('c'), "2026-10-29T07:00:00.000Z", 1, "map"});
    fold.currents.push_back(Row{scope, "repo/other", id_of('e'), "2026-10-29T07:00:00.000Z", 1, "map"});
    fold.currents.push_back(Row{scope, "core", id_of('f'), "2026-10-29T07:00:00.000Z", 1, "item"});
    const auto map = [&](const std::string& object) {
        return R"({"kind": "map", "basis": "derived", "content": {)" + admit +
               R"(, "band": "high", "contract": "c", "does": "d", "object": )" + object +
               R"(, "object_type": "file"}})";
    };
    const std::string digest = R"("digest": "hmac-sha256:)" + std::string(64, '0') + R"(", )";
    const std::string repo = R"({"piece": "source", "path": "src/a.py", "repository": "example", "type": "repo"})";
    const std::string& in_repo = repo;  // a map's object a reference: its digest the piece's
    const std::string root = "{" + digest + R"("id": "@name:root", "type": "entry"})";
    const std::string entries =
        R"({"kind": "clause", "basis": "derived", "content": {"authority": "law", "band": "core",
            "clause_id": "ENG-110", "subject": "clause:ENG-110", "text": "An invented clause.", "source": [)" +
        swapped(repo, "src/a.py", "rules/clauses.txt") + "]}}, " +
        swapped(claim, R"("kind")", R"("name": "root", "kind")") + ", " + map(in_repo) + ", " + map(in_repo) + ", " +
        map(swapped(in_repo, "\"example\"", "\"other\""));
    FakePlatform platform(config());
    const Result r = build(file_of("import", entries, true), fold, platform);
    CHECK_EQ(r.refused, std::string());
    REQUIRE(r.built.size() == 5);
    CHECK(has(r.built[0].line, "\"target\":\"" + id_of('b') + "\""));  // the clause's current version
    CHECK(r.built[0].derived);
    CHECK(has(r.built[1].record, "\"id\":\"" + id_of('f') + "\""));   // @item: in a reference, the fold's
    CHECK(has(r.built[2].line, "\"target\":\"" + id_of('c') + "\""));  // the map of that repository's object
    CHECK(r.built[2].derived);
    CHECK(has(r.built[3].line, "\"target\":\"" + r.built[2].id + "\""));  // the file's own, before the fold's
    CHECK(r.built[3].derived);
    CHECK(has(r.built[4].line, "\"target\":\"" + id_of('e') + "\""));  // the other repository's
    for (const Out& o : r.built) {
        CHECK(holds(platform, o));
    }
    // A map's object is no place for a placeholder: its id is the content's,
    // refused as the content check finds it.
    const std::string named = swapped(entries, map(in_repo), map(root));
    CHECK_EQ(build(file_of("import", named, true), fold, platform).refused,
             std::string("invalid/content@entries[2].content.object.id#2:"));
}

QIVEN3_CASE(write_build, a_question_without_its_id_is_a_root) {
    FakeFold fold;
    const std::string scope = "project:example";
    fold.currents.push_back(Row{scope, "unit-one-shape", id_of('d'), "2026-10-29T07:00:00.000Z", 1, "question"});
    fold.currents.push_back(Row{scope, "core", id_of('f'), "2026-10-29T07:00:00.000Z", 1, "item"});
    const auto question = [&](const std::string& id) {
        return R"({"kind": "question", "basis": "claimed", "content": {"question": "Which shape?", )" + id +
               R"("subject": "entity:example", "band": "normal", )" + admit + ", " + evidence + "}}";
    };
    FakePlatform platform(config());
    const Result r =
        build(file_of("implementer/s7", question("") + ", " + question(R"("question_id": "unit-one-shape", )")), fold,
              platform);
    REQUIRE(r.refused.empty());
    REQUIRE(r.built.size() == 2);
    CHECK(!has(r.built[0].line, "\"target\""));
    CHECK(!r.built[0].derived);
    CHECK(has(r.built[1].line, "\"target\":\"" + id_of('d') + "\""));
    CHECK(r.built[1].derived);
    CHECK(has(r.built[0].record, "\"id\":\"" + id_of('f') + "\""));
}

QIVEN3_CASE(write_build, checks_expires_against_the_real_at) {
    FakeFold fold;
    fold.currents.push_back(Row{"project:example", "core", id_of('f'), "2026-10-29T07:00:00.000Z", 1, "item"});
    FakePlatform platform(config());
    const auto expiring = [&](std::string_view when) {
        return file_of("import", claim + ", " +
                                     swapped(claim, R"("band")", R"("expires": ")" + std::string(when) + R"(", "band")"));
    };
    // The reader's stand-in at lies far before; the built at is the clock's.
    const std::string refused = build(expiring("2026-10-29T08:00:00.000Z"), fold, platform).refused;
    CHECK(refused.starts_with("invalid/"));
    CHECK(has(refused, "@entries[1].content.expires#1:"));
    CHECK(build(expiring("2026-10-29T08:00:00.001Z"), fold, platform).refused.empty());
}

QIVEN3_CASE(write_build, an_import_checkpoint_revises_by_for_lane) {
    FakeFold fold;
    const std::string scope = "project:example";
    fold.heads.push_back(Row{scope, "import", id_of('a'), "2026-10-29T07:00:00.000Z", 9, ""});
    fold.heads.push_back(Row{scope, "implementer", id_of('9'), "2026-10-29T07:00:00.000Z", 3, ""});
    fold.currents.push_back(Row{scope, "implementer", id_of('c'), "2026-10-29T07:00:00.000Z", 3, "checkpoint"});
    fold.currents.push_back(Row{scope, "import", id_of('e'), "2026-10-29T07:00:00.000Z", 1, "checkpoint"});
    fold.currents.push_back(Row{scope, "core", id_of('f'), "2026-10-29T07:00:00.000Z", 1, "item"});
    const std::string checkpoint =
        R"({"kind": "checkpoint", "basis": "claimed", "content": {"for_lane": "implementer",
            "progress": "Unit one built.", "next_action": "The reviewer reviews unit one.",
            "subject": "item:core", "band": "high", )" + admit + ", " + evidence + "}}";
    FakePlatform platform(config());
    const Result r = build(file_of("import", checkpoint), fold, platform);
    REQUIRE(r.refused.empty());
    REQUIRE(r.built.size() == 1);
    CHECK(has(r.built[0].line, "\"lane\":\"import\""));
    CHECK(has(r.built[0].line, "\"parent\":\"" + id_of('a') + "\""));
    CHECK(r.built[0].seq == 10);
    CHECK(has(r.built[0].line, "\"target\":\"" + id_of('c') + "\""));  // for_lane's, not the import lane's
    CHECK(r.built[0].derived);
    CHECK(holds(platform, r.built[0]));
}
