// store/sqlite's fault runs of the create and of the forget
// (store_sqlite_memory_test.cpp's course of steps), each in parts by its k
// (support/faults/parts.hpp), each part within 30 s on every job:
// faults_create_part1 to _part25 and faults_forget_part1 to _part35. The
// parts' first k, a row a stretch: the create's 100 k a stretch, the last
// to its completing run (750 on Windows, 740 on Linux); the forget's 100 k
// a stretch to k 200, 50 to k 300, 100 to k 700, then 60 and 45, the last
// to its completing run (850 on Windows, 853 on Linux). Beside each row,
// the seconds the slowest CI Windows job yet ran it in (windows-msvc at
// 486b505, its files' steps there at about a fifth of their usual pace): a
// stretch past 30 s there is cut in three parts, one past 60 s in four, so
// that each part takes about a third of that or less, 30 s or under; even
// in k, but for the forget's k 101 to 200, whose runs cost the most past
// k 166, cut at even measured seconds. Where its seconds are named, the
// 30 s is so measured on that job; the forget's rows from k 301 to 700 hit
// the watcher there, timed out at 90 s or killed past 60 s, so that their
// seconds are a lower bound and their parts' 30 s an estimate, not a
// measure.

#include <array>
#include <cstddef>
#include <string_view>

#include "runner/runner.hpp"
#include "support/faults/parts.hpp"

namespace qiven3::test {
void store_sqlite_fault_steps(Context& qiven3_ctx, std::string_view group, FaultRange range);
}  // namespace qiven3::test

namespace {

constexpr qiven3::test::FaultParts<25> create_parts{{{
    {0, 1},                                  // 3.8 s
    {0, 101}, {0, 126}, {0, 151}, {0, 176},  // 10.8, 12.2, 14.6, 14.9 s
    {0, 201}, {0, 226}, {0, 251}, {0, 276},  // 17.4, 17.1, 17.3, 19.1 s
    {0, 301}, {0, 326}, {0, 351}, {0, 376},  // 11.2, 11.3, 11.5, 13.5 s
    {0, 401}, {0, 434}, {0, 467},            // 49.1 s
    {0, 501}, {0, 534}, {0, 567},            // 47.3 s
    {0, 601}, {0, 634}, {0, 667},            // 46.2 s
    {0, 701}, {0, 718}, {0, 735},            // 53.6 s
}}};
constexpr qiven3::test::FaultParts<35> forget_parts{{{
    {0, 1},                                  // 4.3 s
    {0, 101}, {0, 167}, {0, 184},            // 39.7 s
    {0, 201}, {0, 218}, {0, 235},            // 42.5 s
    {0, 251}, {0, 268}, {0, 285},            // 43.7 s
    {0, 301}, {0, 326}, {0, 351}, {0, 376},  // past 90 s
    {0, 401}, {0, 426}, {0, 451}, {0, 476},  // past 90 s
    {0, 501}, {0, 526}, {0, 551}, {0, 576},  // past 90 s
    {0, 601}, {0, 626}, {0, 651}, {0, 676},  // past 60 s
    {0, 701}, {0, 721}, {0, 741},            // 36.6 s
    {0, 761}, {0, 776}, {0, 791},            // 43.3 s
    {0, 806}, {0, 821}, {0, 836},            // 48.6 s
}}};
static_assert(create_parts.whole() && forget_parts.whole());

void create_part(qiven3::test::Context& qiven3_ctx, std::size_t i) {
    CHECK(qiven3::test::registered("store_sqlite.faults_create", create_parts.from.size()));
    qiven3::test::store_sqlite_fault_steps(qiven3_ctx, "create", create_parts.range(i, 0));
}

void forget_part(qiven3::test::Context& qiven3_ctx, std::size_t i) {
    CHECK(qiven3::test::registered("store_sqlite.faults_forget", forget_parts.from.size()));
    qiven3::test::store_sqlite_fault_steps(qiven3_ctx, "forget", forget_parts.range(i, 0));
}

}  // namespace

QIVEN3_FAULT_PART(store_sqlite, faults_create, 1, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 2, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 3, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 4, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 5, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 6, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 7, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 8, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 9, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 10, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 11, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 12, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 13, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 14, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 15, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 16, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 17, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 18, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 19, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 20, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 21, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 22, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 23, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 24, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_create, 25, create_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 1, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 2, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 3, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 4, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 5, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 6, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 7, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 8, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 9, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 10, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 11, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 12, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 13, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 14, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 15, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 16, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 17, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 18, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 19, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 20, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 21, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 22, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 23, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 24, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 25, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 26, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 27, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 28, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 29, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 30, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 31, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 32, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 33, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 34, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 35, forget_part)
