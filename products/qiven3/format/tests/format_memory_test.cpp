// format's budgets and fault runs: frame, entry_id and keyed over a Key
// within the specification's 4 KiB beside the input, counted by the arena,
// each one allocation, its result's; a byte short a MemoryError budget with
// the caller's string unchanged; each allocation failed in turn, a typed
// error with nothing held; new_key on no arena, its draw failed on the fake a
// PlatformError with the partial bytes wiped. The runner's count after each
// case holds too.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "qiven/faults/faults.hpp"
#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/platform/fake/fake_platform.hpp"
#include "qiven/runner/runner.hpp"
#include "qiven3/format/entry_id.hpp"
#include "qiven3/format/key.hpp"

namespace {

namespace format = qiven3::format;
using format::Field;
using qiven::memory::Allocator;
using qiven::memory::Arena;
using qiven::memory::MemoryCode;
using qiven::memory::MemoryError;
using qiven::memory::String;
using Fake = qiven::platform::fake::FakePlatform;

constexpr std::size_t budget_for(std::size_t input) { return 4096 + input; }

// The case's eleven fields, the last 1,196 bytes long, in English and
// Chinese (格式).
struct Fields {
    std::string last = "format's own field, \xe6\xa0\xbc\xe5\xbc\x8f: " + std::string(1168, 'x');
    std::string scope = "scope:\xe6\xa0\xbc\xe5\xbc\x8f";
    std::array<Field, 11> fields = {"format-case/1", scope, "lane", "12", std::nullopt, "kind", "writer/w1",
                                    "basis", "2026-11-03T08:00:00.000Z", std::nullopt, last};
    [[nodiscard]] std::size_t bytes() const {
        std::size_t n = 0;
        for (const Field& f : fields) {
            n += f ? f->size() : 0U;
        }
        return n;
    }
};

std::array<std::byte, 32> counting(unsigned first) {
    std::array<std::byte, 32> key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(first + i);
    }
    return key;
}

// One operation of format's, writing into out.
template <typename Op>
void budget_case(qiven::runner::Context& qiven_ctx, std::string_view name, std::size_t input, const Op& op) {
    const std::size_t budget = budget_for(input);
    Arena& arena = qiven_ctx.counted.arena(budget);
    std::size_t peak = 0;
    std::size_t allocations = 0;
    {
        String out{Allocator<char>(arena)};
        op(out);
        peak = arena.peak();
        allocations = out.empty() ? 0U : 1U;
    }
    std::cout << "format." << name << ": input " << input << " bytes, peak " << peak << ", budget " << budget
              << ", result allocations " << allocations << "\n";
    CHECK(peak <= budget);
    CHECK(arena.used() == 0);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    Arena& shorter = qiven_ctx.counted.arena(peak - 1);
    String out{Allocator<char>(shorter)};
    bool refused = false;
    try {
        op(out);
    } catch (const MemoryError& e) {
        refused = e.code == MemoryCode::budget;
    }
    CHECK(refused);
    CHECK(out.empty());
#else
    std::cout << "format." << name << ": a byte short not run, under MSVC's debug containers\n";
#endif
}

}  // namespace

QIVEN_CASE(format, budgets) {
    const Fields own;
    CHECK_EQ(own.last.size(), 1196U);
    Fake fake{qiven::platform::fake::Config{}};
    const format::Key<Fake> key(fake, counting(0xa0));
    budget_case(qiven_ctx, "frame", own.bytes(), [&](String& out) { format::frame(own.fields, out); });
    budget_case(qiven_ctx, "entry_id", own.bytes(), [&](String& out) { format::entry_id(own.fields, out); });
    budget_case(qiven_ctx, "keyed", own.last.size(), [&](String& out) { format::keyed(key, own.last, out); });
    // new_key takes no arena: the key and its draw are on the stack.
    Arena& arena = qiven_ctx.counted.arena(budget_for(0));
    {
        const format::Key<Fake> drawn = format::new_key(fake);
        String out{Allocator<char>(arena)};
        format::keyed(drawn, "qiven", out);
        CHECK_EQ(out.size(), 76U);
    }
    CHECK(arena.used() == 0);
}

QIVEN_CASE(format, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const Fields own;
    Fake fake{qiven::platform::fake::Config{}};
    const format::Key<Fake> key(fake, counting(0xa0));
    std::vector<std::byte> pages(budget_for(own.bytes()));
    qiven::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    const auto runs = [&](qiven::log::Name site, auto write) {
        const qiven::test::FaultRuns r = qiven::test::run_faults<MemoryError>(
            qiven_ctx.log, site, countdown,
            [&] {
                arena.emplace(pages);
                arena->attach(&countdown);
                String out{Allocator<char>(*arena)};
                write(out);
            },
            check);
        // The result's one allocation.
        CHECK_EQ(r.typed, 1U);
        CHECK_EQ(r.completed_at, 2U);
        CHECK_EQ(r.untyped, 0U);
        CHECK_EQ(r.unchecked, 0U);
    };
    runs("format.frame", [&](String& out) { format::frame(own.fields, out); });
    runs("format.entry_id", [&](String& out) { format::entry_id(own.fields, out); });
    runs("format.keyed", [&](String& out) { format::keyed(key, own.last, out); });
    arena.reset();

    // new_key's platform calls: the draw fails, its bytes wiped after.
    fake.attach(&countdown);
    const qiven::test::FaultRuns draws = qiven::test::run_faults<qiven::platform::PlatformError>(
        qiven_ctx.log, "format.new_key", countdown, [&] { const format::Key<Fake> k = format::new_key(fake); },
        [&](std::size_t) { return fake.calls().back() == "wipe"; });
    fake.attach(nullptr);
    std::cout << "format.faults: new_key's platform calls failed " << draws.typed << ", completed at "
              << draws.completed_at << "\n";
    CHECK_EQ(draws.typed, 1U);
    CHECK_EQ(draws.completed_at, 2U);
    CHECK_EQ(draws.untyped, 0U);
    CHECK_EQ(draws.unchecked, 0U);
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
