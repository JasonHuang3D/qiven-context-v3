// write/build's budgets and fault runs over synthetic files (every name,
// slot, scope, id and text invented). A build holds on the arena the out
// vector and each entry's two lines and identity key alone, each entry's
// work in a work span given back at the build's end; on any error out is
// left empty and the arena holds the file alone, as before the build. Each
// allocation failed in turn, the arena's and each work arena's, is a
// MemoryError with out empty and nothing more held, never a refusal; the
// run completes past the last, which counts them exactly, by MSVC's
// library, libstdc++ and libc++: a write file of three entries with a
// piece referenced twice and three placeholders 126, 112 and 109, an
// import file of two 70, 60 and 60, the write file with a content that
// expires at its at 74, 63 and 62, with its refusal, the write file with
// its piece a path 128, 114 and 111. Each platform call failed in turn, three keys' draws, the path
// piece's one read and three clocks', is the platform's error, the read's
// the refusal form/piece, with out empty, nothing more held and the last
// key wiped.

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "entry/kinds.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "write/build.hpp"
#include "write/entries.hpp"
#include "write/entries_reader.hpp"

namespace {

namespace wb = qiven3::write::build;
namespace we = qiven3::write::entries;
namespace fs = std::filesystem;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using qiven3::platform::fake::FakePlatform;

// The reader's work span, the builder's, and the file's room.
constexpr std::size_t budget = std::size_t{16} << 20U;

#define ADMIT R"("admit": {"underivable": "No file holds it.", "mistake": "A session repeats it."})"

// Three entries, a piece referenced twice and three placeholders.
const std::string write_file = R"json({
  "form": "qiven3-entries/1", "writer": "implementer/s7", "scope": "project:example",
  "episode": {"client": "claude-code", "session": "synthetic-session-7"},
  "pieces": {"record": {"text": "progress: unit one built\n"}},
  "entries": [
    {"name": "moved", "kind": "claim", "basis": "claimed",
     "content": {"claim": "An invented claim.", "subject": "entity:example", "band": "normal", )json" ADMIT R"json(,
       "evidence": [{"type": "output", "command": "an invented command", "piece": "record"}]}},
    {"kind": "claim", "basis": "claimed",
     "content": {"claim": "Another invented claim.", "subject": "entity:example", "band": "low", )json" ADMIT R"json(,
       "evidence": [{"type": "output", "command": "an invented command", "piece": "record"},
                    {"type": "entry", "id": "@name:moved"}]}},
    {"kind": "retire", "basis": "claimed", "target": "@question:unit-one-shape",
     "content": {"reason": "Settled in review.", "evidence": [{"type": "entry", "id": "@name:moved"}]}}
  ]
})json";

const std::string import_file = R"json({
  "form": "qiven3-entries/1", "writer": "import", "scope": "project:example",
  "entries": [
    {"kind": "item", "basis": "claimed",
     "content": {"item_id": "core", "state": "active", "turn": "reviewer", "subject": "item:core",
       "band": "normal", )json" ADMIT R"json(,
       "evidence": [{"type": "entry", "id": "sha256:1111111111111111111111111111111111111111111111111111111111111111"}]}},
    {"scope": "project:other", "kind": "claim", "basis": "claimed",
     "content": {"claim": "Another invented claim.", "subject": "entity:example", "band": "low", )json" ADMIT R"json(,
       "evidence": [{"type": "entry", "id": "sha256:1111111111111111111111111111111111111111111111111111111111111111"}]}}
  ]
})json";

std::string swap(std::string text, std::string_view from, std::string_view to) {
    const std::size_t at = text.find(from);
    return at == std::string::npos ? "missing: " + std::string(from) : text.replace(at, from.size(), to);
}

// The second content expires at the clock's at: refused invalid at entry 1
// once built, though its read passed.
const std::string invalid_file =
    swap(write_file, R"("band": "low")", R"("expires": "2026-10-29T08:00:00.000Z", "band": "low")");

// The fold holds the question the retire names, and nothing else.
struct OneQuestion {
    std::optional<wb::VersionView> head(std::string_view, std::string_view) const { return std::nullopt; }
    std::optional<wb::VersionView> current(std::string_view scope, const qiven3::entry::kinds::IdentityView& i) const {
        if (scope == "project:example" && i.kind == qiven3::entry::kinds::index_of("question") &&
            i.parts[0] == "unit-one-shape") {
            return wb::VersionView{"sha256:3333333333333333333333333333333333333333333333333333333333333333",
                               "2026-10-29T07:00:00.000Z", 1};
        }
        return std::nullopt;
    }
    std::optional<wb::VersionView> find(std::string_view, std::string_view) const { return std::nullopt; }
};
static_assert(wb::Fold<OneQuestion>);

qiven3::platform::fake::Config config(const std::string& root = {}) {
    qiven3::platform::fake::Config c;
    c.root = root;
    c.seed = 7;
    c.clock = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::sys_days{std::chrono::year{2026} / 10 / 29} + std::chrono::hours{8});
    return c;
}

// The build's values and texts grow unreserved, so its count follows the
// library's growth: MSVC's, libstdc++'s, libc++'s.
constexpr std::size_t grown(std::size_t msvc, std::size_t libstdcxx, std::size_t libcxx) {
    static_cast<void>(msvc);
    static_cast<void>(libstdcxx);
    static_cast<void>(libcxx);
#if defined(_MSVC_STL_VERSION)
    return msvc;
#elif defined(_LIBCPP_VERSION)
    return libcxx;
#else
    return libstdcxx;
#endif
}

// What a build left: its entries, its refusal, and whether out was empty
// with the arena holding the file alone after an error.
struct Left {
    std::size_t built = 0;
    std::optional<we::EntriesError> refused;
    bool clean = false;
};

// bytes read on arena, then built with the countdown attached by attach
// and detached by detach, into left; the arena's and the platform's errors
// rethrown once left is set.
template <class Attach, class Detach>
void build(std::string_view bytes, Arena& arena, FakePlatform& platform, Attach attach, Detach detach, Left& left,
           we::Hooks hooks = {}) {
    left = Left{};
    const we::File file = we::read(bytes, arena);
    const std::size_t file_held = arena.used();
    qiven3::memory::Vector<wb::Built> out{qiven3::memory::Allocator<wb::Built>(arena)};
    OneQuestion fold;
    attach();
    try {
        wb::build(file, fold, platform, arena, out, hooks);
    } catch (...) {
        detach();
        left.clean = out.empty() && out.capacity() == 0 && arena.used() == file_held;
        try {
            throw;
        } catch (const we::EntriesError& e) {
            left.refused = e;
            return;
        }
    }
    detach();
    left.built = out.size();
    left.clean = arena.used() > file_held;
}

void none() {}

struct Case {
    std::string_view name;
    const std::string* bytes;
    std::size_t allocations;
    bool refused;
};

// A scratch directory of its own, removed at the scope's end.
struct Scratch {
    Scratch() {
        std::vector<std::random_device> device(1);
        root = fs::temp_directory_path() / ("qiven3-build-memory-" + std::to_string(device.front()()));
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
    fs::path root;
};

// The write file, its piece a file under scratch of the same bytes.
std::string path_file_of(const Scratch& scratch) {
    const fs::path piece = scratch.root / "record.txt";
    std::ofstream(piece, std::ios::binary) << "progress: unit one built\n";
    return swap(write_file, R"({"text": "progress: unit one built\n"})",
                R"({"path": ")" + piece.generic_string() + "\"}");
}

}  // namespace

QIVEN3_CASE(write_build, budget) {
    FakePlatform platform(config());
    for (const std::string* bytes : {&write_file, &import_file}) {
        Arena& arena = qiven3_ctx.counted.arena(budget);
        {
            const we::File f = we::read(*bytes, arena);
            const std::size_t file_held = arena.used();
            {
                qiven3::memory::Vector<wb::Built> out{qiven3::memory::Allocator<wb::Built>(arena)};
                OneQuestion fold;
                const std::size_t before = arena.peak();
                wb::build(f, fold, platform, arena, out);
                CHECK_EQ(out.size(), f.entries().size());
                CHECK(arena.used() > file_held);
                std::cout << "write_build.budget: " << out.size() << " entries held in " << arena.used() - file_held
                          << " bytes, the build's peak " << arena.peak() - before << "\n";
            }
            CHECK_EQ(arena.used(), file_held);
        }
        CHECK_EQ(arena.used(), 0U);
    }
    // A refusal once built: out empty, the file alone held.
    Arena& arena = qiven3_ctx.counted.arena(budget);
    Left left;
    build(invalid_file, arena, platform, none, none, left);
    REQUIRE(left.refused.has_value());
    CHECK(left.refused->code == we::Code::invalid);
    CHECK_EQ(left.refused->entry, 1U);
    CHECK(left.clean);
    CHECK_EQ(arena.used(), 0U);
}

QIVEN3_CASE(write_build, faults) {
#if defined(QIVEN3_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const Scratch scratch;
    const std::string path_file = path_file_of(scratch);
    FakePlatform platform(config(scratch.root.generic_string()));
    Left left;
    const std::array<Case, 4> all = {
        Case{"a write file", &write_file, grown(126, 112, 109), false},
        Case{"an import file", &import_file, grown(70, 60, 60), false},
        Case{"a content invalid once built", &invalid_file, grown(74, 63, 62), true},
        Case{"a write file of a path piece", &path_file, grown(128, 114, 111), false},
    };
    for (const Case& c : all) {
        // The countdown on the arena and on each entry's work arena.
        const auto op = [&] {
            arena.emplace(pages);
            build(*c.bytes, *arena, platform, [&] { arena->attach(&countdown); }, [&] { arena->attach(nullptr); },
                  left, we::Hooks{&countdown});
        };
        const qiven3::test::FaultRuns runs = qiven3::test::run_faults<MemoryError>(
            qiven3_ctx.log, "write_build.build", countdown, op, [&](std::size_t) { return left.clean; });
        std::cout << "write_build.faults: " << c.name << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, c.allocations);
        CHECK_EQ(runs.completed_at, c.allocations + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
        CHECK_EQ(left.refused.has_value(), c.refused);
        CHECK_EQ(left.built, c.refused ? 0U : 3U - (c.bytes == &import_file ? 1U : 0U));
    }
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS, or with MSVC's debug containers");
#endif
}

QIVEN3_CASE(write_build, platform_faults) {
#if defined(QIVEN3_FAULTS)
    const Scratch scratch;
    const std::string bytes = path_file_of(scratch);
    FakePlatform platform(config(scratch.root.generic_string()));
    std::vector<std::byte> pages(budget);
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    Left left;
    std::size_t refusals = 0;
    std::size_t errors = 0;
    struct Failed {};
    const auto op = [&] {
        arena.emplace(pages);
        try {
            build(bytes, *arena, platform, [&] { platform.attach(&countdown); }, [&] { platform.attach(nullptr); },
                  left);
        } catch (const qiven3::platform::PlatformError& e) {
            left.clean = left.clean && e.code == qiven3::platform::Code::io;
            ++errors;
            throw Failed{};
        }
        if (left.refused) {
            // An unread piece: refused by its path, as an absent one is.
            left.clean = left.clean && left.refused->code == we::Code::form &&
                         left.refused->why == we::Why::piece && left.refused->path.view().ends_with(".piece");
            ++refusals;
            throw Failed{};
        }
    };
    const qiven3::test::FaultRuns runs = qiven3::test::run_faults<Failed>(
        qiven3_ctx.log, "write_build.platform", countdown, op,
        [&](std::size_t) { return left.clean && platform.calls().back() == "wipe"; });
    std::cout << "write_build.faults: the platform's calls failed " << runs.typed << ", completed at "
              << runs.completed_at << ", reads refused " << refusals << "\n";
    CHECK_EQ(runs.typed, 7U);
    CHECK_EQ(runs.completed_at, 8U);
    CHECK_EQ(runs.untyped, 0U);
    CHECK_EQ(runs.unchecked, 0U);
    CHECK_EQ(refusals, 1U);
    CHECK_EQ(errors, 6U);
    CHECK_EQ(left.built, 3U);
    arena.reset();
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS");
#endif
}
