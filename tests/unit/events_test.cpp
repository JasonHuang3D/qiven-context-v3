// The report's events (diag/events.hpp): the kept rings' table, at most 8,
// none kept twice; the events, the rings' lines in their slots' order, each
// ring's oldest first, the last 16 in all, a ring's two pieces in order;
// and the cut at a code point's start, by constant evaluation. The reader
// takes no allocator, so it allocates nothing by construction.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "diag/events.hpp"
#include "log/ring.hpp"
#include "runner/runner.hpp"

namespace {

using qiven3::diag::detail::cut;
using qiven3::diag::detail::events;
using qiven3::diag::detail::events_max;
using qiven3::diag::detail::hold;
using qiven3::diag::detail::release;
using qiven3::diag::detail::Rings;
using qiven3::diag::detail::rings_max;
using qiven3::log::Ring;

static_assert(cut("abc", 3) == "abc");
static_assert(cut("abc", 2) == "ab");
static_assert(cut("abc", 0).empty());
static_assert(cut("a\xC3\xA9" "b", 2) == "a\xC3\xA9");
static_assert(cut("a\xC3\xA9" "b", 1) == "a");
static_assert(cut("\xF0\x9F\x98\x80" "x", 1) == "\xF0\x9F\x98\x80");

// The events of a table: the count head() gave, and each line.
struct Seen {
    std::uint64_t count = 0;
    std::vector<std::string> lines;
};

Seen seen(const Rings& rings) {
    Seen out;
    out.count = 99;
    events(
        rings, [&out](std::uint64_t n) noexcept { out.count = n; },
        [&out](std::string_view line) noexcept { out.lines.emplace_back(line); });
    return out;
}

// A ring over its own storage.
struct Held {
    explicit Held(std::size_t bytes) : storage(bytes), ring(std::span<char>(storage)) {}
    std::vector<char> storage;
    Ring ring;
};

void push(Ring& ring, const std::string& prefix, int from, int to) {
    for (int i = from; i < to; ++i) {
        ring.push(prefix + std::to_string(i));
    }
}

}  // namespace

QIVEN3_CASE(events, table) {
    Rings rings{};
    std::vector<std::unique_ptr<Held>> held;
    for (std::size_t i = 0; i <= rings_max; ++i) {
        held.push_back(std::make_unique<Held>(64));
    }
    CHECK(hold(rings, held[0]->ring));
    CHECK(!hold(rings, held[0]->ring));
    for (std::size_t i = 1; i < rings_max; ++i) {
        CHECK(hold(rings, held[i]->ring));
    }
    CHECK(!hold(rings, held[rings_max]->ring));
    CHECK(!release(rings, held[rings_max]->ring));
    CHECK(release(rings, held[3]->ring));
    CHECK(!release(rings, held[3]->ring));
    CHECK(hold(rings, held[rings_max]->ring));
    for (std::size_t i = 0; i <= rings_max; ++i) {
        CHECK_EQ(release(rings, held[i]->ring), i != 3);
    }
}

QIVEN3_CASE(events, lines) {
    Rings rings{};
    const Seen none = seen(rings);
    CHECK_EQ(none.count, 0U);
    CHECK(none.lines.empty());

    Held a(4096);
    Held b(4096);
    push(a.ring, "a", 0, 3);
    CHECK(hold(rings, a.ring));
    const Seen one = seen(rings);
    CHECK_EQ(one.count, 3U);
    CHECK(one.lines == (std::vector<std::string>{"a0", "a1", "a2"}));

    // Two rings, 20 lines: the last 16, a's 3 to 9, then b's 0 to 8.
    push(a.ring, "a", 3, 10);
    push(b.ring, "b", 0, 9);
    CHECK(hold(rings, b.ring));
    const Seen two = seen(rings);
    CHECK_EQ(two.count, events_max);
    std::vector<std::string> want;
    for (int i = 3; i < 10; ++i) {
        want.push_back("a" + std::to_string(i));
    }
    for (int i = 0; i < 9; ++i) {
        want.push_back("b" + std::to_string(i));
    }
    CHECK(two.lines == want);
    CHECK(release(rings, a.ring));
    CHECK(release(rings, b.ring));
}

QIVEN3_CASE(events, wrapped) {
    // A span of 16 bytes and lines of 4 with their '\n': after 5, the ring
    // holds two pieces, its older first.
    Rings rings{};
    Held small(16);
    push(small.ring, "w", 10, 15);
    CHECK(!small.ring.lines().newer.empty());
    CHECK(hold(rings, small.ring));
    const Seen got = seen(rings);
    CHECK_EQ(got.count, static_cast<std::uint64_t>(got.lines.size()));
    REQUIRE(!got.lines.empty());
    CHECK_EQ(got.lines.back(), std::string("w14"));
    for (std::size_t i = 1; i < got.lines.size(); ++i) {
        CHECK(got.lines[i - 1] < got.lines[i]);
    }
    CHECK(release(rings, small.ring));
}

QIVEN3_CASE(events, cut_long) {
    // A line past 300 code points comes out cut at 300.
    Rings rings{};
    Held big(4096);
    big.ring.push(std::string(400, 'x'));
    CHECK(hold(rings, big.ring));
    const Seen got = seen(rings);
    REQUIRE(got.lines.size() == 1U);
    CHECK_EQ(got.lines[0].size(), qiven3::diag::detail::event_points);
    CHECK(release(rings, big.ring));
}
