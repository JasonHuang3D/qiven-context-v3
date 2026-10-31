// digest's budgets and fault runs: frame, entry_id, plain and keyed within
// the specification's 4 KiB beside the input, counted by the arena, each
// one allocation, its result's; a byte short a MemoryError budget with the
// caller's string unchanged; each allocation failed in turn, a typed error
// with nothing held; new_key on no arena, its draw failed on the fake a
// PlatformError with the partial bytes wiped. The runner's count after
// each case holds too.

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "qiven3/digest.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace digest = qiven3::digest;
using digest::Field;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryCode;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using Fake = qiven3::platform::fake::FakePlatform;

constexpr std::size_t budget_for(std::size_t input) { return 4096 + input; }

// An entry's eleven fields, its content a body record's size, in English
// and Chinese.
struct Header {
    std::string content = "hmac-sha256:" + std::string(64, 'a') + std::string(1120, 'x');
    std::string scope = "project:qiven \xe8\xae\xb0\xe5\xbf\x86";
    std::array<Field, 11> fields = {"qiven-entry/1", scope, "implementer", "12", std::nullopt, "claim",
                                    "implementer/s61", "claimed", "2026-10-21T09:30:00.000Z", std::nullopt,
                                    content};
    [[nodiscard]] std::size_t bytes() const {
        std::size_t n = 0;
        for (const Field& f : fields) {
            n += f ? f->size() : 0U;
        }
        return n;
    }
};

std::array<std::byte, 32> fixture_key() {
    std::array<std::byte, 32> key{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<std::byte>(i);
    }
    return key;
}

// One operation of digest's, writing into out.
template <typename Op>
void budget_case(qiven3::test::Context& qiven3_ctx, std::string_view name, std::size_t input, const Op& op) {
    const std::size_t budget = budget_for(input);
    Arena& arena = qiven3_ctx.counted.arena(budget);
    std::size_t peak = 0;
    std::size_t allocations = 0;
    {
        String out{Allocator<char>(arena)};
        op(out);
        peak = arena.peak();
        allocations = out.empty() ? 0U : 1U;
    }
    std::cout << "digest." << name << ": input " << input << " bytes, peak " << peak << ", budget " << budget
              << ", result allocations " << allocations << "\n";
    CHECK(peak <= budget);
    CHECK(arena.used() == 0);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    Arena& shorter = qiven3_ctx.counted.arena(peak - 1);
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
    std::cout << "digest." << name << ": a byte short not run, under MSVC's debug containers\n";
#endif
}

}  // namespace

QIVEN3_CASE(digest, budgets) {
    const Header header;
    Fake fake{qiven3::platform::fake::Config{}};
    const digest::Key<Fake> key(fake, fixture_key());
    budget_case(qiven3_ctx, "frame", header.bytes(), [&](String& out) { digest::frame(header.fields, out); });
    budget_case(qiven3_ctx, "entry_id", header.bytes(), [&](String& out) { digest::entry_id(header.fields, out); });
    budget_case(qiven3_ctx, "plain", header.content.size(), [&](String& out) { digest::plain(header.content, out); });
    budget_case(qiven3_ctx, "keyed", header.content.size(),
                [&](String& out) { digest::keyed(key, header.content, out); });
    // new_key takes no arena: the key and its draw are on the stack.
    Arena& arena = qiven3_ctx.counted.arena(budget_for(0));
    {
        const digest::Key<Fake> drawn = digest::new_key(fake);
        String out{Allocator<char>(arena)};
        digest::keyed(drawn, "qiven", out);
        CHECK_EQ(out.size(), 76U);
    }
    CHECK(arena.used() == 0);
}

QIVEN3_CASE(digest, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    const Header header;
    Fake fake{qiven3::platform::fake::Config{}};
    const digest::Key<Fake> key(fake, fixture_key());
    std::vector<std::byte> pages(budget_for(header.bytes()));
    qiven3::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    const auto runs = [&](qiven3::log::Name site, auto write) {
        const qiven3::test::FaultRuns r = qiven3::test::run_faults<MemoryError>(
            qiven3_ctx.log, site, countdown,
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
    runs("digest.frame", [&](String& out) { digest::frame(header.fields, out); });
    runs("digest.entry_id", [&](String& out) { digest::entry_id(header.fields, out); });
    runs("digest.plain", [&](String& out) { digest::plain(header.content, out); });
    runs("digest.keyed", [&](String& out) { digest::keyed(key, header.content, out); });
    arena.reset();

    // new_key's platform calls: the draw fails, its bytes wiped after.
    fake.attach(&countdown);
    const qiven3::test::FaultRuns draws = qiven3::test::run_faults<qiven3::platform::PlatformError>(
        qiven3_ctx.log, "digest.new_key", countdown, [&] { const digest::Key<Fake> k = digest::new_key(fake); },
        [&](std::size_t) { return fake.calls().back() == "wipe"; });
    fake.attach(nullptr);
    std::cout << "digest.faults: new_key's platform calls failed " << draws.typed << ", completed at "
              << draws.completed_at << "\n";
    CHECK_EQ(draws.typed, 1U);
    CHECK_EQ(draws.completed_at, 2U);
    CHECK_EQ(draws.untyped, 0U);
    CHECK_EQ(draws.unchecked, 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
