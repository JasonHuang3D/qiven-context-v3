// store/memory's binding of the case a scan's end (scan_case.hpp) over
// plain records with no admit hook: two objects on one state on the fake,
// the case run on them.

#include <cstddef>
#include <memory>
#include <vector>

#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"
#include "store/contract.hpp"
#include "store/memory.hpp"
#include "qiven/platform/fake/fake_platform.hpp"
#include "support/stores/scan_case.hpp"

namespace {

using Fake = qiven::platform::fake::FakePlatform;
using Store = qiven3::store::MemoryStore<Fake>;

// The fake, the state's pages and the state, and the caller's arena; on
// the heap.
struct Rig {
    Rig()
        : fake(qiven::platform::fake::Config{}), heap(fake.pages(std::size_t{8} << 20U)),
          state(heap.bytes(), qiven3::store::Limits{}), pages(std::size_t{1} << 20U), arena(pages), chars(arena) {}
    Fake fake;
    qiven::platform::fake::FakePages heap;
    qiven3::store::state::State state;
    std::vector<std::byte> pages;
    qiven::memory::Arena arena;
    qiven::memory::Allocator<char> chars;
};

}  // namespace

QIVEN_CASE(store_memory, scan_at_start) {
    const auto rig = std::make_unique<Rig>();
    Store store(rig->state, rig->fake);
    Store other(rig->state, rig->fake);
    qiven::test::scan_case::scan_ends_at_start(qiven_ctx, store, other, rig->chars);
    CHECK(other.verify() == qiven::test::scan_case::written);
}
