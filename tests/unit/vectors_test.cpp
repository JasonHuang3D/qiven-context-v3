// FA5's vectors case: tests/vectors/ids.json, one canonical JSON object a
// line, each a vector the format pins with its inputs and its expected
// bytes, read whole, every vector recomputed by the code built so far: the
// framing and A's framed header and id (digest); the content digests from
// the text shown (canon, digest); each entry's content, its keyed digest,
// its framed header, its id and its body record, the entry admitted
// (header, body, check), and the fixtures' entry of its name the same; the
// evidence and message digests; the parser's refusals and acceptances
// (canon); the validator's refusals of A (check); a usage at every limit
// (content); the mapped file's versions, the digests and the marks
// (projectmap); M1's and M2's blocks and their costs (fold, labels,
// served). The head line counts the vectors: a line short or extra, a
// group no reader knows, one with no vector, a member its reader does not
// read or a member missing fails the reading, so that none is left unread.
// Then each vector planted wrong, its expected value changed, fails its
// reader, and the file's own plants fail the reading.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "qiven3/canon.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fixtures/fixtures.hpp"
#include "support/fixtures/vectors.hpp"

namespace {

namespace canon = qiven3::canon;
namespace fixtures = qiven3::test::fixtures;
namespace json = qiven3::json;
using qiven3::test::vectors::Context;
using qiven3::test::vectors::Entry;
using qiven3::test::vectors::from_hex;
using qiven3::test::vectors::integer;
using qiven3::test::vectors::maybe_text;
using qiven3::test::vectors::size;
using qiven3::test::vectors::text;
using qiven3::test::vectors::Value;
namespace read = qiven3::test::vectors;

// A group's members: each by name, and whether each vector carries it.
struct MemberView {
    std::string_view name;
    bool required = true;
};
constexpr std::array<MemberView, 3> HEAD = {{{"group"}, {"name"}, {"vectors"}}};
constexpr std::array<MemberView, 4> FRAMING = {{{"framed"}, {"group"}, {"name"}, {"value", false}}};
constexpr std::array<MemberView, 6> FRAMED = {{{"framed"}, {"group"}, {"header"}, {"id"}, {"name"}, {"size"}}};
constexpr std::array<MemberView, 7> DIGESTS = {
    {{"bytes"}, {"digest"}, {"group"}, {"key"}, {"name"}, {"shown"}, {"size"}}};
constexpr std::array<MemberView, 10> ENTRY = {{{"body", false},
                                               {"body_size", false},
                                               {"content", false},
                                               {"content_size", false},
                                               {"framed_size"},
                                               {"group"},
                                               {"header"},
                                               {"id"},
                                               {"key", false},
                                               {"name"}}};
constexpr std::array<MemberView, 6> EVIDENCE = {{{"data"}, {"digest"}, {"entry"}, {"field"}, {"group"}, {"name"}}};
constexpr std::array<MemberView, 4> PARSE = {{{"bytes"}, {"code"}, {"group"}, {"name"}}};
constexpr std::array<MemberView, 6> CHECK_ = {{{"code"}, {"group"}, {"header"}, {"name"}, {"part"}, {"record"}}};
constexpr std::array<MemberView, 9> USAGE = {{{"cited"},
                                              {"client"},
                                              {"content"},
                                              {"corrected"},
                                              {"group"},
                                              {"name"},
                                              {"served"},
                                              {"session"},
                                              {"size"}}};
constexpr std::array<MemberView, 7> FILE_ = {
    {{"bytes"}, {"group"}, {"name"}, {"path"}, {"repository"}, {"size"}, {"version"}}};
constexpr std::array<MemberView, 6> MAP = {{{"digest", false}, {"entry"}, {"group"}, {"mark"}, {"name"}, {"version"}}};
constexpr std::array<MemberView, 8> BLOCK = {
    {{"at"}, {"block"}, {"cost"}, {"entry"}, {"group"}, {"mark"}, {"name"}, {"record"}}};

// A group: its members, and the member a plant changes, its expected value.
struct GroupView {
    std::string_view name;
    std::span<const MemberView> members;
    std::string_view planted;
};
constexpr std::array<GroupView, 11> GROUPS = {{
    {"framing", FRAMING, "framed"},
    {"framed_header", FRAMED, "id"},
    {"entry", ENTRY, "id"},
    {"content_digest", DIGESTS, "digest"},
    {"evidence", EVIDENCE, "digest"},
    {"parse", PARSE, "code"},
    {"check", CHECK_, "code"},
    {"usage", USAGE, "size"},
    {"file", FILE_, "size"},
    {"map", MAP, "mark"},
    {"block", BLOCK, "cost"},
}};

const GroupView* group_of(std::string_view name) {
    for (const GroupView& g : GROUPS) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

// The first member a line carries that its group does not read, or the
// first it lacks that its group requires; empty when neither.
std::string members_fault(const Value& v, const GroupView& g) {
    for (json::Index k = 1; k < v.next(0); k = v.next(k + 1)) {
        bool known = false;
        for (const MemberView& m : g.members) {
            known = known || m.name == v.text(k);
        }
        if (!known) {
            return "a member no reader reads: " + std::string(v.text(k));
        }
    }
    for (const MemberView& m : g.members) {
        if (m.required && !v.find(0, m.name)) {
            return "a member missing: " + std::string(m.name);
        }
    }
    return {};
}

// One vector line's reading: its group's members, then its recomputation;
// empty when it holds.
std::string verify(Context& c, std::string_view line) {
    try {
        Value v;
        if (const std::optional<qiven3::ParseCode> code = canon::try_parse(line, v)) {
            return "the line refused: " + std::string(qiven3::name(*code));
        }
        if (v.kind(0) != json::Kind::object) {
            return "the line not an object";
        }
        const GroupView* g = group_of(text(v, "group"));
        if (g == nullptr) {
            return "a group no reader knows: " + std::string(text(v, "group"));
        }
        if (std::string f = members_fault(v, *g); !f.empty()) {
            return f;
        }
        using Reader = std::string (*)(Context&, const Value&);
        constexpr std::array<Reader, GROUPS.size()> readers = {
            read::framing,  read::framed_header, read::entry, read::content_digest, read::evidence, read::parse,
            read::check_line, read::usage, read::file,  read::map,            read::block};
        return readers.at(static_cast<std::size_t>(g - GROUPS.data()))(c, v);
    } catch (const std::exception& e) {
        return std::string("thrown: ") + e.what();
    } catch (...) {
        return "an error thrown";
    }
}

// A file's reading: the vectors its head counts, those read and held, and
// each fault.
struct Report {
    std::size_t vectors = 0;
    std::size_t read = 0;
    std::vector<std::string> faults;
};

// The lines of data, each ended by LF; none when the last is not.
std::optional<std::vector<std::string_view>> lines_of(std::string_view data) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at < data.size()) {
        const std::size_t lf = data.find('\n', at);
        if (lf == std::string_view::npos) {
            return std::nullopt;
        }
        out.push_back(data.substr(at, lf - at));
        at = lf + 1;
    }
    return out;
}

// The vectors' entries and files, which other vectors name, first.
void gather(Context& c, std::span<const std::string_view> lines) {
    c.entries.clear();
    c.files.clear();
    for (const std::string_view line : lines) {
        Value v;
        if (canon::try_parse(line, v) || v.kind(0) != json::Kind::object || !v.find(0, "group")) {
            continue;
        }
        if (maybe_text(v, "group") == "entry") {
            c.entries.push_back(Entry{std::string(text(v, "name")), std::string(text(v, "header")),
                                      std::string(maybe_text(v, "key").value_or("")),
                                      std::string(maybe_text(v, "content").value_or(""))});
        } else if (maybe_text(v, "group") == "file") {
            c.files.emplace_back(integer(v, "version"), from_hex(text(v, "bytes")));
        }
    }
}

Report read_all(Context& c, std::string_view data) {
    Report r;
    const std::optional<std::vector<std::string_view>> lines = lines_of(data);
    if (!lines || lines->empty()) {
        r.faults.emplace_back("the file not lines each ended by LF");
        return r;
    }
    try {
        Value head;
        canon::parse(lines->front(), head);
        const std::string f = text(head, "group") == "head" ? members_fault(head, GroupView{"head", HEAD, ""})
                                                             : "the first line not the head";
        if (!f.empty()) {
            r.faults.push_back(f);
            return r;
        }
        r.vectors = size(head, "vectors");
    } catch (...) {
        r.faults.emplace_back("the head unread");
        return r;
    }
    const std::span<const std::string_view> body_lines = std::span<const std::string_view>(*lines).subspan(1);
    if (body_lines.size() != r.vectors) {
        r.faults.push_back("the head counts " + std::to_string(r.vectors) + ", the file holds " +
                           std::to_string(body_lines.size()));
    }
    gather(c, body_lines);
    std::array<std::size_t, GROUPS.size()> seen{};
    for (std::size_t i = 0; i < body_lines.size(); ++i) {
        const std::string f = verify(c, body_lines[i]);
        if (!f.empty()) {
            r.faults.push_back("line " + std::to_string(i + 2) + ": " + f);
            continue;
        }
        Value v;
        canon::parse(body_lines[i], v);
        ++seen.at(static_cast<std::size_t>(group_of(text(v, "group")) - GROUPS.data()));
        ++r.read;
    }
    for (std::size_t g = 0; g < GROUPS.size(); ++g) {
        if (seen.at(g) == 0) {
            r.faults.push_back("no vector of group " + std::string(GROUPS.at(g).name));
        }
    }
    return r;
}

// A line with its member's expected value changed: a text's last
// character, or an integer's last digit; empty when it has no such member.
std::string planted(std::string_view line, std::string_view member) {
    std::string out(line);
    const std::string key = "\"" + std::string(member) + "\":";
    std::size_t at = out.find("," + key);
    if (at == std::string::npos) {
        at = out.find("{" + key);
    }
    if (at == std::string::npos) {
        return {};
    }
    at += 1 + key.size();
    if (out.at(at) == '"') {
        char& c = out.at(out.find('"', at + 1) - 1);
        c = c == '0' ? '1' : '0';
    } else {
        std::size_t end = at;
        while (end < out.size() && out.at(end) >= '0' && out.at(end) <= '9') {
            ++end;
        }
        char& c = out.at(end - 1);
        c = c == '9' ? '0' : static_cast<char>(c + 1);
    }
    return out;
}

std::string_view ids() {
    const std::optional<std::string_view> data = fixtures::vector_bytes("ids.json");
    if (!data) {
        throw std::runtime_error("tests/vectors/ids.json not in the build");
    }
    return *data;
}

}  // namespace

QIVEN_CASE(vectors, ids) {
    CHECK_EQ(fixtures::vector_file_count(), 1U);
    CHECK_EQ(fixtures::vector_file_name(0), std::string_view("ids.json"));
    Context c;
    fixtures::load_entries(c.fixtures);
    const Report r = read_all(c, ids());
    for (const std::string& f : r.faults) {
        std::cout << "vectors.ids: " << f << "\n";
    }
    CHECK(r.faults.empty());
    CHECK_EQ(r.read, r.vectors);
    CHECK_EQ(r.vectors, 81U);
    std::cout << "vectors.ids: " << r.read << " of " << r.vectors << " vectors read and recomputed, " << c.entries.size()
              << " entries, " << c.files.size() << " file versions\n";
}

QIVEN_CASE(vectors, planted) {
    Context c;
    fixtures::load_entries(c.fixtures);
    const std::string_view data = ids();
    const std::vector<std::string_view> lines = lines_of(data).value_or(std::vector<std::string_view>{});
    REQUIRE(lines.size() > 1);
    gather(c, std::span<const std::string_view>(lines).subspan(1));
    // Each vector, its expected value changed, fails its reader.
    std::size_t caught = 0;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        Value v;
        canon::parse(lines[i], v);
        const GroupView* g = group_of(text(v, "group"));
        REQUIRE(g != nullptr);
        const std::string wrong = planted(lines[i], g->planted);
        // Caught by its reader's comparison of that value, no earlier fault.
        const std::string f = wrong.empty() ? std::string() : verify(c, wrong);
        const bool by_value = f.starts_with(std::string(g->planted) + ": ");
        if (!by_value) {
            std::cout << "vectors.planted: line " << i + 1 << " not caught by its value: " << f << "\n";
        }
        caught += by_value ? 1U : 0U;
    }
    CHECK_EQ(caught, lines.size() - 1);
    // The file's own: its last line dropped, a group no reader knows, a
    // member no reader reads, a group left with no vector, and the head's
    // count changed; each a fault of the reading.
    const std::string whole(data);
    const std::string dropped = whole.substr(0, whole.rfind('\n', whole.size() - 2) + 1);
    std::string unknown = whole;
    unknown.replace(unknown.find("\"group\":\"framing\""), 17, "\"group\":\"framinh\"");
    std::string extra = whole;
    extra.replace(extra.find("{\"framed\":"), 10, "{\"extra\":1,\"framed\":");
    std::string no_usage;
    for (const std::string_view line : lines) {
        if (line.find("\"group\":\"usage\"") == std::string_view::npos) {
            no_usage += std::string(line) + "\n";
        }
    }
    const std::string count = "\"vectors\":" + std::to_string(lines.size() - 1);
    no_usage.replace(no_usage.find(count), count.size(), "\"vectors\":" + std::to_string(lines.size() - 2));
    const std::array<std::string, 5> files = {dropped, unknown, extra, no_usage, planted(whole, "vectors")};
    constexpr std::array<std::string_view, 5> wanted = {"the head counts", "a group no reader knows",
                                                        "a member no reader reads", "no vector of group usage",
                                                        "the head counts"};
    std::size_t refused = 0;
    for (std::size_t i = 0; i < files.size(); ++i) {
        const Report r = read_all(c, files.at(i));
        bool named = false;
        for (const std::string& f : r.faults) {
            named = named || f.find(wanted.at(i)) != std::string::npos;
        }
        refused += named ? 1U : 0U;
        if (!named) {
            std::cout << "vectors.planted: file plant " << i << " not caught as " << wanted.at(i) << "\n";
        }
    }
    CHECK_EQ(refused, files.size());
    std::cout << "vectors.planted: " << caught << " of " << lines.size() - 1 << " vectors planted wrong caught, "
              << refused << " of " << files.size() << " file plants caught\n";
}
