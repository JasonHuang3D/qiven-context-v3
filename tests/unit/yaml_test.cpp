// import/yaml: the flat YAML reader over synthetic records. Each form read
// (a text quoted both ways and plain, " #" kept as text, a list with its
// items, comments and empty lines between, a whole-line comment, the empty
// line); each refusal, naming its line: a byte not UTF-8, a tab or a CR, an
// indented line, a line of no form, a key twice, an item with no list
// open, a quoted scalar unclosed or holding its quote or a backslash, a
// plain scalar the source system types, a file past its bound; the first
// faulty line deciding; the fields kept past their bytes; an arena too
// small, MemoryError budget.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "import/yaml.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"

namespace {

namespace yaml = qiven3::import::yaml;
using qiven3::memory::Arena;

// The outcome of reading text: "ok", or the refusal's code and line.
std::string outcome(std::string_view text) {
    std::vector<std::byte> pages(256 * 1024);  // on the heap: a file at its bound and its fields
    Arena arena(pages);
    try {
        static_cast<void>(yaml::read_flat(text, arena));
        return "ok";
    } catch (const yaml::YamlError& e) {
        return std::string(yaml::name(e.code)) + "@" + std::to_string(e.line);
    }
}

// A one-line record's text field read, or none.
std::optional<std::string> text_of(std::string_view line) {
    std::vector<std::byte> pages(4096);
    Arena arena(pages);
    const std::string file = "key: " + std::string(line) + "\n";
    try {
        const yaml::Fields fields = yaml::read_flat(file, arena);
        return std::string(fields.text(0));
    } catch (const yaml::YamlError&) {
        return std::nullopt;
    }
}

constexpr std::string_view record =
    "# a synthetic record\n"
    "record_schema_version: 1.0.0\n"
    "task_id: \"task-one\"\n"
    "objective: 'land the reader'\n"
    "next_action: open PR #7 and wait\n"
    "\n"
    "refs:\n"
    "- \"a/b.md\"\n"
    "# between items\n"
    "\n"
    "- plain item\n"
    "empty:\n"
    "phase: \"caf\xc3\xa9\"\n";

}  // namespace

QIVEN3_CASE(yaml, forms) {
    std::vector<std::byte> pages(4096);
    Arena arena(pages);
    const yaml::Fields fields = yaml::read_flat(record, arena);
    REQUIRE(fields.size() == 7);
    const std::vector<std::string_view> keys = {"record_schema_version", "task_id", "objective", "next_action",
                                                "refs", "empty", "phase"};
    const std::vector<std::size_t> lines = {2, 3, 4, 5, 7, 12, 13};
    for (std::size_t i = 0; i < keys.size(); ++i) {
        CHECK(fields.key(i) == keys[i]);
        CHECK_EQ(fields.line(i), lines[i]);
        CHECK_EQ(fields.find(keys[i]), std::optional<std::size_t>(i));
    }
    CHECK(fields.text(0) == "1.0.0");
    CHECK(fields.text(1) == "task-one");
    CHECK(fields.text(2) == "land the reader");
    CHECK(fields.text(3) == "open PR #7 and wait");
    CHECK(fields.list(4) && fields.text(4).empty());
    REQUIRE(fields.count(4) == 2);
    CHECK(fields.item(4, 0) == "a/b.md");
    CHECK(fields.item(4, 1) == "plain item");
    CHECK(fields.list(5) && fields.count(5) == 0);
    CHECK(!fields.list(6) && fields.text(6) == "caf\xc3\xa9");
    CHECK_EQ(fields.count(6), 0U);
    CHECK(!fields.find("absent").has_value());
}

// The fields keep their own text: the bytes read may go.
QIVEN3_CASE(yaml, outlives_bytes) {
    std::vector<std::byte> pages(4096);
    Arena arena(pages);
    std::string bytes = "a: \"one\"\nb:\n- two\n";
    const yaml::Fields fields = yaml::read_flat(bytes, arena);
    bytes.assign(bytes.size(), 'z');
    bytes.clear();
    bytes.shrink_to_fit();
    CHECK(fields.key(0) == "a" && fields.text(0) == "one");
    CHECK(fields.key(1) == "b" && fields.item(1, 0) == "two");
}

QIVEN3_CASE(yaml, scalars) {
    CHECK_EQ(text_of("\"\""), std::optional<std::string>(""));
    CHECK_EQ(text_of("''"), std::optional<std::string>(""));
    CHECK_EQ(text_of("\"it's\""), std::optional<std::string>("it's"));
    CHECK_EQ(text_of("'say \"hi\"'"), std::optional<std::string>("say \"hi\""));
    CHECK_EQ(text_of("\"a # b\""), std::optional<std::string>("a # b"));
    CHECK_EQ(text_of("   spaced out   "), std::optional<std::string>("spaced out"));
    CHECK_EQ(text_of("\"kept  \"   "), std::optional<std::string>("kept  "));
    CHECK_EQ(text_of("a: b: c"), std::optional<std::string>("a: b: c"));
    CHECK_EQ(text_of("# not a comment"), std::optional<std::string>("# not a comment"));
    CHECK_EQ(text_of("path\\to"), std::optional<std::string>("path\\to"));
    CHECK_EQ(text_of("ends with \""), std::optional<std::string>("ends with \""));
    // Not typed: no float's text reads them.
    for (const std::string_view plain : {"1_", "_1", "1__0", "1e", "1e+", ".", "+", "0x10", "12 apples", "nan2",
                                         "in", "1.2.3", "1._5", "yes", "TRUEly"}) {
        CHECK_EQ(text_of(plain), std::optional<std::string>(plain));
    }
}

QIVEN3_CASE(yaml, empty_and_unended) {
    CHECK_EQ(outcome(""), std::string("ok"));
    CHECK_EQ(outcome("\n\n# only comments\n"), std::string("ok"));
    std::vector<std::byte> pages(4096);
    Arena arena(pages);
    const yaml::Fields fields = yaml::read_flat("a: x\nlist:\n- last", arena);
    REQUIRE(fields.size() == 2);
    CHECK(fields.text(0) == "x");
    REQUIRE(fields.count(1) == 1);
    CHECK(fields.item(1, 0) == "last");
    CHECK(yaml::read_flat("", arena).empty());
}

QIVEN3_CASE(yaml, refused_utf8) {
    CHECK_EQ(outcome("a: x\nb: \"\xff\"\n"), std::string("utf8@2"));
    CHECK_EQ(outcome("a: \xc3\n"), std::string("utf8@1"));
    CHECK_EQ(outcome("a: x\n# \xed\xa0\x80 a surrogate\n"), std::string("utf8@2"));
    CHECK_EQ(outcome("a: \xc0\xaf\n"), std::string("utf8@1"));
}

QIVEN3_CASE(yaml, refused_control) {
    CHECK_EQ(outcome("a: x\tb\n"), std::string("control@1"));
    CHECK_EQ(outcome("a: x\r\nb: y\r\n"), std::string("control@1"));
    CHECK_EQ(outcome("a: x\n\t\n"), std::string("control@2"));
    CHECK_EQ(outcome("a: x\n\r"), std::string("control@2"));
    CHECK_EQ(outcome("a: x\n# tab\tin a comment\n"), std::string("control@2"));
}

QIVEN3_CASE(yaml, refused_indented) {
    CHECK_EQ(outcome("a:\n  - x\n"), std::string("indented@2"));
    CHECK_EQ(outcome(" a: x\n"), std::string("indented@1"));
    CHECK_EQ(outcome("a: x\n   \n"), std::string("indented@2"));
    CHECK_EQ(outcome("a: x\n # comment\n"), std::string("indented@2"));
}

QIVEN3_CASE(yaml, refused_form) {
    for (const std::string_view line : {"no colon here", "Key: x", "key-name: x", "key:value", "key::", ": x",
                                        "-", "-x", "--- ", "{a: 1}", "\xef\xbb\xbf" "a: x", "key :x"}) {
        CHECK_EQ(outcome("a: x\n" + std::string(line) + "\n"), std::string("form@2"));
    }
}

QIVEN3_CASE(yaml, refused_duplicate_key) {
    CHECK_EQ(outcome("a: x\nb: y\na: z\n"), std::string("duplicate_key@3"));
    CHECK_EQ(outcome("a:\n- x\na: y\n"), std::string("duplicate_key@3"));
    CHECK_EQ(outcome("a: x\na:\n"), std::string("duplicate_key@2"));
    // The earliest second line, whatever the keys' order.
    CHECK_EQ(outcome("b: 1x\na: 2x\na: 3x\nb: 4x\n"), std::string("duplicate_key@3"));
    // On its line the key is read before its value; a later fault waits.
    CHECK_EQ(outcome("a: x\na: \"bad\n"), std::string("duplicate_key@2"));
    CHECK_EQ(outcome("a: x\na: y\nnot a line\n"), std::string("duplicate_key@2"));
    CHECK_EQ(outcome("not a line\na: x\na: y\n"), std::string("form@1"));
}

QIVEN3_CASE(yaml, refused_outside_list) {
    CHECK_EQ(outcome("- x\n"), std::string("outside_list@1"));
    CHECK_EQ(outcome("a:\n- x\nb: y\n- z\n"), std::string("outside_list@4"));
    CHECK_EQ(outcome("# comment\n\n- x\n"), std::string("outside_list@3"));
}

QIVEN3_CASE(yaml, refused_quote) {
    for (const std::string_view value : {"\"open", "'open", "\"", "'", "\"mixed'", "'mixed\"", "\"a\"b\"",
                                         "'it''s'", "\"back\\slash\"", "'back\\slash'", "\"\\\""}) {
        CHECK_EQ(outcome("a: x\nb: " + std::string(value) + "\n"), std::string("quote@2"));
        CHECK_EQ(outcome("a:\n- y\n- " + std::string(value) + "\n"), std::string("quote@3"));
    }
}

QIVEN3_CASE(yaml, refused_typed) {
    for (const std::string_view value : {"true", "False", "TRUE", "null", "NULL", "none", "None", "~", "0", "12",
                                         "-3", "+7", "007", "1_000", "3.5", "-.5", "5.", "1e5", "2E-3", "1_0.0_1e1_0",
                                         "inf", "-Infinity", "NaN", "+nan"}) {
        CHECK_EQ(outcome("a: x\nb: " + std::string(value) + "\n"), std::string("typed@2"));
        CHECK_EQ(outcome("a:\n- " + std::string(value) + "\n"), std::string("typed@2"));
        // Quoted, it is text.
        CHECK_EQ(text_of("\"" + std::string(value) + "\""), std::optional<std::string>(value));
    }
}

QIVEN3_CASE(yaml, refused_too_large) {
    std::string file = "a: " + std::string(yaml::max_bytes - 4, 'x') + "\n";
    CHECK_EQ(file.size(), yaml::max_bytes);
    CHECK_EQ(outcome(file), std::string("ok"));
    file += "\n";
    CHECK_EQ(outcome(file), std::string("too_large@0"));
}

QIVEN3_CASE(yaml, first_fault_decides) {
    CHECK_EQ(outcome("a: x\nbad line\nb: \"open\n- z\n"), std::string("form@2"));
    CHECK_EQ(outcome("a: \"open\n\t\n"), std::string("quote@1"));
    // Within a line: utf8 before control, control before indented.
    CHECK_EQ(outcome(" \t\xff\n"), std::string("utf8@1"));
    CHECK_EQ(outcome(" \t\n"), std::string("control@1"));
}

QIVEN3_CASE(yaml, arena_too_small) {
    std::vector<std::byte> pages(16);
    Arena arena(pages);
    bool budget = false;
    try {
        static_cast<void>(yaml::read_flat("a: x\nb: y\nc: z\n", arena));
    } catch (const qiven3::memory::MemoryError& e) {
        budget = e.code == qiven3::memory::MemoryCode::budget;
    }
    CHECK(budget);
}
