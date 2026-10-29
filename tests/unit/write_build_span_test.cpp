// write/build's work span, its path pieces and its @name: scope (every
// name, scope, id and text invented). An import file of 512 entries, each
// naming a path piece and the entry before it, built in a fixed arena of
// two work spans and four MiB: the build's peak past what it holds is the
// same for 64 entries and for 512, its work given back at each entry's
// end, and the piece read once for every reference. A path piece named by
// three references read once, each digest over its bytes; a piece whose
// file changes after its read gives every reference the bytes first read.
// An @name: naming an entry of another scope, in evidence or as a target,
// refused form by its path, as a literal target of another scope is.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "entry/kinds.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
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

struct EmptyFold {
    std::optional<wb::VersionView> head(std::string_view, std::string_view) const { return std::nullopt; }
    std::optional<wb::VersionView> current(std::string_view, const qiven3::entry::kinds::IdentityView&) const {
        return std::nullopt;
    }
    std::optional<wb::VersionView> find(std::string_view, std::string_view) const { return std::nullopt; }
};
static_assert(wb::Fold<EmptyFold>);

// The fake, its file at a path rewritten after each read of it.
class Changing : public FakePlatform {
public:
    using FakePlatform::FakePlatform;
    void read(std::string_view path, qiven3::memory::String& out) {
        FakePlatform::read(path, out);
        std::ofstream(std::string(path), std::ios::binary) << "changed after its read\n";
    }
};
static_assert(qiven3::platform::Platform<Changing>);

qiven3::platform::fake::Config config(const std::string& root) {
    qiven3::platform::fake::Config c;
    c.root = root;
    c.seed = 7;
    c.clock = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::sys_days{std::chrono::year{2026} / 10 / 29} + std::chrono::hours{8});
    return c;
}

// A scratch directory of its own, removed at the scope's end.
struct Scratch {
    Scratch() {
        std::vector<std::random_device> device(1);
        root = fs::temp_directory_path() / ("qiven3-build-span-" + std::to_string(device.front()()));
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

const std::string admit = R"("admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."})";
const std::string output = R"({"type": "output", "command": "an invented command", "piece": "record"})";

// An import file of n claims, each naming the piece at path and, but the
// first, the entry before it.
std::string chain(std::size_t n, const std::string& path) {
    std::string s = R"({"form": "qiven3-entries/1", "writer": "import", "scope": "project:example", )"
                    R"("pieces": {"record": {"path": ")" + path + R"("}}, "entries": [)";
    for (std::size_t i = 0; i < n; ++i) {
        s += (i == 0 ? "" : ", ") + std::string(R"({"name": "entry-)") + std::to_string(i) +
             R"(", "kind": "claim", "basis": "claimed", "content": {"claim": "Invented claim )" + std::to_string(i) +
             R"(.", "subject": "entity:example", "band": "normal", )" + admit + R"(, "evidence": [)" + output +
             (i == 0 ? "" : R"(, {"type": "entry", "id": "@name:entry-)" + std::to_string(i - 1) + "\"}") + "]}}";
    }
    return s + "]}";
}

// A built entry's texts, copied out of the arena; or its refusal as
// code/why@path#index:name; and the build's peak and held bytes.
struct Result {
    std::vector<std::string> records;
    std::string refused;
    std::size_t peak = 0;
    std::size_t held = 0;
};
template <class P>
Result build(const std::string& bytes, P& platform, std::size_t arena_bytes = 16 * mib) {
    std::vector<std::byte> pages(arena_bytes);
    Arena arena(pages);
    Result r;
    try {
        const we::File f = we::read(bytes, arena);
        const std::size_t peak = arena.peak();
        const std::size_t used = arena.used();
        qiven3::memory::Vector<wb::Built> out{qiven3::memory::Allocator<wb::Built>(arena)};
        EmptyFold fold;
        wb::build(f, fold, platform, arena, out);
        r.peak = arena.peak() - peak;
        r.held = arena.used() - used;
        for (const wb::Built& b : out) {
            r.records.emplace_back(b.record);
        }
    } catch (const we::EntriesError& e) {
        r.refused = std::string(we::name(e.code)) + "/" + std::string(we::name(e.why)) + "@" +
                    std::string(e.path.view()) + "#" + std::to_string(e.entry) + ":" + std::string(e.name.view());
    }
    return r;
}

std::size_t reads(const FakePlatform& platform) {
    return static_cast<std::size_t>(std::count(platform.calls().begin(), platform.calls().end(), "read"));
}

// The HMAC under the record's key over data, as hmac-sha256:hex.
std::string keyed(const std::string& record, std::string_view data) {
    const std::size_t at = record.find("\"key\":\"");
    std::string raw;
    for (std::size_t i = 0; at != std::string::npos && i < 64; i += 2) {
        raw.push_back(static_cast<char>(std::stoi(record.substr(at + 7 + i, 2), nullptr, 16)));
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

std::size_t times(const std::string& text, const std::string& part) {
    std::size_t n = 0;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
        ++n;
    }
    return n;
}

}  // namespace

QIVEN3_CASE(write_build, builds_512_entries_in_a_fixed_arena) {
    const Scratch scratch;
    const std::string piece = "progress: unit one built\n";
    const std::string path = scratch.put("record.txt", piece);
    const std::size_t fixed = 2 * wb::detail::work_bytes + 4 * mib;
    std::vector<Result> runs;
    for (const std::size_t n : {std::size_t{64}, std::size_t{512}}) {
        FakePlatform platform(config(scratch.root.generic_string()));
        runs.push_back(build(chain(n, path), platform, fixed));
        const Result& r = runs.back();
        REQUIRE(r.refused.empty());
        REQUIRE(r.records.size() == n);
        CHECK_EQ(reads(platform), 1U);
        CHECK(r.records.back().find(keyed(r.records.back(), piece)) != std::string::npos);
        std::cout << "write_build.span: " << n << " entries held in " << r.held << " bytes, the build's peak "
                  << r.peak << "\n";
    }
    // The work given back at each entry's end: past what is held, the peak
    // is the work span and the piece's one read, whatever the count.
    CHECK_EQ(runs[0].peak - runs[0].held, runs[1].peak - runs[1].held);
    CHECK(runs[1].peak - runs[1].held <= wb::detail::work_bytes + 4096);
    CHECK(runs[1].held > runs[0].held);
}

QIVEN3_CASE(write_build, reads_a_path_piece_once) {
    const Scratch scratch;
    const std::string piece = "line one\r\nline two\n";
    const std::string path = scratch.put("record.txt", piece);
    // Three references: the first entry's one, the second's two.
    std::string file = chain(2, path);
    file.insert(file.rfind(output) + output.size(), ", " + output);
    for (const bool changes : {false, true}) {
        static_cast<void>(scratch.put("record.txt", piece));  // as before the change
        // The fakes on the heap, the frame within its bound.
        const auto plain = std::make_unique<FakePlatform>(config(scratch.root.generic_string()));
        const auto changing = std::make_unique<Changing>(config(scratch.root.generic_string()));
        const Result r = changes ? build(file, *changing) : build(file, *plain);
        REQUIRE(r.refused.empty());
        REQUIRE(r.records.size() == 2U);
        CHECK_EQ(reads(changes ? static_cast<const FakePlatform&>(*changing) : *plain), 1U);
        // Every reference's digest over the bytes of the one read.
        CHECK_EQ(times(r.records[0], keyed(r.records[0], piece)), 1U);
        CHECK_EQ(times(r.records[1], keyed(r.records[1], piece)), 2U);
    }
}

QIVEN3_CASE(write_build, refuses_an_at_name_of_another_scope) {
    const Scratch scratch;
    FakePlatform platform(config(scratch.root.generic_string()));
    const std::string head = R"({"form": "qiven3-entries/1", "writer": "import", "scope": "project:example", )"
                             R"("pieces": {"record": {"text": "an invented output\n"}}, "entries": [)"
                             R"({"name": "who", "scope": "person", "kind": "claim", "basis": "claimed", )"
                             R"("content": {"claim": "An invented claim about the person.", "subject": "scope:person", )"
                             R"("band": "core", )" + admit + R"(, "evidence": [)" + output + "]}}, ";
    const std::string evidence = head + R"({"name": "uses", "kind": "claim", "basis": "claimed", "content": )"
                                        R"({"claim": "An invented claim resting on it.", "subject": "entity:example", )"
                                        R"("band": "normal", )" + admit +
                                 R"(, "evidence": [{"type": "entry", "id": "@name:who"}]}}]})";
    const std::string target = head + R"({"kind": "claim", "basis": "claimed", "target": "@name:who", "content": )"
                                      R"({"claim": "An invented revision across scopes.", "subject": "entity:example", )"
                                      R"("band": "normal", )" + admit + R"(, "evidence": [)" + output + "]}}]}";
    CHECK(build(evidence, platform).refused == "form/placeholder@entries[1].content.evidence[0].id#1:uses");
    CHECK(build(target, platform).refused == "form/placeholder@entries[1].target#1:");
    // In one scope, each builds.
    const std::string person = R"("name": "who", "scope": "person",)";
    const std::string same = R"("name": "who",)";
    const auto unscoped = [&](std::string s) { return s.replace(s.find(person), person.size(), same); };
    CHECK(build(unscoped(evidence), platform).refused.empty());
    CHECK(build(unscoped(target), platform).refused.empty());
}
