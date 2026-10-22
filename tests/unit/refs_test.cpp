// refs' cases: the code references' vectors (paths, names, subjects and
// the line suffix), the whitespace table's 29 code points, and each type of
// reference accepted and refused, each code on its field and the first in
// content's code order, evaluated by the compiler on std::allocator; at run
// time the limits at their bounds, in Chinese text where a limit counts
// characters, and every reference, subject and path of tests/fixtures/
// accepted.

#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "entry/refs.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"

namespace {

namespace refs = qiven3::entry::refs;
namespace canon = qiven3::canon;
namespace json = qiven3::json;
namespace fixtures = qiven3::test::fixtures;
using refs::Code;
using refs::Field;

#define DIGEST "hmac-sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ID "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define REVISION "0123456789abcdef0123456789abcdef01234567"

// The whitespace table: 29 code points, the controls among them too.
constexpr std::size_t spaces() {
    std::size_t n = 0;
    for (const refs::Range& r : refs::WHITESPACE) {
        n += static_cast<std::size_t>(r.last - r.first) + 1;
    }
    return n;
}
static_assert(spaces() == 29);
static_assert(refs::is_space(U' ') && refs::is_space(0x3000) && refs::is_space(0x1C) && refs::is_space(0x85));
static_assert(!refs::is_space(0x180E) && !refs::is_space(0x200B) && !refs::is_space(0xFEFF) && !refs::is_space(U'a'));
static_assert(refs::is_control(0) && refs::is_control(0x7F) && refs::is_control(0x9F) && !refs::is_control(0xA0));

// The line suffix, at the end only.
static_assert(refs::line_suffix(":12") && refs::line_suffix("x:12:5") && refs::line_suffix("x:12-30"));
static_assert(refs::line_suffix("x#L12") && refs::line_suffix("x#L12-L30") && refs::line_suffix("x#L12-30"));
static_assert(refs::line_suffix("a:b:7") && refs::line_suffix("x:1-2:3"));
static_assert(!refs::line_suffix("x:main") && !refs::line_suffix("x#12") && !refs::line_suffix("x:L12"));
static_assert(!refs::line_suffix("x-30") && !refs::line_suffix("L12") && !refs::line_suffix("x:12-L30"));
static_assert(!refs::line_suffix("x#L") && !refs::line_suffix("x:12a") && !refs::line_suffix("") &&
              !refs::line_suffix("12"));

// Paths: the vectors refused, then accepted.
static_assert(!refs::check_path("src/x.py:12") && !refs::check_path("src/x.py:12:5"));
static_assert(!refs::check_path("src/x.py:12-30") && !refs::check_path("src/x.py#L12"));
static_assert(!refs::check_path("../x.py") && !refs::check_path("/x.py") && !refs::check_path("a//b"));
static_assert(!refs::check_path("src/") && !refs::check_path("src/./x") && !refs::check_path("."));
static_assert(!refs::check_path("src/a\x07" "b.py") && !refs::check_path("src/a\xc2\x85" "b.py"));
static_assert(!refs::check_path("src/a\xe2\x80\xa8" "b.py") && !refs::check_path("src/a\xe2\x80\xa9" "b.py"));
static_assert(!refs::check_path("src\\x.py") && !refs::check_path("") && !refs::check_path("a/\x7f"));
static_assert(!refs::check_path("a\xff") && !refs::check_path("a\xed\xa0\x80"));
static_assert(refs::check_path("qiven3/entry.py") && refs::check_path("src/x.py:main"));
static_assert(refs::check_path("docs/a b#c.md") && refs::check_path("..x/.y") && refs::check_path("\xe8\xae\xb0.md"));

// Names.
static_assert(refs::check_name("qiven3.entry") && refs::check_name("Store.append"));
static_assert(refs::check_name("crate::ids::entry_id") && refs::check_name("entry_id"));
static_assert(!refs::check_name("f:12") && !refs::check_name("f#L12") && !refs::check_name("a b"));
static_assert(!refs::check_name("a\xe3\x80\x80" "b") && !refs::check_name("a\xc2\xa0") && !refs::check_name(""));
static_assert(!refs::check_name("a\x1c") && !refs::check_name("a\tb"));

// Subjects: the vectors refused, then accepted.
static_assert(!refs::check_subject("repo:qiven/src/x.py:12") && !refs::check_subject("repo:qiven/src/x.py#L12"));
static_assert(!refs::check_subject("repo:qiven/src/x.py#f:12") && !refs::check_subject("module:qiven3/entry.py:12"));
static_assert(!refs::check_subject("entity:src/x.py#L12") && !refs::check_subject("repo:qiven/../x.py"));
// One repository compile a constant evaluation: each is about half of
// MSVC's default step limit.
static_assert(!refs::check_subject("repo:qiven/"));
static_assert(!refs::check_subject("repo:-qiven") && !refs::check_subject("repo:qiven#f"));
static_assert(!refs::check_subject("repo:qiven/a#") && !refs::check_subject("repo:qiven/a b") &&
              !refs::check_subject("note:x"));
static_assert(!refs::check_subject("entity:") && !refs::check_subject("entity") && !refs::check_subject(":x"));
static_assert(refs::check_subject("scope:project:12") && refs::check_subject("repo:qiven-context-v3/qiven3/entry.py#entry_id"));
static_assert(refs::check_subject("repo:qiven") && refs::check_subject("entity:docker"));
static_assert(refs::check_subject("repo:qiven/src/x.py"));
static_assert(refs::check_subject("entry:" ID) && refs::check_subject("clause:ENG-110") &&
              refs::check_subject("repo:q/src/x.py:main"));

// A reference's first fault, parsed from canonical bytes.
constexpr std::optional<refs::RefError> fault_of(std::string_view text) {
    canon::Basic<> v;
    canon::parse(text, v);
    return refs::try_check_reference(v);
}
constexpr bool accepted(std::string_view text) { return !fault_of(text).has_value(); }
constexpr bool refused(std::string_view text, Code code, Field field) {
    const std::optional<refs::RefError> f = fault_of(text);
    return f && f->code == code && f->field == field;
}

// Each type accepted, with and without its optional fields.
static_assert(accepted(R"({"id":")" ID R"(","type":"entry"})"));
static_assert(accepted(R"({"command":"python -I probe_dup.py","digest":")" DIGEST R"(","type":"output"})"));

// No object; the type absent, of another JSON type, empty, off the four.
static_assert(refused("[]", Code::type, Field::none) && refused(R"("entry")", Code::type, Field::none));
static_assert(refused("{}", Code::missing, Field::type) && refused(R"({"type":1})", Code::type, Field::type));
static_assert(refused(R"({"type":""})", Code::empty, Field::type) && refused(R"({"type":"file"})", Code::syntax, Field::type));
// A key no type defines comes first; one another type defines needs a type.
static_assert(refused(R"({"type":"file","x":1})", Code::unknown_field, Field::none));
static_assert(refused(R"({"path":"a","type":"file"})", Code::syntax, Field::type));
static_assert(refused(R"({"id":")" ID R"(","path":"a","type":"entry"})", Code::unknown_field, Field::path));
static_assert(refused(R"({"digest":")" DIGEST R"(","id":")" ID R"(","type":"entry"})", Code::unknown_field, Field::digest));
static_assert(refused(R"({"anchor":"x","command":"c","digest":")" DIGEST R"(","type":"output"})", Code::unknown_field,
                      Field::anchor));
// missing, in the type's field order; before type and syntax.
static_assert(refused(R"({"type":"entry"})", Code::missing, Field::id));
static_assert(refused(R"({"digest":1,"path":"/x","type":"repo"})", Code::missing, Field::repository));
static_assert(refused(R"({"digest":1,"type":"output"})", Code::missing, Field::command));
static_assert(refused(R"({"client":"c","digest":")" DIGEST R"(","session":"s","type":"transcript"})", Code::missing,
                      Field::message));
// type, empty, too_long and syntax, each over the members in order.
static_assert(refused(R"({"id":1,"type":"entry"})", Code::type, Field::id));
static_assert(refused(R"({"digest":"","path":["a"],"repository":"-","type":"repo"})", Code::type, Field::path));
static_assert(refused(R"({"digest":"","path":"/","repository":"-","type":"repo"})", Code::empty, Field::digest));
static_assert(refused(R"({"id":"","type":"entry"})", Code::empty, Field::id));
static_assert(refused(R"({"id":"sha256:00","type":"entry"})", Code::syntax, Field::id));
static_assert(refused(R"({"digest":"sha256:00","path":"/","repository":"-","type":"repo"})", Code::syntax, Field::digest));
static_assert(refused(R"({"digest":")" DIGEST R"(","path":"/x.py","repository":"-","type":"repo"})", Code::syntax,
                      Field::path));
static_assert(refused(R"({"digest":")" DIGEST R"(","path":"x.py","repository":"-q","type":"repo"})", Code::syntax,
                      Field::repository));
// A repo reference's fields, each off its rule.
constexpr bool repo_refused(std::string_view member, Field field) {
    std::string text = R"({)";
    text += member;
    text += R"(,"digest":")" DIGEST R"(","path":"src/x.py","repository":"qiven","type":"repo"})";
    return refused(text, Code::syntax, field);
}
static_assert(repo_refused(R"("anchor":"L12")", Field::anchor) && repo_refused(R"("anchor":"L12-L30")", Field::anchor));
static_assert(repo_refused(R"("anchor":")" "0123456789012345678901234567890123456789012345678901234567890123x" R"(")",
                           Field::anchor));
static_assert(!fault_of(R"({"anchor":"ENG-110 L12","digest":")" DIGEST R"(","path":"src/x.py","repository":"q","type":"repo"})"));
constexpr bool name_refused(std::string_view name) {
    std::string text = R"({"digest":")" DIGEST R"(","name":")";
    text += name;
    text += R"(","path":"src/x.py","repository":"qiven","type":"repo"})";
    return refused(text, Code::syntax, Field::name);
}
static_assert(name_refused("f:12") && name_refused("a b") && name_refused("f#L3"));
static_assert(refused(R"({"digest":")" DIGEST R"(","path":"src/x.py:12","repository":"q","type":"repo"})", Code::syntax,
                      Field::path));
// A transcript's client, session and message.
bool transcript_refused(std::string_view client, std::string_view session, std::string_view message, Field f) {
    std::string text = R"({"client":")";
    text += client;
    text += R"(","digest":")" DIGEST R"(","message":")";
    text += message;
    text += R"(","session":")";
    text += session;
    text += R"(","type":"transcript"})";
    return f == Field::none ? accepted(text) : refused(text, Code::syntax, f);
}
// The fault's place: the member's key, or the reference's node.
static_assert(fault_of(R"({"type":"file","x":1})")->at == 3 && fault_of(R"({"type":"entry"})")->at == 0);
static_assert(fault_of(R"({"id":1,"type":"entry"})")->at == 1 && fault_of(R"({"id":"","type":"entry"})")->at == 1);
static_assert(refs::name(Code::too_long) == "too_long" && refs::name(Field::message) == "message" &&
              refs::name(Field::none).empty());

std::string repeat(std::string_view unit, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        out += unit;
    }
    return out;
}

const std::string ji = "\xe8\xae\xb0";  // a Chinese character, three bytes

std::optional<refs::RefError> run_fault(const std::string& text) {
    canon::Basic<> v;
    canon::parse(text, v);
    return refs::try_check_reference(v);
}

std::string output_of(const std::string& command) {
    return R"({"command":")" + command + R"(","digest":")" DIGEST R"(","type":"output"})";
}

}  // namespace

// A transcript's syntaxes compile past MSVC's default step limit: these
// run at run time.
QIVEN3_CASE(refs, transcript) {
    CHECK(transcript_refused("claude-code", "00000000-0000-4000-8000-000000000039",
                             "00000000-0000-4000-8000-000000000039", Field::none));
    CHECK(transcript_refused("zcode", "s", "a.b_c:d-1", Field::none));
    CHECK(transcript_refused("Claude", "s", "m", Field::client));
    CHECK(transcript_refused("c", "-s", "m", Field::session));
    CHECK(transcript_refused("c", "s", "m n", Field::message) && transcript_refused("c", "s", "m/n", Field::message));
    CHECK(transcript_refused("c", "s", "", Field::none) == false);
}

// Repo references that compile their repository's syntax beside others
// pass MSVC's step limit under /analyze: these run at run time.
QIVEN3_CASE(refs, repo) {
    CHECK(accepted(R"({"digest":")" DIGEST R"(","path":"qiven3/entry.py","repository":"qiven-context-v3","type":"repo"})"));
    CHECK(accepted(R"({"anchor":"Q14","digest":")" DIGEST R"(","name":"entry_id","path":"docs/a b#c.md",)"
                   R"("repository":"qiven","revision":")" REVISION R"(","type":"repo"})"));
    CHECK(refused(R"({"digest":")" DIGEST R"(","path":"src/x.py","repository":"q",)"
                  R"("revision":"ABCDEF0123456789abcdef0123456789abcdef01","type":"repo"})",
                  Code::syntax, Field::revision));
}

QIVEN3_CASE(refs, limits) {
    // A command of 400 characters, Chinese or Latin, and one past.
    CHECK(!run_fault(output_of(repeat(ji, 400))).has_value());
    CHECK(!run_fault(output_of(repeat("c", 400))).has_value());
    const std::optional<refs::RefError> long_command = run_fault(output_of(repeat(ji, 401)));
    REQUIRE(long_command.has_value());
    CHECK(long_command->code == Code::too_long && long_command->field == Field::command);
    // too_long before syntax: the digest off too.
    const std::optional<refs::RefError> both =
        run_fault(R"({"command":")" + repeat("c", 401) + R"(","digest":"x","type":"output"})");
    REQUIRE(both.has_value());
    CHECK(both->code == Code::too_long);
    // A path and a name of 200 characters, and 201.
    CHECK(refs::check_path(repeat(ji, 200)) && !refs::check_path(repeat(ji, 201)));
    CHECK(refs::check_path("a/" + repeat("b", 198)) && !refs::check_path("a/" + repeat("b", 199)));
    CHECK(refs::check_name(repeat(ji, 200)) && !refs::check_name(repeat("n", 201)));
    // A subject of 160 characters, and 161; a repository of 100, and 101.
    CHECK(refs::check_subject("entity:" + repeat(ji, 153)) && !refs::check_subject("entity:" + repeat(ji, 154)));
    CHECK(refs::check_subject("repo:" + repeat("r", 100)) && !refs::check_subject("repo:" + repeat("r", 101)));
    // An anchor of 64 characters, and 65; a message and a session of 128, and 129.
    const auto repo = [](const std::string& anchor) {
        return R"({"anchor":")" + anchor + R"(","digest":")" DIGEST R"(","path":"a","repository":"q","type":"repo"})";
    };
    CHECK(!run_fault(repo(repeat(ji, 64))).has_value());
    CHECK(run_fault(repo(repeat(ji, 65)))->field == Field::anchor);
    const auto transcript = [](const std::string& session, const std::string& message) {
        return R"({"client":"c","digest":")" DIGEST R"(","message":")" + message + R"(","session":")" + session +
               R"(","type":"transcript"})";
    };
    CHECK(!run_fault(transcript(repeat("s", 128), repeat("m", 128))).has_value());
    CHECK(run_fault(transcript(repeat("s", 128), repeat("m", 129)))->field == Field::message);
    CHECK(run_fault(transcript(repeat("s", 129), repeat("m", 128)))->field == Field::session);
    // check_reference throws the first fault.
    bool thrown = false;
    try {
        canon::Basic<> v;
        canon::parse(R"({"type":"entry"})", v);
        refs::check_reference(v);
    } catch (const refs::RefError& e) {
        thrown = e.code == Code::missing && e.field == Field::id;
    }
    CHECK(thrown);
}

QIVEN3_CASE(refs, fixtures) {
    canon::Basic<> all;
    fixtures::load_entries(all);
    std::size_t references = 0;
    std::size_t subjects = 0;
    std::size_t paths = 0;
    for (std::size_t n = 0; n < fixtures::entry_count(all); ++n) {
        const fixtures::EntryView e = fixtures::entry(all, n);
        if (!e.content) {
            continue;
        }
        canon::Basic<> v;
        canon::parse(*e.content, v);
        const auto reference = [&](json::Index i) {
            const std::optional<refs::RefError> f = refs::try_check_reference(v, i);
            if (f) {
                std::cout << "refs.fixtures: " << e.name << ": " << refs::name(f->code) << " " << refs::name(f->field) << "\n";
            }
            CHECK(!f.has_value());
            ++references;
        };
        for (const std::string_view list : {"evidence", "source"}) {
            if (const std::optional<json::Index> l = v.find(0, list)) {
                for (json::Index i = *l + 1; i < v.next(*l); i = v.next(i)) {
                    reference(i);
                }
            }
        }
        for (const std::string_view one : {"message", "object"}) {
            if (const std::optional<json::Index> r = v.find(0, one); r && v.kind(*r) == json::Kind::object) {
                reference(*r);
            }
        }
        if (const std::optional<json::Index> s = v.find(0, "subject")) {
            CHECK(refs::check_subject(v.text(*s)));
            ++subjects;
        }
        for (const std::string_view holder : {"trigger", "applies_to"}) {
            const std::optional<json::Index> h = v.find(0, holder);
            const std::optional<json::Index> p = h ? v.find(*h, "paths") : std::nullopt;
            for (json::Index i = p ? *p + 1 : 0; p && i < v.next(*p); i = v.next(i)) {
                CHECK(refs::check_path(v.text(i)));
                ++paths;
            }
        }
    }
    std::cout << "refs.fixtures: " << references << " references, " << subjects << " subjects, " << paths
              << " paths, each accepted\n";
    CHECK(references > 0 && subjects > 0);
}
