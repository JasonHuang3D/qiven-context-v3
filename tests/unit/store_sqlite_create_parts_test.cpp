// store/sqlite's refused create (store_sqlite_create_test.cpp), its fault
// runs in parts (support/faults/parts.hpp): create_refused_part1 to
// _part127. A run costs about the same at any point of any pass: the create
// refused there, the check that nothing is left, a second create and the
// directory's removal, all on files. A part is its runs, six at most, so
// that a part stays a few seconds on every job, and within its bound when
// a job's files run several times slower than their usual pace for a
// while. The parts' first points come from the stretches below: a stretch
// a row, each cut in parts of its runs from its first k to its last part's
// first k, the last part of the case running to the heap's completing run.

#include <array>
#include <cstddef>

#include "qiven/runner/runner.hpp"
#include "support/faults/parts.hpp"

namespace qiven3::test {
void store_sqlite_create_refused(qiven::runner::Context& qiven_ctx, const std::array<FaultRange, 4>& ranges);
}  // namespace qiven3::test

namespace {

// Pass pass cut in parts of runs k each, the first from k from, the last
// from k last, each to the next part's first point.
struct Stretch {
    std::size_t pass = 0;
    std::size_t from = 1;
    std::size_t last = 1;
    std::size_t runs = 1;
};

// The passes: 0 the platform's calls (6), 1 the arena's allocations (2), 2
// the engine calls (95), 3 the engine heap's allocations (650 on Windows,
// 640 on Linux), each to its completing run.
constexpr std::array<Stretch, 4> stretches = {{
    {0, 1, 1, 6},    // the platform's 6
    {1, 1, 1, 6},    // the arena's 2, the engine calls' first 4
    {2, 5, 95, 6},   // the engine calls from k 5
    {3, 1, 649, 6},  // the heap's
}};

constexpr std::size_t count() {
    std::size_t n = 0;
    for (const Stretch& s : stretches) {
        n += (s.last - s.from) / s.runs + 1;
    }
    return n;
}

constexpr qiven3::test::FaultParts<count()> cut() {
    qiven3::test::FaultParts<count()> out{};
    std::size_t i = 0;
    for (const Stretch& s : stretches) {
        for (std::size_t k = s.from; k <= s.last; k += s.runs) {
            out.from.at(i) = {s.pass, k};
            ++i;
        }
    }
    return out;
}

constexpr auto parts = cut();
static_assert(parts.whole() && parts.from.size() == 127);

void create_part(qiven::runner::Context& qiven_ctx, std::size_t i) {
    CHECK(qiven3::test::registered("store_sqlite.create_refused", parts.from.size()));
    qiven3::test::store_sqlite_create_refused(
        qiven_ctx, {parts.range(i, 0), parts.range(i, 1), parts.range(i, 2), parts.range(i, 3)});
}

}  // namespace

QIVEN3_FAULT_PART(store_sqlite, create_refused, 1, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 2, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 3, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 4, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 5, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 6, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 7, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 8, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 9, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 10, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 11, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 12, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 13, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 14, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 15, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 16, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 17, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 18, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 19, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 20, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 21, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 22, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 23, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 24, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 25, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 26, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 27, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 28, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 29, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 30, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 31, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 32, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 33, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 34, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 35, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 36, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 37, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 38, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 39, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 40, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 41, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 42, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 43, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 44, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 45, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 46, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 47, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 48, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 49, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 50, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 51, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 52, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 53, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 54, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 55, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 56, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 57, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 58, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 59, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 60, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 61, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 62, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 63, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 64, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 65, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 66, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 67, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 68, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 69, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 70, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 71, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 72, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 73, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 74, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 75, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 76, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 77, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 78, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 79, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 80, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 81, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 82, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 83, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 84, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 85, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 86, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 87, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 88, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 89, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 90, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 91, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 92, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 93, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 94, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 95, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 96, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 97, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 98, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 99, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 100, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 101, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 102, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 103, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 104, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 105, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 106, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 107, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 108, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 109, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 110, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 111, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 112, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 113, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 114, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 115, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 116, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 117, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 118, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 119, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 120, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 121, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 122, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 123, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 124, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 125, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 126, create_part)
QIVEN3_FAULT_PART(store_sqlite, create_refused, 127, create_part)
