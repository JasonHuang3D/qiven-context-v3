// import/manifest: the quote manifest's reader over synthetic manifests
// (invented documents, paths and repositories). Each part read: the
// manifest's own fields, documents, rulings, records, the three kinds of
// entry with their sources, notes and also_in, the goal, the person,
// replacements and amendments, kept past the bytes; each refusal, naming
// its path and the entry once read: a member missing, of another kind,
// past its bound (in characters, so a text of two-byte characters at its
// bound passes), not in its form, a value its place does not take, a
// member the form does not hold, a document named by no documents member,
// a document nothing names, an entry reference to no entry, an id, a
// ruling or a (doc, section, ordinal) twice, documents in two
// repositories, JSON the strict parser refuses, bytes past the bound;
// a manifest lacking rulings or records; verbatim found and not, across a
// line break and its indentation, with quotation marks, with non-ASCII
// text; check_verbatim naming the entry; an arena too small.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "import/manifest.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"

namespace {

namespace mf = qiven3::import::manifest;
using qiven3::memory::Arena;

// An e with an acute accent, two bytes in UTF-8, and a word ending in it.
constexpr std::string_view acute = "\xc3\xa9";
constexpr std::string_view cafe = "caf\xc3\xa9";

const std::string base = std::string(R"json({
  "manifest": "test-manifest/1",
  "written_by": "reviewer/s1",
  "records": "records-repo",
  "documents": {
    "notes": {"repo": "docs-repo", "path": "texts/notes.md", "revision": "0123456789abcdef0123456789abcdef01234567"},
    "rules-a": {"repo": "docs-repo", "path": "rules/a.md", "revision": "abcd"}
  },
  "rulings": ["rules-a"],
  "entries": [
    {"id": "dir-1-01", "kind": "direction", "basis": "owner-words", "scope": "project:demo", "band": "high",
     "band_basis": "proposal", "doc": "notes", "section": "1", "ordinal": 1,
     "words": "keep the record \"plain\" at the )json") + std::string(cafe) + R"json(",
     "source": [{"doc": "notes", "section": "1"}], "note": "quoted with the spelling corrected",
     "also_in": [{"doc": "rules-a", "section": "2.1", "words": "plain", "part": true}]},
    {"id": "dec-1-01", "kind": "decision", "basis": "claimed", "scope": "project:demo", "band": "normal",
     "doc": "notes", "section": "1", "ordinal": 2, "decides": "One store per person.",
     "reasons": ["It stays private.", "It moves whole."],
     "source": [{"doc": "notes", "section": "1"}, {"entry": "dir-1-01"}]},
    {"id": "clm-2-01", "kind": "claim", "basis": "claimed", "scope": "project:demo", "band": "normal",
     "doc": "notes", "section": "2", "ordinal": 7, "claim": "Reads come before writes.",
     "source": [{"doc": "notes", "section": "2"}]}
  ],
  "goal": {"kind": "goal", "basis": "claimed", "scope": "project:demo", "band": "core", "doc": "notes",
           "section": "3", "objective": "Serve one person.", "boundary": "One repository.",
           "test_questions": ["Who is the person?"], "source": [{"entry": "dir-1-01"}],
           "source_reasons": {"dir-1-01": "states the record's form"}, "seed_retire_reason": "replaced",
           "seed_retire_reason_source": {"doc": "notes", "section": "3"}, "when": "the manifest's commit"},
  "person": {"kind": "decision", "basis": "claimed", "scope": "person", "band": "core",
             "decides": "Who the person is.", "reasons": ["Wants the reason first."],
             "decides_source": [{"doc": "notes", "section": "4"}],
             "reasons_source": [[{"doc": "notes", "section": "4"}]],
             "source": [{"doc": "notes", "section": "4"}], "note": "the reviewer's words"},
  "replacements": [{"doc": "rules-a", "line": 12, "in_section": "3", "text": "2.1 is replaced by 3.2.",
                    "replaced": {"doc": "rules-a", "section": "2.1"},
                    "replaced_by": {"doc": "rules-a", "section": "3.2"}, "decisions": {"from": "2", "to": "3"}}],
  "amendments": [{"doc": "rules-a", "line": 3, "in_section": "preamble", "text": "Supersedes the old rules.",
                  "what": "the header"}]
}
)json";

// text with its first `from` made `to`; a `from` text lacks gives the empty
// text, which no case expects read.
std::string swap(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) {
        return {};
    }
    text.replace(at, from.size(), to);
    return text;
}
std::string with(std::string_view from, std::string_view to) { return swap(base, from, to); }

// The outcome of reading bytes: "ok", or code@path, and #id once read.
std::string outcome(std::string_view bytes) {
    std::vector<std::byte> pages(1024 * 1024);
    Arena arena(pages);
    try {
        static_cast<void>(mf::read_manifest(bytes, arena));
        return "ok";
    } catch (const mf::ManifestError& e) {
        std::string out = std::string(mf::name(e.code)) + "@" + std::string(e.path.view());
        if (e.code == mf::Code::json) {
            out += std::string(qiven3::name(e.parse));
        }
        if (e.entry.size() > 0) {
            out += "#" + std::string(e.entry.view());
        }
        return out;
    }
}

std::string repeat(std::string_view unit, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += unit;
    }
    return out;
}

}  // namespace

QIVEN3_CASE(manifest, reads_every_part) {
    std::vector<std::byte> pages(256 * 1024);
    Arena arena(pages);
    std::string bytes = base;
    const mf::Manifest m = mf::read_manifest(bytes, arena);
    bytes.assign(bytes.size(), 'z');  // the manifest keeps its own texts
    CHECK(m.form() == "test-manifest/1");
    CHECK(m.written_by() == "reviewer/s1");
    CHECK(m.records() == "records-repo");
    CHECK(m.repository() == "docs-repo");
    REQUIRE(m.documents().size() == 2);
    CHECK(m.text(m.documents()[0].name) == "notes");
    CHECK(m.text(m.documents()[0].path) == "texts/notes.md");
    CHECK(m.text(m.documents()[1].revision) == "abcd");
    CHECK_EQ(m.document("rules-a"), std::optional<std::size_t>(1));
    CHECK(!m.document("absent").has_value());
    REQUIRE(m.rulings().size() == 1);
    CHECK_EQ(m.rulings()[0], 1U);

    REQUIRE(m.entries().size() == 3);
    const mf::Entry& quote = m.entries()[0];
    CHECK(quote.kind == mf::Kind::direction);
    CHECK(m.text(quote.text) == "keep the record \"plain\" at the " + std::string(cafe));
    CHECK(m.text(quote.basis) == "owner-words" && m.text(quote.band) == "high");
    CHECK(m.text(quote.scope) == "project:demo" && m.text(quote.band_basis) == "proposal");
    CHECK(m.text(quote.doc) == "notes" && m.text(quote.section) == "1");
    CHECK_EQ(quote.ordinal, 1);
    CHECK(m.text(quote.note) == "quoted with the spelling corrected");
    REQUIRE(m.alsos(quote.also).size() == 1);
    CHECK(m.alsos(quote.also)[0].part && m.text(m.alsos(quote.also)[0].words) == "plain");
    CHECK(m.text(m.alsos(quote.also)[0].section) == "2.1");

    const mf::Entry& decision = m.entries()[1];
    CHECK(decision.kind == mf::Kind::decision && m.text(decision.text) == "One store per person.");
    REQUIRE(m.texts(decision.reasons).size() == 2);
    CHECK(m.text(m.texts(decision.reasons)[1]) == "It moves whole.");
    REQUIRE(m.sources(decision.sources).size() == 2);
    CHECK(m.text(m.sources(decision.sources)[0].doc) == "notes");
    CHECK(m.text(m.sources(decision.sources)[0].section) == "1");
    CHECK(m.text(m.sources(decision.sources)[1].entry) == "dir-1-01");
    CHECK_EQ(decision.note, mf::Text{0});
    CHECK(m.entries()[2].kind == mf::Kind::claim && m.text(m.entries()[2].text) == "Reads come before writes.");
    CHECK_EQ(m.entry("clm-2-01"), std::optional<std::size_t>(2));
    CHECK(!m.entry("dir-9-99").has_value());

    const mf::Goal& goal = m.goal();
    CHECK(m.text(goal.objective) == "Serve one person." && m.text(goal.boundary) == "One repository.");
    CHECK(m.text(goal.scope) == "project:demo" && m.text(goal.section) == "3");
    REQUIRE(m.texts(goal.test_questions).size() == 1);
    CHECK(m.text(m.sources(goal.sources)[0].entry) == "dir-1-01");
    REQUIRE(m.pairs(goal.source_reasons).size() == 1);
    CHECK(m.text(m.pairs(goal.source_reasons)[0].key) == "dir-1-01");
    CHECK(m.text(m.pairs(goal.source_reasons)[0].text) == "states the record's form");
    CHECK(m.text(goal.seed_retire_reason_source.section) == "3" && m.text(goal.when) == "the manifest's commit");

    const mf::Person& person = m.person();
    CHECK(m.text(person.decides) == "Who the person is." && m.text(person.note) == "the reviewer's words");
    REQUIRE(m.groups(person.reasons_source).size() == 1);
    const mf::Span group = m.groups(person.reasons_source)[0];
    CHECK(m.text(m.sources(group)[0].section) == "4");
    CHECK_EQ(m.sources(person.decides_source).size(), 1U);

    REQUIRE(m.replacements().size() == 1);
    const mf::Replacement& r = m.replacements()[0];
    CHECK(m.text(r.replaced.section) == "2.1" && m.text(r.replaced_by.section) == "3.2");
    CHECK(m.text(r.from) == "2" && m.text(r.to) == "3" && r.line == 12);
    REQUIRE(m.amendments().size() == 1);
    CHECK(m.text(m.amendments()[0].in_section) == "preamble" && m.text(m.amendments()[0].what) == "the header");
}

// The optional parts absent: an empty rulings list, no replacements, no
// amendments, an entry without note or band_basis.
QIVEN3_CASE(manifest, optional_parts_absent) {
    std::string bytes = swap(with(R"("rulings": ["rules-a"])", R"("rulings": [])"), R"("band_basis": "proposal", )", "");
    bytes = swap(bytes, R"(, "note": "quoted with the spelling corrected")", "");
    const std::size_t from = bytes.find(",\n  \"replacements\"");
    REQUIRE(from != std::string::npos);
    bytes.erase(from, bytes.rfind('}') - from);
    std::vector<std::byte> pages(256 * 1024);
    Arena arena(pages);
    const mf::Manifest m = mf::read_manifest(bytes, arena);
    CHECK(m.rulings().empty() && m.replacements().empty() && m.amendments().empty());
    CHECK_EQ(m.entries()[0].note, mf::Text{0});
    CHECK_EQ(m.entries()[0].band_basis, mf::Text{0});
    // rules-a was named by its ruling, its replacement and its amendment;
    // the also_in item names it still, and without it nothing does.
    const std::string bare = swap(bytes, R"(,
     "also_in": [{"doc": "rules-a", "section": "2.1", "words": "plain", "part": true}])", "");
    CHECK_EQ(outcome(bare), std::string("unused_document@documents.rules-a"));
}

QIVEN3_CASE(manifest, refused_missing) {
    CHECK_EQ(outcome(with(R"("records": "records-repo",)", "")), std::string("missing@records"));
    CHECK_EQ(outcome(with(R"("rulings": ["rules-a"],)", "")), std::string("missing@rulings"));
    CHECK_EQ(outcome(with(R"("revision": "abcd")", R"("x_y": 1)")),
             std::string("unknown_field@documents.rules-a.x_y"));
    CHECK_EQ(outcome(with(R"(, "revision": "abcd")", "")), std::string("missing@documents.rules-a.revision"));
    CHECK_EQ(outcome(with(R"("doc": "notes", "section": "2", )", R"("doc": "notes", )")),
             std::string("missing@entries[2].section#clm-2-01"));
    CHECK_EQ(outcome(with(R"("objective": "Serve one person.", )", "")), std::string("missing@goal.objective"));
    CHECK_EQ(outcome(with(R"("source": [{"doc": "notes", "section": "2"}])", R"("note": "n")")),
             std::string("missing@entries[2].source#clm-2-01"));
}

QIVEN3_CASE(manifest, refused_type) {
    CHECK_EQ(outcome("[]"), std::string("type@"));
    CHECK_EQ(outcome(with(R"("ordinal": 7)", R"("ordinal": "7")")), std::string("type@entries[2].ordinal#clm-2-01"));
    CHECK_EQ(outcome(with(R"("rulings": ["rules-a"])", R"("rulings": "rules-a")")), std::string("type@rulings"));
    CHECK_EQ(outcome(with(R"(["It stays private.", )", R"([7, )")),
             std::string("type@entries[1].reasons[0]#dec-1-01"));
    CHECK_EQ(outcome(with(R"("part": true)", R"("part": "yes")")),
             std::string("type@entries[0].also_in[0].part#dir-1-01"));
    CHECK_EQ(outcome(with(R"([[{"doc": "notes", "section": "4"}]])", R"([{"doc": "notes", "section": "4"}])")),
             std::string("type@person.reasons_source[0]"));
}

// Bounds in characters: 2,000 two-byte characters pass as words, 2,001 do
// not; lists past their items; an empty text or list; an integer outside.
QIVEN3_CASE(manifest, refused_bound) {
    const std::string words = R"("words": "keep the record \"plain\" at the )" + std::string(cafe) + "\"";
    CHECK_EQ(outcome(with(words, "\"words\": \"" + repeat(acute, mf::max_words) + "\"")), std::string("ok"));
    CHECK_EQ(outcome(with(words, "\"words\": \"" + repeat(acute, mf::max_words + 1) + "\"")),
             std::string("bound@entries[0].words#dir-1-01"));
    CHECK_EQ(outcome(with(R"("claim": "Reads come before writes.")", R"("claim": "")")),
             std::string("bound@entries[2].claim#clm-2-01"));
    CHECK_EQ(outcome(with(R"("ordinal": 7)", R"("ordinal": 0)")), std::string("bound@entries[2].ordinal#clm-2-01"));
    CHECK_EQ(outcome(with(R"("reasons": ["It stays private.", "It moves whole."])",
                          "\"reasons\": [" + repeat(R"("r",)", 8) + "\"r\"]")),
             std::string("bound@entries[1].reasons#dec-1-01"));
    CHECK_EQ(outcome(with(R"("source": [{"doc": "notes", "section": "2"}])",
                          "\"source\": [" + repeat(R"({"doc": "notes", "section": "2"},)", 8) +
                              R"({"doc": "notes", "section": "2"}])")),
             std::string("bound@entries[2].source#clm-2-01"));
    CHECK_EQ(outcome(with(R"("source": [{"entry": "dir-1-01"}])", R"("source": [])")),
             std::string("bound@goal.source"));
    CHECK_EQ(outcome(with(R"("decides": "Who the person is.")", "\"decides\": \"" + repeat("d", 501) + "\"")),
             std::string("bound@person.decides"));
    CHECK_EQ(outcome(with(R"("line": 12)", R"("line": -1)")), std::string("bound@replacements[0].line"));
}

QIVEN3_CASE(manifest, refused_form) {
    CHECK_EQ(outcome(with(R"("revision": "abcd")", R"("revision": "ABCD")")),
             std::string("form@documents.rules-a.revision"));
    CHECK_EQ(outcome(with(R"("revision": "abcd")", R"("revision": "abc")")),
             std::string("form@documents.rules-a.revision"));
    CHECK_EQ(outcome(with(R"("revision": "abcd")", "\"revision\": \"" + repeat("a", 41) + "\"")),
             std::string("bound@documents.rules-a.revision"));
    for (const std::string_view path : {"../a.md", "/rules/a.md", "rules//a.md", "rules/./a.md", "c:/a.md",
                                        "rules\\\\a.md", "rules/"}) {
        CHECK_EQ(outcome(with(R"("path": "rules/a.md")", "\"path\": \"" + std::string(path) + "\"")),
                 std::string("form@documents.rules-a.path"));
    }
    CHECK_EQ(outcome(with(R"("repo": "docs-repo", "path": "rules)", R"("repo": "..", "path": "rules)")),
             std::string("form@documents.rules-a.repo"));
    CHECK_EQ(outcome(with(R"("rules-a": {)", R"("Rules-A": {)")), std::string("form@documents.Rules-A"));
    CHECK_EQ(outcome(with(R"("id": "clm-2-01")", R"("id": "Claim 2")")), std::string("form@entries[2].id"));
    CHECK_EQ(outcome(with(R"("section": "2", "ordinal")", R"("section": "2 b", "ordinal")")),
             std::string("form@entries[2].section#clm-2-01"));
    CHECK_EQ(outcome(with(R"("scope": "project:demo", "band": "normal",
     "doc": "notes", "section": "2")", R"("scope": "demo", "band": "normal",
     "doc": "notes", "section": "2")")),
             std::string("form@entries[2].scope#clm-2-01"));
    CHECK_EQ(outcome(with(R"("records": "records-repo")", R"("records": "records repo")")),
             std::string("form@records"));
}

QIVEN3_CASE(manifest, refused_value) {
    CHECK_EQ(outcome(with(R"("kind": "claim")", R"("kind": "quote")")), std::string("value@entries[2].kind#clm-2-01"));
    CHECK_EQ(outcome(with(R"("basis": "owner-words")", R"("basis": "claimed")")),
             std::string("value@entries[0].basis#dir-1-01"));
    CHECK_EQ(outcome(with(R"("band": "high")", R"("band": "normal")")), std::string("value@entries[0].band#dir-1-01"));
    CHECK_EQ(outcome(with(R"("kind": "goal")", R"("kind": "decision")")), std::string("value@goal.kind"));
    CHECK_EQ(outcome(with(R"("scope": "person")", R"("scope": "project:demo")")), std::string("value@person.scope"));
    CHECK_EQ(outcome(with(R"("reasons": ["Wants the reason first."])", R"("reasons": ["one", "two"])")),
             std::string("value@person.reasons_source"));
}

QIVEN3_CASE(manifest, refused_unknown_field) {
    CHECK_EQ(outcome(with(R"("written_by")", R"("extra": "x", "written_by")")), std::string("unknown_field@extra"));
    CHECK_EQ(outcome(with(R"("claim": "Reads come before writes.")",
                          R"("claim": "Reads come before writes.", "words": "w")")),
             std::string("unknown_field@entries[2].words#clm-2-01"));
    CHECK_EQ(outcome(with(R"({"entry": "dir-1-01"}]},)", R"({"entry": "dir-1-01", "doc": "notes"}]},)")),
             std::string("unknown_field@entries[1].source[1].doc#dec-1-01"));
    CHECK_EQ(outcome(with(R"("source": [{"doc": "notes", "section": "4"}])", R"("source": [{"entry": "dir-1-01"}])")),
             std::string("unknown_field@person.source[0].entry"));
    CHECK_EQ(outcome(with(R"("goal": {"kind")", R"("goal": {"subject": "s", "kind")")),
             std::string("unknown_field@goal.subject"));
}

// A document named by no documents member, a document nothing names, and
// documents in two repositories.
QIVEN3_CASE(manifest, refused_documents) {
    CHECK_EQ(outcome(with(R"("doc": "notes", "section": "2", )", R"("doc": "absent", "section": "2", )")),
             std::string("unknown_document@entries[2].doc#clm-2-01"));
    CHECK_EQ(outcome(with(R"("rulings": ["rules-a"])", R"("rulings": ["absent"])")),
             std::string("unknown_document@rulings[0]"));
    CHECK_EQ(outcome(with(R"("replaced": {"doc": "rules-a")", R"("replaced": {"doc": "nope")")),
             std::string("unknown_document@replacements[0].replaced.doc"));
    CHECK_EQ(outcome(with(R"("rules-a": {"repo")", R"("spare": {"repo": "docs-repo", "path": "s.md", "revision": "abcd"},
    "rules-a": {"repo")")),
             std::string("unused_document@documents.spare"));
    CHECK_EQ(outcome(with(R"("repo": "docs-repo", "path": "rules)", R"("repo": "other-repo", "path": "rules)")),
             std::string("two_repositories@documents.rules-a.repo"));
}

QIVEN3_CASE(manifest, refused_references) {
    CHECK_EQ(outcome(with(R"({"entry": "dir-1-01"}]},)", R"({"entry": "dir-9-09"}]},)")),
             std::string("unknown_entry@entries[1].source[1].entry#dec-1-01"));
    CHECK_EQ(outcome(swap(with(R"("source": [{"entry": "dir-1-01"}],)", R"("source": [{"entry": "dec-1-01"}],)"),
                          R"("source_reasons": {"dir-1-01")", R"("source_reasons": {"dec-1-01")")),
             std::string("unknown_entry@goal.source[0].entry"));
    CHECK_EQ(outcome(with(R"("source_reasons": {"dir-1-01")", R"("source_reasons": {"dir-1.2-07")")),
             std::string("unknown_entry@goal.source_reasons.dir-1.2-07"));
}

// A repeated id names the later entry; a ruling or an entry's (doc,
// section, ordinal) twice.
QIVEN3_CASE(manifest, refused_duplicate) {
    CHECK_EQ(outcome(with(R"("id": "clm-2-01")", R"("id": "dir-1-01")")),
             std::string("duplicate@entries[2].id#dir-1-01"));
    CHECK_EQ(outcome(with(R"("rulings": ["rules-a"])", R"("rulings": ["rules-a", "rules-a"])")),
             std::string("duplicate@rulings[1]"));
    CHECK_EQ(outcome(with(R"("section": "1", "ordinal": 2)", R"("section": "1", "ordinal": 1)")),
             std::string("duplicate@entries[1].ordinal#dec-1-01"));
}

// The present form, lacking rulings and records, is refused; JSON the
// strict parser refuses names its code; bytes past the bound.
QIVEN3_CASE(manifest, refused_json_and_size) {
    CHECK_EQ(outcome(with(R"("records": "records-repo",)", R"("records": null,)")), std::string("json@null"));
    CHECK_EQ(outcome(with(R"("line": 3, )", R"("line": 3.5, )")), std::string("json@float"));
    CHECK_EQ(outcome(with(R"("what": "the header"})", R"("what": "the header",})")), std::string("json@syntax"));
    CHECK_EQ(outcome(with(R"("written_by": "reviewer/s1")", R"("written_by": "a", "written_by": "b")")),
             std::string("json@duplicate_key"));
    CHECK_EQ(outcome("\xef\xbb\xbf" + base), std::string("json@bom"));
    std::string big = base;
    big.resize(mf::max_bytes, ' ');
    CHECK_EQ(outcome(big), std::string("ok"));
    big += " ";
    CHECK_EQ(outcome(big), std::string("too_large@"));
}

// The owner's quotation marks are bytes of the words; a line break and the
// spaces and tabs after it read as one space; non-ASCII matched bytewise.
QIVEN3_CASE(manifest, verbatim) {
    const std::string document = "# Notes\n\n- She said \"keep the record\n  \"plain\"\" at the\n\t" +
                                 std::string(cafe) + ", twice.\nend\n";
    CHECK(mf::verbatim("keep the record \"plain\"", document));
    CHECK(mf::verbatim("\"plain\"\" at the " + std::string(cafe) + ", twice.", document));
    CHECK(mf::verbatim("# Notes  - She", document));  // an empty line: two breaks, two spaces
    CHECK(mf::verbatim("twice. end ", document));
    CHECK(mf::verbatim(cafe, document));
    CHECK(!mf::verbatim("keep the record plain", document));
    CHECK(!mf::verbatim("keep the record\n  \"plain\"", document));  // the words are never read so
    CHECK(!mf::verbatim("keep the record   \"plain\"", document));    // nor is the indentation kept
    CHECK(!mf::verbatim("cafe", document));
    CHECK(!mf::verbatim("", document));
    CHECK(!mf::verbatim("end\n", document));
    CHECK(!mf::verbatim("x", ""));
}

QIVEN3_CASE(manifest, check_verbatim_names_the_entry) {
    std::vector<std::byte> pages(256 * 1024);
    Arena arena(pages);
    const mf::Manifest m = mf::read_manifest(base, arena);
    mf::check_verbatim(m, 0, "Ask them to keep the record\n   \"plain\" at the " + std::string(cafe) + ".");
    bool named = false;
    try {
        mf::check_verbatim(m, 0, "keep the record plain");
    } catch (const mf::ManifestError& e) {
        named = e.code == mf::Code::not_verbatim && e.path.view() == "entries[0].words" &&
                e.entry.view() == "dir-1-01";
    }
    CHECK(named);
}

QIVEN3_CASE(manifest, arena_too_small) {
    std::vector<std::byte> pages(1024);
    Arena arena(pages);
    bool budget = false;
    try {
        static_cast<void>(mf::read_manifest(base, arena));
    } catch (const qiven3::memory::MemoryError& e) {
        budget = e.code == qiven3::memory::MemoryCode::budget;
    }
    CHECK(budget);
}
