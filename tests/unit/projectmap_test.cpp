// projectmap's cases. Evaluated by the compiler: the registry's and the
// marks file's syntaxes, os.path.isabs on each system over the samples
// CPython 3.15 answered, the control characters; the registry read in any
// layout and each of its codes, the first in order deciding; the marks file
// read, its codes, its bytes written back the same, a version it does not
// name with no mark; Marks built by put, its refusals, and its bytes; the
// map's rule against a view in memory, an object naming a record's root
// and not; and the entry reference's digest, taken in a map's object and
// unknown_field in its source. At run time: the parser's thrown faults.

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "entry/content.hpp"
#include "entry/header.hpp"
#include "entry/projectmap.hpp"
#include "entry/projectmap_files.hpp"
#include "entry/served.hpp"
#include "qiven3/canon.hpp"
#include "runner/runner.hpp"
#include "support/fixtures/view.hpp"

namespace {

namespace projectmap = qiven3::entry::projectmap;
namespace header = qiven3::entry::header;
namespace content = qiven3::entry::content;
namespace served = qiven3::entry::served;
namespace canon = qiven3::canon;
using projectmap::FileCode;
using projectmap::FileError;
using projectmap::System;
using qiven3::ParseCode;
using qiven3::test::fixtures::MemoryView;

#define HEX "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define ID_A "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define ID_B "sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
#define ID_C "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
#define ID_M "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
#define ID_1 "sha256:1111111111111111111111111111111111111111111111111111111111111111"
#define COMMIT "0123456789abcdef0123456789abcdef01234567"
#define AT "2026-10-17T12:06:00.000Z"

// The syntaxes.
static_assert(projectmap::repository("qiven-context-v3") && projectmap::repository("a.b_c-9"));
static_assert(!projectmap::repository("-x") && !projectmap::repository("a/b") && !projectmap::repository(""));
static_assert(projectmap::repository(std::string(100, 'a')) && !projectmap::repository(std::string(101, 'a')));
static_assert(projectmap::ref("main") && projectmap::ref("feature/x.y_z-1") && !projectmap::ref("/main"));
static_assert(projectmap::ref(std::string(200, 'r')) && !projectmap::ref(std::string(201, 'r')) &&
              !projectmap::ref("a b"));
static_assert(projectmap::version_id("sha256:" HEX) && !projectmap::version_id("sha256:" HEX "0") &&
              !projectmap::version_id("sha256:0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
static_assert(projectmap::commit_id(COMMIT) && !projectmap::commit_id(COMMIT "8") &&
              !projectmap::commit_id("0123456789ABCDEF0123456789abcdef01234567"));

// os.path.isabs, as ntpath and posixpath answered each sample.
struct SampleView {
    std::string_view path;
    bool windows;
    bool posix;
};
constexpr std::array<SampleView, 19> SAMPLES = {{
    {"C:\\x", true, false},  {"C:/x", true, false},   {"C:x", false, false},       {"/x", false, true},
    {"\\x", false, false},   {"\\\\s\\h", true, false}, {"//s/h", true, true},     {"\xc3\xa9:\\x", true, false},
    {"1:/", true, false},    {"C:", false, false},    {"\\\\", true, false},       {"x/y", false, false},
    {"", false, false},      {"/", false, true},      {"/\\", true, true},         {"\\/", true, false},
    {"C:\\", true, false},   {":\\x", false, false},  {"ab:/", false, false},
}};
constexpr bool isabs_samples() {
    for (const SampleView& s : SAMPLES) {
        if (projectmap::absolute(s.path, System::windows) != s.windows ||
            projectmap::absolute(s.path, System::posix) != s.posix) {
            return false;
        }
    }
    return true;
}
static_assert(isabs_samples());
static_assert(projectmap::control("a\x1f") && projectmap::control("\x7f") && projectmap::control("\xc2\x85") &&
              projectmap::control(std::string_view("a\0b", 3)));
static_assert(!projectmap::control("\xc2\xa0") && !projectmap::control("caf\xc3\xa9") && !projectmap::control("/x y"));

// The registry: a fault, or none.
constexpr std::optional<FileError> registry(std::string_view data, System system = System::posix) {
    projectmap::Registry<> r;
    return projectmap::try_parse_registry(data, r, system);
}
constexpr FileError parse_fault(ParseCode p) { return FileError{FileCode::parse, p}; }
constexpr FileError shape(FileCode c) { return FileError{c, ParseCode::syntax}; }

// Any layout, CRLF and indented, and its accessors.
constexpr std::string_view CRLF_REGISTRY =
    "{\r\n  \"qiven-context-v3\": {\r\n    \"path\": \"C:\\\\work\\\\v3\",\r\n    \"ref\": \"cognition-core\"\r\n  },"
    "\r\n  \"qiven-other\": {\"ref\": \"main\", \"path\": \"//server/share/other\"}\r\n}\r\n";
constexpr bool crlf_registry() {
    const projectmap::Registry<> r = projectmap::parse_registry(CRLF_REGISTRY, std::allocator<char>(), System::windows);
    return r.size() == 2 && r.name(0) == "qiven-context-v3" && r.path(0) == "C:\\work\\v3" &&
           r.ref(0) == "cognition-core" && r.find("qiven-other") == std::optional<std::size_t>(1) &&
           r.path(1) == "//server/share/other" && r.ref(1) == "main" && !r.find("qiven").has_value();
}
static_assert(crlf_registry());
static_assert(registry(CRLF_REGISTRY, System::posix) == shape(FileCode::syntax));
static_assert(!registry("{}") && !registry(R"({"r":{"path":"/home/r","ref":"main"}})" "\n"));

// Its codes: canon's first, then unknown_field, missing, type, syntax.
static_assert(registry("\xef\xbb\xbf{}") == parse_fault(ParseCode::bom));
static_assert(registry(R"({"r":{"path":"/a","path":"/b","ref":"m"}})") == parse_fault(ParseCode::duplicate_key));
static_assert(registry(R"({"bad name":{"path":"/a","ref":"m"}})") == parse_fault(ParseCode::key_syntax));
static_assert(registry(R"({"r":{"Path":"/a","ref":"m"}})") == parse_fault(ParseCode::key_syntax));
static_assert(registry(R"({"r":{"branch":"m","path":"/a","ref":"m"}})") == shape(FileCode::unknown_field));
static_assert(registry(R"({"r":{"path":"/a"}})") == shape(FileCode::missing));
static_assert(registry(R"({"r":{"path":1,"ref":"m"}})") == shape(FileCode::type));
static_assert(registry(R"({"r":"x"})") == shape(FileCode::type) && registry("[]") == shape(FileCode::type));
static_assert(registry(R"({"r":{"path":"x/y","ref":"m"}})") == shape(FileCode::syntax));
static_assert(registry(R"({"r":{"path":"/a\u0001","ref":"m"}})") == shape(FileCode::syntax));
static_assert(registry(R"({"r":{"path":"/a\u0085","ref":"m"}})") == shape(FileCode::syntax));
static_assert(registry(R"({"r":{"path":"/a","ref":"/m"}})") == shape(FileCode::syntax));
static_assert(registry(R"({"r":{"path":"","ref":"m"}})") == shape(FileCode::syntax));
// The first code in order decides, wherever it stands.
static_assert(registry(R"({"a":{"path":2,"ref":"m"},"b":{"path":"/b","ref":"m","x":1}})") ==
              shape(FileCode::unknown_field));
static_assert(registry(R"({"a":{"path":"rel","ref":"m"},"b":{"ref":"m"}})") == shape(FileCode::missing));

// The marks file: a fault, or none.
constexpr std::optional<FileError> marks(std::string_view data) {
    projectmap::Marks<> m;
    return projectmap::try_parse_marks(data, m);
}
#define MARKS_LINE                                                                                              \
    R"({"at":")" AT R"(","marks":{"sha256:)" HEX R"(":"fresh",")" ID_A R"(":"stale"},"read":{"qiven-context-v3":")" \
        COMMIT R"("}})"
constexpr std::string_view MARKS_FILE = MARKS_LINE "\n";

// Whether a mark found is served's map_mark of mark at AT.
constexpr bool is(const std::optional<served::MapMark>& found, served::Mark mark) {
    const std::optional<served::MapMark> want = served::map_mark(mark, AT);
    return found && want && found->mark == want->mark && found->when() == want->when();
}

// Read, each version's mark with the file's at, none for a version it does
// not name, and written back the same.
constexpr bool marks_read() {
    const projectmap::Marks<> m = projectmap::parse_marks(MARKS_FILE);
    std::string out;
    projectmap::marks_bytes(m, out);
    return m.size() == 2 && m.read_size() == 1 && m.at() == std::optional<std::string_view>(AT) &&
           is(m.find("sha256:" HEX), served::Mark::fresh) && is(m.find(ID_A), served::Mark::stale) &&
           !m.find(ID_1).has_value() && m.commit("qiven-context-v3") == std::optional<std::string_view>(COMMIT) &&
           !m.commit("qiven-other").has_value() && out == MARKS_FILE && out.capacity() >= out.size();
}
static_assert(marks_read());
static_assert(!marks(R"({"at":")" AT R"(","marks":{},"read":{}})" "\n"));

// Its codes.
static_assert(marks(MARKS_LINE) == parse_fault(ParseCode::not_canonical));
static_assert(marks(MARKS_LINE "\r\n") == parse_fault(ParseCode::not_canonical));
static_assert(marks(MARKS_LINE "\n\n") == parse_fault(ParseCode::not_canonical));
static_assert(marks(R"({"marks":{},"at":")" AT R"(","read":{}})" "\n") == parse_fault(ParseCode::not_canonical));
static_assert(marks(R"({"at":")" AT R"(","marks":{"sha256:x":"fresh"},"read":{}})" "\n") ==
              parse_fault(ParseCode::key_syntax));
static_assert(marks(R"({"at":")" AT R"(","marks":{},"read":{"-r":")" COMMIT R"("}})" "\n") ==
              parse_fault(ParseCode::key_syntax));
static_assert(marks(R"({"At":")" AT R"(","marks":{},"read":{}})" "\n") == parse_fault(ParseCode::key_syntax));
static_assert(marks(R"({"at":")" AT R"(","extra":1,"marks":{},"read":{}})" "\n") == shape(FileCode::unknown_field));
static_assert(marks(R"({"at":")" AT R"(","marks":{}})" "\n") == shape(FileCode::missing));
static_assert(marks(R"({"at":")" AT R"(","marks":[],"read":{}})" "\n") == shape(FileCode::type));
static_assert(marks(R"({"at":")" AT R"(","marks":{")" ID_A R"(":1},"read":{}})" "\n") == shape(FileCode::type));
static_assert(marks(R"({"at":")" AT R"(","marks":{")" ID_A R"(":"unchecked"},"read":{}})" "\n") ==
              shape(FileCode::syntax));
static_assert(marks(R"({"at":"2026-10-17T12:06:00Z","marks":{},"read":{}})" "\n") == shape(FileCode::syntax));
static_assert(marks(R"({"at":")" AT R"(","marks":{},"read":{"r":"0123"}})" "\n") == shape(FileCode::syntax));
static_assert(marks(R"({"at":1,"marks":{},"read":{"r":"0123"}})" "\n") == shape(FileCode::type));

// Marks built by put: sorted, each refusal leaving it as it was, and its
// bytes.
constexpr bool marks_built() {
    projectmap::Marks<> m;
    const bool refused = !m.set_at("12:06") && !m.put("sha256:x", served::Mark::fresh) &&
                         !m.put(ID_A, served::Mark::unchecked) && !m.put_read("-r", COMMIT) &&
                         !m.put_read("r", "0123") && m.size() == 0 && m.read_size() == 0;
    std::string out;
    const bool put = m.set_at(AT) && m.put(ID_A, served::Mark::stale) && m.put("sha256:" HEX, served::Mark::fresh) &&
                     !m.put(ID_A, served::Mark::fresh) && m.put_read("qiven-context-v3", COMMIT) &&
                     !m.put_read("qiven-context-v3", COMMIT);
    projectmap::marks_bytes(m, out);
    return refused && put && m.row(0).id.view() == "sha256:" HEX && out == MARKS_FILE &&
           out.size() == 7 + 24 + 11 + 81 + 1 + 81 + 10 + 1 + 16 + 3 + 40 + 1 + 2 + 1;
}
static_assert(marks_built());
constexpr bool empty_bytes() {
    projectmap::Marks<> m;
    static_cast<void>(m.set_at(AT));
    std::string out;
    projectmap::marks_bytes(m, out);
    return out == R"({"at":")" AT R"(","marks":{},"read":{}})" "\n" && out.size() == 7 + 24 + 11 + 10 + 2 + 1;
}
static_assert(empty_bytes());

// The map's rule. A view holding a claim A (a root), its revision B, a
// check C of A, each a header of a kind, an id and a target.
constexpr std::string head(std::string_view kind, std::string_view id, std::string_view target = "") {
    std::string line = R"({"basis":"claimed","id":")" + std::string(id) + R"(","kind":")" + std::string(kind) + "\"";
    if (!target.empty()) {
        line += R"(,"target":")" + std::string(target) + "\"";
    }
    return line + "}";
}
#define DIGEST "hmac-sha256:" HEX
constexpr std::string map_of(std::string_view object) {
    return R"({"object":)" + std::string(object) + "}";
}
constexpr std::string entry_object(std::string_view id) {
    return R"({"digest":")" DIGEST R"(","id":")" + std::string(id) + R"(","type":"entry"})";
}
constexpr std::optional<projectmap::Code> rule(std::string_view kind, const std::string& text) {
    MemoryView view;
    view.put(header::parse_line(head("claim", ID_A)), std::nullopt);
    view.put(header::parse_line(head("claim", ID_B, ID_A)), std::nullopt);
    view.put(header::parse_line(head("check", ID_C, ID_A)), std::nullopt);
    const header::Header<> h = header::parse_line(head(kind, ID_M));
    canon::Basic<> v;
    canon::parse(text, v);
    return projectmap::try_check_map(h, v, view);
}
static_assert(!rule("map", map_of(entry_object(ID_A))));
static_assert(rule("map", map_of(entry_object(ID_B))) == projectmap::Code::reference);
static_assert(rule("map", map_of(entry_object(ID_C))) == projectmap::Code::reference);
// An id not held is validate's; a repo object, another kind, pass.
static_assert(!rule("map", map_of(entry_object("sha256:" HEX))));
static_assert(!rule("map", map_of(R"({"digest":")" DIGEST R"(","path":"a.py","repository":"r","type":"repo"})")));
static_assert(!rule("claim", map_of(entry_object(ID_B))));

// The entry reference's digest: a map's object takes it; its source, as
// any other place, refuses it unknown_field.
#define MAP_HEAD                                                                                          \
    R"("admit":{"mistake":"A reader would trust it.","underivable":"Said in a session."},"band":"high",)" \
    R"("contract":"c","does":"d","episode":{"client":"claude-code","session":"s-1"},)"
constexpr std::optional<content::ContentError> map_content(const std::string& text) {
    header::FieldsView f;
    f.scope = "project:qiven";
    f.lane = "reviewer";
    f.kind = "map";
    f.writer = "reviewer/s70";
    f.basis = "derived";
    f.at = AT;
    f.id = ID_M;
    const header::Header<> h = header::make(f);
    canon::Basic<> v;
    canon::parse(text, v);
    return content::try_check(h, v);
}

}  // namespace

// Past the compiler's step limit, so at run time.
QIVEN3_CASE(projectmap, entry_digest) {
    CHECK(!map_content("{" MAP_HEAD R"("object":)" + entry_object(ID_A) + R"(,"object_type":"item"})").has_value());
    const std::optional<content::ContentError> source =
        map_content("{" MAP_HEAD R"("object":)" + entry_object(ID_A) + R"(,"object_type":"item","source":[)" +
                    entry_object(ID_B) + "]}");
    CHECK(source.has_value() && source->code == content::Code::unknown_field &&
          source->path.view() == "source[0].digest");
}

QIVEN3_CASE(projectmap, thrown) {
    bool registry_thrown = false;
    try {
        static_cast<void>(projectmap::parse_registry(R"({"r":{"path":"/a"}})"));
    } catch (const FileError& e) {
        registry_thrown = e == shape(FileCode::missing);
    }
    CHECK(registry_thrown);
    bool marks_thrown = false;
    try {
        static_cast<void>(projectmap::parse_marks(MARKS_LINE));
    } catch (const FileError& e) {
        marks_thrown = e == parse_fault(ParseCode::not_canonical);
    }
    CHECK(marks_thrown);
    // Marks with no at: MapError at, the caller's string as it was.
    bool no_at = false;
    std::string out = "kept";
    try {
        projectmap::marks_bytes(projectmap::Marks<>(), out);
    } catch (const projectmap::MapError& e) {
        no_at = e.code == projectmap::Code::at && out == "kept";
    }
    CHECK(no_at);
    MemoryView view;
    view.put(header::parse_line(head("check", ID_C, ID_A)), std::nullopt);
    const header::Header<> h = header::parse_line(head("map", ID_M));
    canon::Basic<> v;
    canon::parse(map_of(entry_object(ID_C)), v);
    bool rule_thrown = false;
    try {
        projectmap::check_map(h, v, view);
    } catch (const projectmap::MapError& e) {
        rule_thrown = e.code == projectmap::Code::reference;
    }
    CHECK(rule_thrown);
    CHECK_EQ(projectmap::name(projectmap::Code::reference), std::string_view("reference"));
    CHECK_EQ(projectmap::name(FileCode::unknown_field), std::string_view("unknown_field"));
}
