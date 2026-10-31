// log::Ring's cases: whole lines in order, the oldest evicted whole, never
// a line across the span's end, since() from a line on, and the lines it
// refuses; over every span from 2 to 48 bytes, each with lines of every
// length that fits.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "log/ring.hpp"
#include "qiven/runner/runner.hpp"

namespace {

using qiven3::log::Ring;
using qiven3::log::RingView;

// The n-th line of a run: its number, then letters to its length.
std::string line_of(std::uint64_t n, std::size_t length) {
    std::string s = std::to_string(n);
    s.resize(length, static_cast<char>('a' + static_cast<char>(n % 26)));
    return s.substr(0, length);
}

// A view's lines; empty when a piece holds part of a line.
bool split(const RingView& v, std::vector<std::string>& out) {
    out.clear();
    for (const std::string_view piece : {v.older, v.newer}) {
        std::string_view rest = piece;
        while (!rest.empty()) {
            const std::size_t end = rest.find('\n');
            if (end == std::string_view::npos) {
                return false;
            }
            out.emplace_back(rest.substr(0, end));
            rest.remove_prefix(end + 1);
        }
    }
    return true;
}

// The pushed lines from first to next, as the ring should hold them.
bool holds(const Ring& ring, const std::vector<std::string>& pushed, std::uint64_t from) {
    std::vector<std::string> got;
    if (!split(from == 0 ? ring.lines() : ring.since(from), got)) {
        return false;
    }
    const std::uint64_t start = from > ring.first() ? from : ring.first();
    if (ring.next() != pushed.size() || got.size() != ring.next() - start) {
        return false;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (got[i] != pushed[static_cast<std::size_t>(start) + i]) {
            return false;
        }
    }
    return true;
}

}  // namespace

QIVEN_CASE(ring, order) {
    std::vector<char> storage(64);
    Ring ring(storage);
    CHECK(ring.lines().older.empty());
    CHECK(ring.lines().newer.empty());
    CHECK(ring.push("one"));
    CHECK(ring.push(""));
    CHECK(ring.push("three"));
    CHECK(ring.lines().older == "one\n\nthree\n");
    CHECK(ring.lines().newer.empty());
    CHECK_EQ(ring.first(), 0U);
    CHECK_EQ(ring.next(), 3U);
    CHECK(ring.since(1).older == "\nthree\n");
    CHECK(ring.since(3).older.empty());
}

QIVEN_CASE(ring, wraps_whole) {
    // 16 bytes: "aaaaaa\n" twice is 14; the third starts at the front, the
    // first evicted, and the 2 bytes at the end stay unused.
    std::vector<char> storage(16);
    Ring ring(storage);
    CHECK(ring.push("aaaaaa"));
    CHECK(ring.push("bbbbbb"));
    CHECK(ring.push("cccccc"));
    CHECK(ring.lines().older == "bbbbbb\n");
    CHECK(ring.lines().newer == "cccccc\n");
    CHECK_EQ(ring.first(), 1U);
    // The fourth evicts the second: the older piece gone, one piece again.
    CHECK(ring.push("dddddd"));
    CHECK(ring.lines().older == "cccccc\ndddddd\n");
    CHECK(ring.lines().newer.empty());
    CHECK_EQ(ring.first(), 2U);
}

QIVEN_CASE(ring, refuses) {
    std::vector<char> storage(8);
    Ring ring(storage);
    CHECK(ring.push("abc"));
    CHECK(!ring.push("a\nb"));
    // 7 bytes and its '\n' take the whole span: refused; 6 fit.
    CHECK(!ring.push("1234567"));
    CHECK(ring.lines().older == "abc\n");
    CHECK_EQ(ring.next(), 1U);
    CHECK(ring.push("123456"));
    CHECK(ring.lines().older == "123456\n");
    CHECK_EQ(ring.first(), 1U);
}

QIVEN_CASE(ring, every_span) {
    std::size_t runs = 0;
    for (std::size_t bytes = 2; bytes <= 48; ++bytes) {
        std::vector<char> storage(bytes, '#');
        Ring ring(storage);
        std::vector<std::string> pushed;
        std::uint64_t seed = bytes;
        for (int i = 0; i < 300; ++i) {
            seed = seed * 6364136223846793005U + 1442695040888963407U;
            // A length from 0 to the largest the span takes, bytes - 2.
            const std::size_t length = static_cast<std::size_t>(seed >> 33U) % (bytes - 1);
            const std::string line = line_of(pushed.size(), length);
            REQUIRE(ring.push(line));
            pushed.push_back(line);
            const RingView v = ring.lines();
            // The newest is always held, and the pieces fit the span.
            REQUIRE(v.older.size() + v.newer.size() <= bytes);
            REQUIRE(holds(ring, pushed, 0));
            REQUIRE(holds(ring, pushed, pushed.size() > 3 ? pushed.size() - 3 : 0));
            ++runs;
        }
        CHECK(ring.first() > 0);
    }
    CHECK_EQ(runs, 47U * 300U);
}
