// write/entries: the entries file's reader over synthetic files (every
// name, slot, scope and text invented). A write file read whole and kept
// past its bytes: its writer, scope and episode, its entries in order with
// their names, kinds, write_files and targets, each hole found (a piece
// reference, @name: naming an earlier entry, @question: in a target), and
// build_content filling them, the piece member dropped for the digest and
// the file's episode copied in, its canonical size the one the reader
// counts. An import file: no episode, an entry's scope over the file's
// default, a scope given by neither refused. Each form refusal naming its
// path and the entry once read, by its why; the content check's faults as
// invalid under the content's path, with their limit and value, a bound in
// characters so that a text of two-byte characters at its bound passes; a
// content past 65,536 bytes with its holes filled; an arena too small.

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entry/content_fields.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "write/entries.hpp"
#include "write/entries_reader.hpp"

namespace {

namespace we = qiven3::write::entries;
using qiven3::memory::Arena;

constexpr std::size_t mib = std::size_t{1} << 20U;

const std::string write_file = R"json({
  "form": "qiven3-entries/1",
  "writer": "implementer/s7",
  "scope": "project:example",
  "episode": {"client": "claude-code", "session": "synthetic-session-7"},
  "pieces": {
    "lane-record": {"text": "progress: unit one built\nnext_action: review\n"}
  },
  "entries": [
    {"name": "moved", "kind": "item", "basis": "claimed",
     "content": {"item_id": "core", "state": "active", "turn": "reviewer",
       "subject": "item:core", "band": "normal",
       "admit": {"underivable": "Whose turn it is lives in no file v3 reads.",
                 "mistake": "The next session takes a turn that is not its own."},
       "evidence": [{"type": "output", "command": "v2 transition core executed",
                     "piece": "lane-record"}]}},
    {"name": "lane", "kind": "checkpoint", "basis": "claimed",
     "content": {"progress": "Unit one built; its tests pass on both systems.",
       "next_action": "The reviewer reviews unit one.",
       "refs": ["@name:moved"],
       "subject": "item:core", "band": "high",
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
    {"name": "elsewhere", "scope": "project:other", "kind": "claim", "basis": "claimed",
     "content": {"claim": "Another invented claim.", "subject": "entity:example", "band": "low",
       "admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."},
       "evidence": [{"type": "entry", "id": "sha256:1111111111111111111111111111111111111111111111111111111111111111"}]}}
  ]
})json";

std::string swap(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) {
        return "missing: " + std::string(from);
    }
    return text.replace(at, from.size(), to);
}
std::string with(std::string_view from, std::string_view to) { return swap(write_file, from, to); }

std::string repeat(std::string_view unit, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += unit;
    }
    return out;
}

// The outcome of reading bytes: "ok", or code/why@path, #index and :name
// once read, and for invalid the content's code, limit and value.
std::string outcome(std::string_view bytes, std::size_t arena_bytes = 8 * mib) {
    std::vector<std::byte> pages(arena_bytes);
    Arena arena(pages);
    try {
        static_cast<void>(we::read(bytes, arena));
        return "ok";
    } catch (const we::EntriesError& e) {
        std::string out = std::string(we::name(e.code)) + "/" + std::string(we::name(e.why)) + "@" +
                          std::string(e.path.view());
        if (e.entry != we::none) {
            out += "#" + std::to_string(e.entry);
        }
        if (e.name.size() > 0) {
            out += ":" + std::string(e.name.view());
        }
        if (e.code == we::Code::invalid) {
            out += " " + std::string(qiven3::entry::content::name(e.fault)) + " " + std::to_string(e.limit) +
                   " " + std::to_string(e.value);
        }
        return out;
    }
}

std::string encoded(const qiven3::json::Value& v) {
    std::string out;
    qiven3::json::encode(v, out);
    return out;
}

}  // namespace

QIVEN3_CASE(write_entries, reads_a_write_file) {
    std::vector<std::byte> pages(8 * mib);
    Arena arena(pages);
    std::string bytes = write_file;
    const we::File f = we::read(bytes, arena);
    bytes.assign(bytes.size(), 'z');  // the file keeps its own texts
    CHECK(f.writer() == "implementer/s7");
    CHECK(f.scope() == "project:example");
    CHECK(!f.import());
    CHECK(f.episode() != 0);
    REQUIRE(f.pieces().size() == 1);
    CHECK(f.text(f.pieces()[0].name) == "lane-record");
    CHECK(!f.pieces()[0].path);
    const std::span<const we::Entry> e = f.entries();
    REQUIRE(e.size() == 3);
    CHECK(f.text(e[0].name) == "moved");
    CHECK(f.text(e[1].name) == "lane");
    CHECK(e[2].name == 0);
    CHECK(f.text(e[1].kind) == "checkpoint");
    CHECK(f.text(e[2].basis) == "claimed");
    CHECK(f.text(e[2].scope) == "project:example");
    CHECK(e[0].target == 0);
    REQUIRE(e[2].target_site != we::none);
    const we::Site& t = f.sites()[e[2].target_site];
    CHECK(t.hole == we::Hole::question);
    CHECK(f.key(t) == "unit-one-shape");
    REQUIRE(f.sites(e[0]).size() == 1);
    CHECK(f.sites(e[0])[0].hole == we::Hole::piece);
    CHECK(f.sites(e[0])[0].named == 0);
    REQUIRE(f.sites(e[1]).size() == 2);
    CHECK(f.sites(e[1])[0].hole == we::Hole::name);
    CHECK(f.sites(e[1])[0].named == 0);
    CHECK(f.sites(e[1])[1].hole == we::Hole::piece);
    REQUIRE(f.sites(e[2]).size() == 1);
    CHECK(f.sites(e[2])[0].named == 0);
    CHECK(f.key(f.sites(e[2])[0]) == "moved");
    CHECK(f.key(f.sites(e[0])[0]).empty());
}

QIVEN3_CASE(write_entries, fills_the_holes) {
    std::vector<std::byte> pages(8 * mib);
    Arena arena(pages);
    we::File f = we::read(write_file, arena);
    we::detail::Reader r(f, arena);
    const std::string digest = "hmac-sha256:" + repeat("ab", 32);
    const std::string id = "sha256:" + repeat("cd", 32);
    for (std::size_t i = 0; i < f.entries().size(); ++i) {
        std::vector<std::string_view> fills;
        for (const we::Site& s : f.sites(f.entries()[i])) {
            fills.push_back(s.hole == we::Hole::piece ? std::string_view(digest) : std::string_view(id));
        }
        qiven3::json::Value out{qiven3::memory::Allocator<char>(arena)};
        we::build_content(f, i, fills, out);
        const std::string text = encoded(out);
        CHECK(text.find("\"piece\"") == std::string::npos);
        CHECK(text.find("\"episode\":{\"client\":\"claude-code\",\"session\":\"synthetic-session-7\"}") !=
              std::string::npos);
        CHECK(text.find("@") == std::string::npos);
        CHECK(text.find(i == 2 ? id : digest) != std::string::npos);
        // The stand-ins' canonical size, counted in place, is the built one's.
        std::vector<std::string_view> stand;
        for (const we::Site& s : f.sites(f.entries()[i])) {
            stand.push_back(s.hole == we::Hole::piece ? we::detail::stand_digest : we::detail::stand_id);
        }
        qiven3::json::Value standing{qiven3::memory::Allocator<char>(arena)};
        we::build_content(f, i, stand, standing);
        CHECK(r.standing_size(f.entries()[i]) == encoded(standing).size());
    }
}

QIVEN3_CASE(write_entries, reads_an_import_file) {
    std::vector<std::byte> pages(8 * mib);
    Arena arena(pages);
    const we::File f = we::read(import_file, arena);
    CHECK(f.import());
    CHECK(f.episode() == 0);
    REQUIRE(f.entries().size() == 2);
    CHECK(f.text(f.entries()[0].scope) == "project:example");
    CHECK(f.text(f.entries()[1].scope) == "project:other");
    CHECK(f.sites().empty());
    CHECK(outcome(swap(import_file, "\"scope\": \"project:example\",", "")) == "form/missing@entries[0].scope#0");
    CHECK(outcome(swap(import_file, "\"writer\": \"import\",", "\"writer\": \"import\", \"episode\": {},")) ==
          "form/not_allowed@episode");
    CHECK(outcome(swap(import_file, "\"elsewhere\"", "\"else-where\"")) == "ok");
}

QIVEN3_CASE(write_entries, refuses_the_file_form) {
    CHECK(outcome(write_file) == "ok");
    CHECK(outcome("{") == "form/json@");
    CHECK(outcome("[]") == "form/type@");
    CHECK(outcome(with("\"form\":", "\"extra\": 1, \"form\":")) == "form/unknown_member@extra");
    CHECK(outcome(with("qiven3-entries/1", "qiven3-entries/2")) == "form/value@form");
    CHECK(outcome(with("\"writer\": \"implementer/s7\",", "")) == "form/missing@writer");
    CHECK(outcome(with("implementer/s7", "Implementer/s7")) == "form/syntax@writer");
    CHECK(outcome(with("implementer/s7", "implementer/s0")) == "form/syntax@writer");
    CHECK(outcome(with("\"scope\": \"project:example\",", "")) == "form/missing@scope");
    CHECK(outcome(with("project:example", "project:Example")) == "form/syntax@scope");
    CHECK(outcome(with("\"session\": \"synthetic-session-7\"", "\"session\": 7")) == "form/type@episode.session");
    CHECK(outcome(with("\"client\": \"claude-code\",", "\"client\": \"claude-code\", \"x\": \"y\",")) ==
          "form/unknown_member@episode.x");
    CHECK(outcome(with("\"episode\": {\"client\": \"claude-code\", \"session\": \"synthetic-session-7\"},", "")) ==
          "form/missing@episode");
    CHECK(outcome(with("\"entries\": [", "\"entries\": [], \"other\": [")) == "form/unknown_member@other");
    std::string none = write_file.substr(0, write_file.find("\"entries\": [")) + "\"entries\": []}";
    CHECK(outcome(swap(none, "\"pieces\": {\n    \"lane-record\": {\"text\": \"progress: unit one built\\nnext_action: "
                             "review\\n\"}\n  },",
                       "")) == "form/count@entries");
    CHECK(outcome(std::string(we::max_bytes + 1, ' ')) == "form/too_large@");
}

QIVEN3_CASE(write_entries, counts_the_entries) {
    const std::string claim = R"json({"kind": "claim", "basis": "claimed",
     "content": {"claim": "An invented claim.", "subject": "entity:example", "band": "normal",
       "admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."},
       "evidence": [{"type": "entry", "id": "@name:moved"}]}},)json";
    const std::string at = "{\"kind\": \"retire\"";
    CHECK(outcome(with(at, repeat(claim, 61) + at)) == "ok");
    CHECK(outcome(with(at, repeat(claim, 62) + at)) == "form/count@entries");
    std::string imports = swap(import_file, "\"entries\": [", "\"entries\": [" + repeat(claim, 1998));
    imports = swap(imports, "@name:moved", "@name:elsewhere");
    CHECK(outcome(imports).starts_with("form/placeholder@entries[0].content.evidence[0].id#0"));
    const std::string plain = swap(claim, "@name:moved", "sha256:" + repeat("1", 64));
    imports = swap(import_file, "\"entries\": [", "\"entries\": [" + repeat(plain, 1998));
    CHECK(outcome(imports, 32 * mib) == "ok");
    imports = swap(import_file, "\"entries\": [", "\"entries\": [" + repeat(plain, 1999));
    CHECK(outcome(imports, 32 * mib) == "form/count@entries");
}

QIVEN3_CASE(write_entries, refuses_the_pieces) {
    CHECK(outcome(with("\"lane-record\": {", "\"Lane_Record\": {")) == "form/syntax@pieces.Lane_Record");
    CHECK(outcome(with("\"lane-record\": {\"text\"", "\"lane-record\": {\"path\": \"/tmp/x\", \"text\"")) ==
          "form/not_allowed@pieces.lane-record.path");
    CHECK(outcome(with("{\"text\": \"progress: unit one built\\nnext_action: review\\n\"}", "{}")) ==
          "form/missing@pieces.lane-record.text");
    CHECK(outcome(with("{\"text\": \"progress: unit one built\\nnext_action: review\\n\"}",
                       "{\"path\": \"relative/file.txt\"}")) == "form/syntax@pieces.lane-record.path");
    CHECK(outcome(with("{\"text\": \"progress: unit one built\\nnext_action: review\\n\"}",
                       "{\"path\": \"/synthetic/record.txt\"}")) == "ok");
    CHECK(outcome(with("{\"text\": \"progress", "{\"text\": 7, \"x\": \"progress")) ==
          "form/unknown_member@pieces.lane-record.x");
    CHECK(outcome(with("\"lane-record\": {", "\"spare\": {\"text\": \"unused\"}, \"lane-record\": {")) ==
          "form/unused_piece@pieces.spare");
    CHECK(outcome(with("\"pieces\": {", "\"pieces\": {}, \"spare\": {")) == "form/json@");  // a key off canon's
    CHECK(outcome(with("\"pieces\": {\n    \"lane-record\": {\"text\": \"progress: unit one built\\nnext_action: "
                       "review\\n\"}\n  },",
                       "\"pieces\": [],")) == "form/type@pieces");
    CHECK(outcome(with("progress: unit one built", repeat("x", we::max_piece_bytes)), 24 * mib) ==
          "form/too_large@pieces.lane-record.text");
    CHECK(outcome(with("\"piece\": \"lane-record\"}]}},\n    {\"name\": \"lane\"",
                       "\"piece\": \"no-such-piece\"}]}},\n    {\"name\": \"lane\"")) ==
          "form/piece@entries[0].content.evidence[0].piece#0:moved");
    CHECK(outcome(with("\"command\": \"v2 transition core executed\",", "\"command\": \"v2 transition core executed\", "
                                                                         "\"digest\": \"hmac-sha256:00\",")) ==
          "form/not_allowed@entries[0].content.evidence[0].digest#0:moved");
    CHECK(outcome(with("\"command\": \"v2 transition core executed\",\n                     \"piece\": \"lane-record\"",
                       "\"command\": \"v2 transition core executed\"")) ==
          "form/missing@entries[0].content.evidence[0].piece#0:moved");
}

QIVEN3_CASE(write_entries, refuses_an_entry_form) {
    CHECK(outcome(with("{\"name\": \"moved\"", "7, {\"name\": \"moved\"")) == "form/type@entries[0]#0");
    CHECK(outcome(with("{\"name\": \"moved\",", "{\"name\": \"moved\", \"extra\": 1,")) ==
          "form/unknown_member@entries[0].extra#0");  // the members before the name
    CHECK(outcome(with("\"name\": \"lane\"", "\"name\": \"moved\"")) == "form/repeated@entries[1].name#1:moved");
    CHECK(outcome(with("\"name\": \"moved\"", "\"name\": \"Moved\"")) == "form/syntax@entries[0].name#0");
    CHECK(outcome(with("\"name\": \"moved\"", "\"name\": \"" + repeat("m", 65) + "\"")) ==
          "form/syntax@entries[0].name#0");
    CHECK(outcome(with("\"name\": \"moved\",", "\"name\": \"moved\", \"scope\": \"project:example\",")) ==
          "form/not_allowed@entries[0].scope#0:moved");
    CHECK(outcome(with("\"kind\": \"item\"", "\"kind\": \"itemized\"")) == "form/value@entries[0].kind#0:moved");
    CHECK(outcome(with("\"kind\": \"item\", \"basis\": \"claimed\"", "\"kind\": \"item\"")) ==
          "form/missing@entries[0].basis#0:moved");
    CHECK(outcome(with("\"kind\": \"item\", \"basis\": \"claimed\"", "\"kind\": \"item\", \"basis\": \"guessed\"")) ==
          "form/value@entries[0].basis#0:moved");
    CHECK(outcome(with("\"kind\": \"item\",", "\"kind\": \"item\", \"target\": \"@item:core\",")) ==
          "form/not_allowed@entries[0].target#0:moved");
    CHECK(outcome(with("\"target\": \"@question:unit-one-shape\",", "")) == "form/missing@entries[2].target#2");
    CHECK(outcome(with("@question:unit-one-shape", "sha256:" + repeat("A", 64))) == "form/syntax@entries[2].target#2");
    CHECK(outcome(with("@question:unit-one-shape", "sha256:" + repeat("a", 64))) == "ok");
    CHECK(outcome(with("@question:unit-one-shape", "@lane:reviewer")) == "ok");
    CHECK(outcome(with("@question:unit-one-shape", "@item:core")) == "ok");
    CHECK(outcome(with("@question:unit-one-shape", "@name:lane")) == "ok");
    CHECK(outcome(with("@question:unit-one-shape", "@thing:x")) == "form/placeholder@entries[2].target#2");
    CHECK(outcome(with("@question:unit-one-shape", "@lane:Reviewer")) == "form/placeholder@entries[2].target#2");
    CHECK(outcome(with("@question:unit-one-shape", "@question:")) == "form/placeholder@entries[2].target#2");
    CHECK(outcome(with("@question:unit-one-shape", "@name:nobody")) == "form/placeholder@entries[2].target#2");
    CHECK(outcome(with("\"refs\": [\"@name:moved\"]", "\"refs\": [\"@name:lane\"]")) ==
          "form/placeholder@entries[1].content.refs[0]#1:lane");
    CHECK(outcome(with("\"evidence\": [{\"type\": \"entry\", \"id\": \"@name:moved\"}]",
                       "\"evidence\": [{\"type\": \"entry\", \"id\": \"@moved\"}]")) ==
          "form/placeholder@entries[2].content.evidence[0].id#2");
    std::string listed = swap(import_file, "\"content\": {\"claim\": \"An invented claim.\"",
                              "\"content\": [{\"claim\": \"An invented claim.\"");
    listed = swap(listed, "\"}]}},\n    {\"name\": \"elsewhere\"", "\"}]}]},\n    {\"name\": \"elsewhere\"");
    CHECK(outcome(listed) == "form/type@entries[0].content#0");
    CHECK(outcome(with("\"item_id\": \"core\",", "\"item_id\": \"core\", \"episode\": {},")) ==
          "form/not_allowed@entries[0].content.episode#0:moved");
    CHECK(outcome(with("\"item_id\": \"core\",", "\"item_id\": \"core\", \"clears\": {},")) ==
          "form/not_allowed@entries[0].content.clears#0:moved");
}

QIVEN3_CASE(write_entries, refuses_an_invalid_content) {
    CHECK(outcome(with("\"turn\": \"reviewer\",", "\"turn\": \"reviewer\", \"colour\": \"red\",")) ==
          "invalid/content@entries[0].content.colour#0:moved unknown_field 0 0");
    CHECK(outcome(with("\"state\": \"active\"", "\"state\": \"busy\"")) ==
          "invalid/content@entries[0].content.state#0:moved syntax 0 0");
    CHECK(outcome(with("\"kind\": \"item\", \"basis\": \"claimed\"", "\"kind\": \"item\", \"basis\": \"observed\"")) ==
          "invalid/content@entries[0].content#0:moved basis 0 0");
    CHECK(outcome(with("project:example", "person")) == "invalid/content@entries[0].content#0:moved scope 0 0");
    CHECK(outcome(with("\"band\": \"normal\",", "")) == "invalid/content@entries[0].content.band#0:moved missing 0 0");
    const std::string reason = "Settled: the reviewer chose the shape in review.";
    CHECK(outcome(with(reason, repeat("r", 300))) == "ok");
    CHECK(outcome(with(reason, repeat("r", 301))) == "invalid/content@entries[2].content.reason#2 too_long 300 301");
    CHECK(outcome(with(reason, repeat("\xc3\xa9", 300))) == "ok");
    CHECK(outcome(with(reason, repeat("\xc3\xa9", 301))) ==
          "invalid/content@entries[2].content.reason#2 too_long 300 301");
    CHECK(outcome(with("\"command\": \"v2 transition core executed\"", "\"command\": \"\"")) ==
          "invalid/content@entries[0].content.evidence[0].command#0:moved empty 0 0");
    // Past the bound before the check: too_large, though the field is unknown.
    CHECK(outcome(with("\"turn\": \"reviewer\",", "\"turn\": \"reviewer\", \"colour\": \"" + repeat("c", 70000) +
                                                      "\","))
              .starts_with("invalid/content@entries[0].content#0:moved too_large 65536 "));
}

QIVEN3_CASE(write_entries, arena_too_small) {
    std::vector<std::byte> pages(64 * 1024);
    Arena arena(pages);
    bool budget = false;
    try {
        static_cast<void>(we::read(write_file, arena));
    } catch (const qiven3::memory::MemoryError& e) {
        budget = e.code == qiven3::memory::MemoryCode::budget;
    }
    CHECK(budget);
}
