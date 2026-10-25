#pragma once

// store_scan_case: the case a scan's end holds on any store, a template
// on the contract's Store, bound to each store by its own case file. A
// scan ends at each lane's length at its start: A, B, D and ACC appended
// to the implementer lane and C and M1 to the reviewer lane; three scans
// begun, the whole store, the reviewer lane alone and every lane after
// seq 1, and one header read from the first; then B forgotten by E through
// another object (its purge done), M2 appended through the scanning object
// and a lane after both, tester, made through the other. Each scan begun
// before then gives what was committed before it began, each once, in
// order, and nothing after; a scan begun after gives everything.
//
// thread: one.

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entry/header.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "support/fixtures/stage_book.hpp"

namespace qiven3::test::scan_case {

using stage_book::Stored;
using Chars = memory::Allocator<char>;

inline constexpr std::string_view scope = "project:qiven";
inline constexpr std::string_view implementer = "implementer";
inline constexpr std::string_view reviewer = "reviewer";

// Each id cut to its first eight hex digits, then a space.
inline std::string cut(const std::vector<Stored>& lines) {
    std::string ids;
    for (const Stored& s : lines) {
        ids += stage_book::id_of(s).substr(7, 8) + " ";
    }
    return ids;
}

// What a scan still gives, at most `most` headers, cut so.
template <store::Scan H>
std::string rest(H& scan, const Chars& chars, std::size_t most = std::numeric_limits<std::size_t>::max()) {
    std::string ids;
    store::Header h(chars);
    for (std::size_t n = 0; n < most && scan.next(h); ++n) {
        ids += std::string(h.value(entry::header::Slot::id).substr(7, 8)) + " ";
    }
    return ids;
}

// store and other: two objects on one empty store, read-write.
template <store::Store S>
void scan_ends_at_start(Context& qiven3_ctx, S& store, S& other, const stage_book::Book& book, const Chars& chars) {
    const std::vector<Stored> mine = {book.stored("A"), book.stored("B"), book.stored("D"), book.stored("ACC")};
    const std::vector<Stored> theirs = {book.stored("C"), book.stored("M1")};
    const std::vector<Stored> later = {book.stored("M2")};
    const std::vector<Stored> forgets = {book.stored("E")};
    const std::vector<Stored> tester = {book.on_lane("A", "tester")};
    (void)store.append(scope, implementer, std::nullopt, stage_book::views(mine));
    (void)store.append(scope, reviewer, std::nullopt, stage_book::views(theirs));

    auto whole = store.scan(store::RangeView{});
    auto lane = store.scan(store::RangeView{scope, reviewer, std::nullopt});
    auto after = store.scan(store::RangeView{std::nullopt, std::nullopt, 1});
    CHECK_EQ(rest(whole, chars, 1), cut({mine[0]}));

    const std::string acc = stage_book::id_of(mine[3]);
    const std::string m1 = stage_book::id_of(theirs[1]);
    const std::vector<store::GuardView> guard = {store::GuardView{scope, implementer, acc},
                                                 store::GuardView{scope, reviewer, m1}};
    store::Vector<store::Id> erased(chars);
    // The Written a temporary: its share of the frame held for the forget alone.
    CHECK(other.forget(scope, implementer, acc, stage_book::views(forgets), guard, erased).purge.done);
    CHECK_EQ(erased.size(), 1U);
    (void)store.append(scope, reviewer, m1, stage_book::views(later));
    (void)other.append(scope, "tester", std::nullopt, stage_book::views(tester));

    CHECK_EQ(rest(whole, chars), cut({mine[1], mine[2], mine[3], theirs[0], theirs[1]}));
    CHECK_EQ(rest(lane, chars), cut(theirs));
    CHECK_EQ(rest(after, chars), cut({mine[1], mine[2], mine[3], theirs[1]}));
    auto now = store.scan(store::RangeView{});
    CHECK_EQ(rest(now, chars), cut(mine) + cut(forgets) + cut(theirs) + cut(later) + cut(tester));
    auto theirs_now = other.scan(store::RangeView{scope, reviewer, 1});
    CHECK_EQ(rest(theirs_now, chars), cut({theirs[1], later[0]}));
}

}  // namespace qiven3::test::scan_case
