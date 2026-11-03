// A caller's mistake on compute's fake, run as a process by
// foundation/services/compute/tests/test_fail.py; never a CTest test itself,
// since its case ends the process (H8): an upload into a buffer another fake
// gave stops it through qiven::memory::fail_fast.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>

#include "qiven/compute/fake_hal.hpp"
#include "qiven/compute/hal.hpp"
#include "qiven/memory/arena.hpp"
#include "qiven/runner/runner.hpp"

QIVEN_CASE(compute, foreign_buffer) {
    namespace compute = qiven::compute;
    const compute::fake::FakeDevice cpu{"cpu0", "cpu", "the CPU", std::uint64_t{1} << 20};
    compute::fake::Config ours, theirs;
    ours.devices = {cpu};
    theirs.devices = {cpu};
    compute::fake::FakeCompute fake(std::move(ours));
    compute::fake::FakeCompute other(std::move(theirs));
    compute::Device d(qiven_ctx.counted.arena(4096));
    CHECK(!other.device("cpu", d).has_value());
    compute::fake::FakeBuffer buffer = other.allocate(d, 8);
    const std::array<std::byte, 8> bytes{};
    std::cout << "before\n";
    std::cout.flush();
    fake.upload(buffer, bytes);
}
