// import/yaml: the flat YAML reader over synthetic records. Each form read
// (a text quoted both ways and plain, " #" kept as text, a list with its
// items, comments and empty lines between, a whole-line comment, the empty
// line); each refusal, naming its line: a byte not UTF-8, a tab or a CR, an
// indented line, a line of no form, a key twice, an item with no list
// open, a value or item beginning or ending with whitespace or a control,
// a quoted scalar unclosed or holding its quote or a backslash, a plain
// scalar the source system types (digits of any script too), a file past
// its bound; the first faulty line deciding; the fields kept past their
// bytes; thirty-one probes, each read as the source system reads it or
// refused by name; an arena too small, MemoryError budget.

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

// What reading text gives: its fields, as key='text' or key=['item',...],
// a space between; or the refusal's code and line.
std::string reading(std::string_view text) {
    std::vector<std::byte> pages(4096);
    Arena arena(pages);
    try {
        const yaml::Fields fields = yaml::read_flat(text, arena);
        std::string out;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            out += (i == 0 ? "" : " ") + std::string(fields.key(i)) + "=";
            if (!fields.list(i)) {
                out += "'" + std::string(fields.text(i)) + "'";
                continue;
            }
            out += "[";
            for (std::size_t k = 0; k < fields.count(i); ++k) {
                out += (k == 0 ? "'" : ",'") + std::string(fields.item(i, k)) + "'";
            }
            out += "]";
        }
        return out;
    } catch (const yaml::YamlError& e) {
        return std::string(yaml::name(e.code)) + "@" + std::to_string(e.line);
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
    // Nor these: a superscript two, full-width letters "inf", an Arabic
    // percent sign, the code point before a run of digits, underscores not
    // between two digits.
    for (const std::string_view plain : {"\xc2\xb2", "1\xc2\xb2", "\xef\xbd\x89\xef\xbd\x8e\xef\xbd\x86", "\xd9\xaa",
                                         "\xf0\x9e\x97\xb0", "1_\xd9\xa3_", "\xd9\xa3__\xd9\xa3"}) {
        CHECK_EQ(text_of(plain), std::optional<std::string>(plain));
    }
    // Controls and other characters inside a value, or inside its quotes,
    // are kept: the source system reads them so.
    for (const std::string_view kept : {"x\x0cy", "x\xe2\x80\x8b", "x\xef\xbb\xbf"}) {
        CHECK_EQ(text_of(kept), std::optional<std::string>(kept));
    }
    CHECK_EQ(text_of("\"\x0cx\x0c\""), std::optional<std::string>("\x0cx\x0c"));
    CHECK_EQ(text_of("'\xc2\xa0x'"), std::optional<std::string>("\xc2\xa0x"));
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
    // Decimal digits of any script, as Python's int and float read them:
    // Arabic-Indic, full-width, mixed with ASCII, signed with a point, with
    // an underscore, in an exponent, mathematical bold (four bytes), the
    // last run's last, a run's last, the Ol Onal run's first, Devanagari.
    for (const std::string_view value :
         {"\xd9\xa3", "\xef\xbc\x91", "1\xd9\xa3", "-\xef\xbc\x91.5", "\xd9\xa3_\xd9\xa3", "1e\xd9\xa3",
          "\xf0\x9d\x9f\x8e", "\xf0\x9f\xaf\xb9", "\xd9\xa9", "\xf0\x9e\x97\xb1", "+\xe0\xa5\xac."}) {
        CHECK_EQ(outcome("b: " + std::string(value) + "\n"), std::string("typed@1"));
        CHECK_EQ(outcome("a:\n- " + std::string(value) + "\n"), std::string("typed@2"));
        CHECK_EQ(text_of("'" + std::string(value) + "'"), std::optional<std::string>(value));
    }
}

QIVEN3_CASE(yaml, refused_edge) {
    // Whitespace by Python's str.isspace, which the source system's strip
    // trims, and the controls (C0, DEL, C1), at either end of a value or
    // an item, its spaces trimmed; quoted, outside the quotes.
    for (const std::string_view value :
         {"x\x0c", "\x0cx", "x\x0b", "x\x1f", "\x01x", "\x7fx", "x\xc2\x80", "x\xc2\x9f", "x\xc2\x85", "\xc2\xa0x",
          "x\xe1\x9a\x80", "x\xe2\x80\x80", "x\xe2\x80\x8a", "x\xe2\x80\xa9", "x\xe2\x80\xaf", "x\xe2\x81\x9f",
          "x\xe3\x80\x80", "\"x\"\x0c", "\x0c\"x\"", "\xc2\xa0", "  \x0c  "}) {
        CHECK_EQ(outcome("a: x\nb: " + std::string(value) + "\n"), std::string("edge@2"));
        CHECK_EQ(outcome("a:\n- y\n- " + std::string(value) + "  \n"), std::string("edge@3"));
    }
    CHECK_EQ(outcome(std::string_view("a: x\x00\n", 6)), std::string("edge@1"));
    CHECK_EQ(outcome(std::string_view("a:\n- \x00x\n", 8)), std::string("edge@2"));
    // A key whose value is only such is refused, never read as a list.
    CHECK_EQ(outcome("a: \x0c\n- x\n"), std::string("edge@1"));
    CHECK_EQ(outcome("a: \xe2\x80\xa8\n- x\n"), std::string("edge@1"));
    // Read before the quote and the type: the source system reads both.
    CHECK_EQ(outcome("a: \"x\x0c"), std::string("edge@1"));
    CHECK_EQ(outcome("a: 1\xc2\xa0\n"), std::string("edge@1"));
    CHECK_EQ(outcome("a:\n- true\x0b\n"), std::string("edge@2"));
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

// Thirty-one probes of where the reader and the source system's reader
// could part: each read as the source system reads it, or refused by name.
// After each refusal, what the source system reads instead.
QIVEN3_CASE(yaml, probes) {
    struct Probe {
        std::string_view bytes;
        std::string_view want;
    };
    const std::vector<Probe> probes = {
        {"a: \xd9\xa3\n", "typed@1"},            // 3, an int
        {"\xef\xbb\xbf" "a: x\n", "form@1"},     // nothing: a line skipped
        {"a:\n- x\n# c\n\n- y\nb: z\n", "a=['x','y'] b='z'"},
        {"a:\n-\n", "form@2"},                   // a=[]
        {"a:\n- \n", "form@2"},                  // a=[]
        {"a: \"\"\n", "a=''"},
        {"a: 5e-0_1\n", "typed@1"},              // 0.5
        {"a: \x0c\n- x\n", "edge@1"},            // a=['x']
        {"a: value\x0c\n", "edge@1"},            // a='value'
        {"a: \xef\xbf\xbe\n", "a='\xef\xbf\xbe'"},
        {"a: value\x1f\n", "edge@1"},            // a='value'
        {"a: \xef\xbc\x91\n", "typed@1"},        // 1
        {"a: PR #36\n", "a='PR #36'"},
        {"a: value\xe3\x80\x80\n", "edge@1"},    // a='value'
        {"a: inf\n", "typed@1"},                 // inf
        {"a:\n- x\x0c\n", "edge@2"},             // a=['x']
        {"a:\n- \xc2\xa0x\n", "edge@2"},         // a=['x']
        {"a:x\n", "form@1"},                     // a='x'
        {"a: x", "a='x'"},
        {"a: value\xe2\x80\xa8\n", "edge@1"},    // a='value'
        {"a: \xc2\xa0value\n", "edge@1"},        // a='value'
        {"a: \xc2\xa0\n- x\n", "edge@1"},        // a=['x']
        {"a: value\xc2\xa0\n", "edge@1"},        // a='value'
        {"a: -NaN\n", "typed@1"},                // nan
        {"a: value\xc2\x85\n", "edge@1"},        // a='value'
        {"a: \xef\xb7\x90\n", "a='\xef\xb7\x90'"},
        {std::string_view("a: x\x00y\n", 7), std::string_view("a='x\x00y'", 7)},
        {"a: \"x\"\x0c\n", "edge@1"},            // a='x'
        {"a: tRuE\n", "typed@1"},                // True
        {"a: 1_000\n", "typed@1"},               // 1000
        {"a: value\x0b\n", "edge@1"},            // a='value'
    };
    CHECK_EQ(probes.size(), std::size_t{31});
    for (const Probe& probe : probes) {
        CHECK_EQ(reading(probe.bytes), std::string(probe.want));
    }
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
