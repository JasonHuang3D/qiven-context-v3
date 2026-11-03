#pragma once

// scan_case: the case a scan's end holds on any store, a template on the
// contract's Store over plain records (records.hpp), bound to each store
// by its own case file. A
// scan ends at each lane's length at its start: A, B, D and ACC appended
// to lane one and C and M1 to lane two; three scans
// begun, the whole store, lane two alone and every lane after
// seq 1, and one record read from the first; then B forgotten by E through
// another object (its purge done), M2 appended through the scanning object
// and a lane after both, tester, made through the other. Each scan begun
// before then gives what was committed before it began, each once, in
// order, and nothing after; a scan begun after gives everything; verify
// counts what was written.
//
// thread: one.

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "qiven/recordlog/contract.hpp"
#include "support/stores/records.hpp"

namespace qiven::test::scan_case {

using records::Rec;
using Chars = qiven::memory::Allocator<char>;

inline constexpr std::string_view scope = "group:primary";
inline constexpr std::string_view lane_one = "lane-one";
inline constexpr std::string_view lane_two = "lane-two";

// The records the case writes: lane one's A, B, D and ACC, lane two's
// C and M1 and then M2, E forgetting B after ACC, and tester's T.
struct Lanes {
    std::vector<Rec> mine = records::chain(scope, lane_one, {"A", "B", "D", "ACC"});
    std::vector<Rec> theirs = records::chain(scope, lane_two, {"C", "M1"});
    std::vector<Rec> later = {records::rec("M2", scope, lane_two, 3, "M1")};
    std::vector<Rec> forgets = {records::forget("E", scope, lane_one, 5, "ACC", "B")};
    std::vector<Rec> tester = {records::rec("T", scope, "tester", 1)};
};

// What verify counts once the case has run: nine entries, B's body erased
// and E's none, one forget, three lanes.
inline constexpr qiven::recordlog::Counts written{9, 7, 1, 3};

// Each id, then a space.
inline std::string cut(const std::vector<Rec>& lines) {
    std::string ids;
    for (const Rec& r : lines) {
        ids += records::id_of(r) + " ";
    }
    return ids;
}

// What a scan still gives, at most `most` records' ids, cut so.
template <qiven::recordlog::Scan H>
std::string rest(H& scan, const Chars& chars, std::size_t most = std::numeric_limits<std::size_t>::max()) {
    std::string ids;
    qiven::recordlog::Record r(chars);
    for (std::size_t n = 0; n < most && scan.next(r); ++n) {
        ids += std::string(r.id.view()) + " ";
    }
    return ids;
}

// store and other: two objects on one empty store, read-write.
template <qiven::recordlog::Store S>
void scan_ends_at_start(qiven::runner::Context& qiven_ctx, S& store, S& other, const Chars& chars) {
    const Lanes lanes;
    const std::vector<Rec>& mine = lanes.mine;
    const std::vector<Rec>& theirs = lanes.theirs;
    const std::vector<Rec>& later = lanes.later;
    const std::vector<Rec>& forgets = lanes.forgets;
    const std::vector<Rec>& tester = lanes.tester;
    (void)store.append(scope, lane_one, std::nullopt, records::views(mine));
    (void)store.append(scope, lane_two, std::nullopt, records::views(theirs));

    auto whole = store.scan(qiven::recordlog::RangeView{});
    auto lane = store.scan(qiven::recordlog::RangeView{scope, lane_two, std::nullopt});
    auto after = store.scan(qiven::recordlog::RangeView{std::nullopt, std::nullopt, 1});
    CHECK_EQ(rest(whole, chars, 1), cut({mine[0]}));

    const std::string& acc = records::id_of(mine[3]);
    const std::string& m1 = records::id_of(theirs[1]);
    const std::vector<qiven::recordlog::GuardView> guard = {qiven::recordlog::GuardView{scope, lane_one, acc},
                                                 qiven::recordlog::GuardView{scope, lane_two, m1}};
    qiven::recordlog::Vector<qiven::recordlog::Id> erased(chars);
    // The Written a temporary: its share of the frame held for the forget alone.
    CHECK(other.forget(scope, lane_one, acc, records::views(forgets), guard, erased).purge.done);
    CHECK_EQ(erased.size(), 1U);
    (void)store.append(scope, lane_two, m1, records::views(later));
    (void)other.append(scope, "tester", std::nullopt, records::views(tester));

    CHECK_EQ(rest(whole, chars), cut({mine[1], mine[2], mine[3], theirs[0], theirs[1]}));
    CHECK_EQ(rest(lane, chars), cut(theirs));
    CHECK_EQ(rest(after, chars), cut({mine[1], mine[2], mine[3], theirs[1]}));
    auto now = store.scan(qiven::recordlog::RangeView{});
    CHECK_EQ(rest(now, chars), cut(mine) + cut(forgets) + cut(theirs) + cut(later) + cut(tester));
    auto theirs_now = other.scan(qiven::recordlog::RangeView{scope, lane_two, 1});
    CHECK_EQ(rest(theirs_now, chars), cut({theirs[1], later[0]}));
    CHECK(store.verify() == written);
}

}  // namespace qiven::test::scan_case
