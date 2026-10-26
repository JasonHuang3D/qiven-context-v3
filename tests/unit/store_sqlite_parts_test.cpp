// store/sqlite's fault runs of the create and of the forget
// (store_sqlite_memory_test.cpp's course of steps), each in parts by its k
// (support/faults/parts.hpp), each part within 30 s on every job:
// faults_create_part1 to _part8 and faults_forget_part1 to _part9. The
// parts' first k: the create's 100 a part, the last to its completing run
// (750 on Windows, 740 on Linux); the forget's 100 a part to k 700, then 60,
// the last to its completing run (850 on Windows, 853 on Linux).

#include <array>
#include <cstddef>
#include <string_view>

#include "runner/runner.hpp"
#include "support/faults/parts.hpp"

namespace qiven3::test {
void store_sqlite_fault_steps(Context& qiven3_ctx, std::string_view group, FaultRange range);
}  // namespace qiven3::test

namespace {

constexpr qiven3::test::FaultParts<8> create_parts{
    {{{0, 1}, {0, 101}, {0, 201}, {0, 301}, {0, 401}, {0, 501}, {0, 601}, {0, 701}}}};
constexpr qiven3::test::FaultParts<9> forget_parts{
    {{{0, 1}, {0, 101}, {0, 201}, {0, 301}, {0, 401}, {0, 501}, {0, 601}, {0, 701}, {0, 761}}}};
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
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 1, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 2, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 3, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 4, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 5, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 6, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 7, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 8, forget_part)
QIVEN3_FAULT_PART(store_sqlite, faults_forget, 9, forget_part)
