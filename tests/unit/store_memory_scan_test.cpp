// store/memory's binding of the case a scan's end (store_scan_case.hpp):
// two objects on one state on the fake, the case run on them.

#include <cstddef>
#include <memory>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "runner/runner.hpp"
#include "store/contract.hpp"
#include "store/memory.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/fixtures/stage_book.hpp"
#include "support/fixtures/store_scan_case.hpp"

namespace {

using Fake = qiven3::platform::fake::FakePlatform;
using Store = qiven3::store::MemoryStore<Fake>;

// The fake, the fixtures, the state's pages and the state, and the
// caller's arena; on the heap.
struct Rig {
    Rig()
        : fake(qiven3::platform::fake::Config{}), book(fake), heap(fake.pages(std::size_t{8} << 20U)),
          state(heap.bytes(), qiven3::store::Limits{}), pages(std::size_t{1} << 20U), arena(pages), chars(arena) {}
    Fake fake;
    qiven3::test::stage_book::Book book;
    qiven3::platform::fake::FakePages heap;
    qiven3::store::state::State state;
    std::vector<std::byte> pages;
    qiven3::memory::Arena arena;
    qiven3::memory::Allocator<char> chars;
};

}  // namespace

QIVEN3_CASE(store_memory, scan_at_start) {
    const auto rig = std::make_unique<Rig>();
    Store store(rig->state, rig->fake);
    Store other(rig->state, rig->fake);
    qiven3::test::scan_case::scan_ends_at_start(qiven3_ctx, store, other, rig->book, rig->chars);
}
