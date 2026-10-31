// projectmap's budgets and fault runs, over an arena: parse_registry takes
// canon's three allocations, all held by the Registry; parse_marks canon's
// four, freed, and one for each non-empty list, held by the Marks;
// marks_bytes one, the text's; mark one, the digest's text, freed; check_map
// none of its own, the view's copy of the named header alone, freed. Each
// is counted by its fault run, which completes one past its count, the
// arena empty after every failed run; each peak is printed beside the
// bytes it reads. The runner's count after each case holds too.

#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "entry/projectmap.hpp"
#include "entry/projectmap_files.hpp"
#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/canon.hpp"
#include "qiven3/digest.hpp"
#include "qiven/runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"
#include "support/fixtures/view.hpp"

namespace {

namespace projectmap = qiven3::entry::projectmap;
namespace header = qiven3::entry::header;
namespace canon = qiven3::canon;
namespace digest = qiven3::digest;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using Fake = qiven3::platform::fake::FakePlatform;

#define HEX "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define A64 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define C64 "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"
#define D64 "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"
constexpr std::string_view REGISTRY =
    "{\n  \"qiven-context-v3\": {\"path\": \"/srv/v3\", \"ref\": \"cognition-core\"},\n"
    "  \"qiven-other\": {\"path\": \"/srv/other\", \"ref\": \"main\"}\n}\n";
constexpr std::string_view MARKS =
    R"({"at":"2026-10-17T12:06:00.000Z","marks":{"sha256:)" HEX R"(":"fresh","sha256:)" A64 R"(":"stale")"
    R"(},"read":{"qiven-context-v3":"0123456789abcdef0123456789abcdef01234567"}})"
    "\n";
constexpr std::string_view MAP_HEADER = R"({"basis":"derived","id":"sha256:)" D64 R"(","kind":"map"})";
constexpr std::string_view MAP_CONTENT =
    R"({"object":{"digest":"hmac-sha256:)" HEX R"(","id":"sha256:)" C64 R"(","type":"entry"}})";
constexpr std::string_view CHECK_HEADER =
    R"({"basis":"claimed","id":"sha256:)" C64 R"(","kind":"check","target":"sha256:)" A64 R"("})";

// The operations, each over arena allocator a.
enum class Op { registry, marks, bytes, mark, rule };
constexpr std::array<Op, 5> OPS = {Op::registry, Op::marks, Op::bytes, Op::mark, Op::rule};
constexpr std::string_view label(Op op) {
    switch (op) {
        case Op::registry: return "parse_registry";
        case Op::marks: return "parse_marks";
        case Op::bytes: return "marks_bytes";
        case Op::mark: return "mark";
        case Op::rule: return "check_map";
    }
    return "";
}
constexpr std::size_t expected(Op op) {
    switch (op) {
        case Op::registry: return 3;
        case Op::marks: return 6;
        case Op::bytes:
        case Op::mark:
        case Op::rule: return 1;
    }
    return 0;
}

// What each operation reads, made outside the arena.
struct Inputs {
    Fake fake{qiven3::platform::fake::Config{}};
    projectmap::Marks<> marks = projectmap::parse_marks(MARKS);
    canon::Basic<> content;
    qiven3::test::fixtures::MemoryView view;
    Inputs() {
        canon::parse(MAP_CONTENT, content);
        view.put(header::parse_line(CHECK_HEADER), std::nullopt);
    }
};

// Op over a, its result held until the arena's use is read: the bytes held
// after it returns.
std::size_t run(Op op, Inputs& in, Arena& arena, const digest::Key<Fake>& key) {
    const Allocator<char> a(arena);
    switch (op) {
        case Op::registry: {
            const projectmap::Registry<Allocator<char>> r =
                projectmap::parse_registry(REGISTRY, a, projectmap::System::posix);
            return arena.used() + (r.size() == 2 ? 0 : 1);
        }
        case Op::marks: {
            const projectmap::Marks<Allocator<char>> m = projectmap::parse_marks(MARKS, a);
            return arena.used() + (m.size() == 2 ? 0 : 1);
        }
        case Op::bytes: {
            String out{a};
            projectmap::marks_bytes(in.marks, out);
            return arena.used() + (out == MARKS ? 0 : 1);
        }
        case Op::mark: {
            const header::Header<> h = header::parse_line(MAP_HEADER);
            const auto m = projectmap::mark(h, in.content, key, "bytes", a);
            return arena.used() + (m == qiven3::entry::served::Mark::stale ? 0 : 1);
        }
        case Op::rule: {
            const header::Header<Allocator<char>> h = header::parse_line(MAP_HEADER, Allocator<char>(arena));
            const std::size_t before = arena.used();
            const std::optional<projectmap::Code> c = projectmap::try_check_map(h, in.content, in.view);
            return arena.used() - before + (c == projectmap::Code::reference ? 0 : 1);
        }
    }
    return 1;
}

}  // namespace

QIVEN_CASE(projectmap, budget) {
    Inputs in;
    const digest::Key<Fake> key = digest::key_from_hex(in.fake, HEX);
    for (const Op op : OPS) {
        Arena& arena = qiven_ctx.counted.arena(1U << 20U);
        const std::size_t before = arena.peak();
        const std::size_t held = run(op, in, arena, key);
        std::cout << "projectmap.budget: " << label(op) << " peak " << arena.peak() - before
                  << " bytes, held at its end " << held << ", after " << arena.used() << "\n";
        CHECK(arena.peak() - before < (1U << 16U));
        // Only what a returned value holds stays; check_map holds nothing.
        CHECK(op != Op::rule || held == 0);
    }
}

QIVEN_CASE(projectmap, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    Inputs in;
    const digest::Key<Fake> key = digest::key_from_hex(in.fake, HEX);
    std::vector<std::byte> pages(1U << 16U);
    for (const Op op : OPS) {
        qiven3::memory::Countdown countdown;
        std::optional<Arena> arena;
        std::size_t held = 1;
        const auto once = [&] {
            arena.emplace(pages);
            if (op == Op::rule) {
                // The header is the caller's; the count is the rule's.
                const header::Header<Allocator<char>> h = header::parse_line(MAP_HEADER, Allocator<char>(*arena));
                arena->attach(&countdown);
                held = projectmap::try_check_map(h, in.content, in.view) == projectmap::Code::reference ? 0 : 1;
                arena->attach(nullptr);
                return;
            }
            arena->attach(&countdown);
            held = run(op, in, *arena, key);
            arena->attach(nullptr);
        };
        const auto empty = [&](std::size_t) { return arena->used() == 0; };
        const qiven3::test::FaultRuns runs =
            qiven3::test::run_faults<MemoryError>(qiven_ctx.log, "projectmap", countdown, once, empty);
        std::cout << "projectmap.faults: " << label(op) << "'s allocations " << runs.typed << ", completed at "
                  << runs.completed_at << "\n";
        CHECK_EQ(runs.typed, expected(op));
        CHECK_EQ(runs.completed_at, expected(op) + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK(held == 0 || op != Op::rule);
        arena.reset();
    }
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
