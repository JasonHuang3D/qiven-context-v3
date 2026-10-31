// kept_bed's unit for the analysis preset: the header, a scratch store, a
// file's entries built and kept by a write on the fake, as write/kept's
// cases keep them.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "support/fixtures/kept_bed.hpp"

namespace qiven3::test::kept_bed {

std::uint64_t write_once() {
    const Scratch scratch{fresh};
    FakePlatform fake(scratch.config());
    std::vector<std::byte> pages(mib);
    Arena arena(pages);
    Store store;
    const Entries entries(arena, "implementer/s7", 2, 1, true);
    const std::uint64_t n = kept::write(fake, scratch.store(), kept::Command::write, "implementer/s7", entries.view(),
                                        store, appends(store, entries), arena);
    return n + static_cast<std::uint64_t>(count(fake.calls(), "rename")) +
           (refusal([] {}).has_value() ? 1U : 0U) + (fault([] {}).has_value() ? 1U : 0U);
}

}  // namespace qiven3::test::kept_bed
