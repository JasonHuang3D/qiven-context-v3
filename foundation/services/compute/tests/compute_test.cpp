// compute's own cases, over its fake and the devices, sizes and kernel they
// write themselves: facts as given (H1); a capability's absence a gap, a
// value (H2); allocate past the device's memory and a span of another size
// refused (H3); a name no kernel has refused (H4); a kernel's CPU reference
// run on the fake (H5); facts and device writing the caller's out on its
// arena alone, within the 64 KiB these cases give, a byte short refused;
// their allocations failed in turn. A caller's mistake ends the process: its
// case is fail_demo.cpp's, which test_fail.py runs.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "qiven/compute/fake_hal.hpp"
#include "qiven/compute/hal.hpp"
#include "qiven/faults/faults.hpp"
#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace compute = qiven::compute;
using compute::fake::Config;
using compute::fake::FakeBuffer;
using compute::fake::FakeCompute;
using compute::fake::FakeDevice;
using qiven::memory::Arena;
using qiven::memory::MemoryCode;
using qiven::memory::MemoryError;

constexpr std::uint64_t gib = std::uint64_t{1} << 30;
// The arena these cases give facts and device, their own 64 KiB.
constexpr std::size_t arena_budget = std::size_t{64} << 10;

FakeDevice gpu() { return FakeDevice{"gpu0", "cuda", "a GPU of 12 GiB", 12 * gib}; }
FakeDevice cpu() { return FakeDevice{"cpu0", "cpu", "the CPU", std::uint64_t{1} << 20}; }

// add_i64: three buffers of int64 in the host's order, out = a + b.
void add_i64(std::span<const std::span<std::byte>> buffers, std::span<const std::int64_t>,
             const std::array<std::uint32_t, 3>&) {
    if (buffers.size() != 3 || buffers[0].size() != buffers[1].size() || buffers[0].size() != buffers[2].size() ||
        buffers[0].size() % 8 != 0) {
        throw compute::HalError(compute::Code::launch, "run", "add_i64 takes three buffers of one size, of int64");
    }
    for (std::size_t at = 0; at < buffers[0].size(); at += 8) {
        std::int64_t a = 0, b = 0;
        std::memcpy(&a, buffers[0].subspan(at, 8).data(), 8);
        std::memcpy(&b, buffers[1].subspan(at, 8).data(), 8);
        const auto sum = static_cast<std::int64_t>(static_cast<std::uint64_t>(a) + static_cast<std::uint64_t>(b));
        std::memcpy(buffers[2].subspan(at, 8).data(), &sum, 8);
    }
}

Config config(std::vector<FakeDevice> devices, unsigned threads = 1) {
    Config c;
    c.cpu_threads = threads;
    c.devices = std::move(devices);
    c.kernels.add("add_i64", add_i64);
    return c;
}

bool same(const compute::Device& d, const FakeDevice& f) {
    return std::string_view(d.id) == f.id && std::string_view(d.back_end) == f.back_end &&
           std::string_view(d.name) == f.name && d.memory == f.memory;
}

std::span<const std::byte> bytes_of(const std::vector<std::int64_t>& values) {
    return std::as_bytes(std::span<const std::int64_t>(values));
}

// The code of the HalError op throws, or nullopt when it throws none.
template <class Op>
std::optional<compute::Code> refused(Op op) {
    try {
        op();
    } catch (const compute::HalError& e) {
        return e.code;
    }
    return std::nullopt;
}

bool budget_error(const MemoryError& e) { return e.code == MemoryCode::budget; }

}  // namespace

QIVEN_CASE(compute, facts) {
    // H1: the fake given 4 threads, AVX2 and one GPU of 12 GiB: facts() the same.
    Config c = config({gpu()}, 4);
    c.vectors.set(static_cast<std::size_t>(compute::Vector::avx2));
    const FakeCompute fake(std::move(c));
    Arena& arena = qiven_ctx.counted.arena(arena_budget);
    compute::Facts f(arena);
    fake.facts(f);
    CHECK_EQ(f.cpu_threads, 4U);
    compute::Vectors avx2;
    avx2.set(static_cast<std::size_t>(compute::Vector::avx2));
    CHECK(f.vectors == avx2);
    CHECK_EQ(compute::name(compute::Vector::avx2), std::string_view("avx2"));
    CHECK_EQ(f.devices.size(), 1U);
    CHECK(f.devices.size() == 1 && same(f.devices[0], gpu()));
}

QIVEN_CASE(compute, gap) {
    // H2: no GPU: device("gpu") a gap, no_device; no back end: device("cuda")
    // a gap, not_built. A gap is a value, not an error.
    const FakeCompute fake(config({cpu()}));
    Arena& arena = qiven_ctx.counted.arena(arena_budget);
    compute::Device d(arena);
    const std::optional<compute::Gap> no_gpu = fake.device("gpu", d);
    CHECK(no_gpu.has_value() && no_gpu->reason == compute::Reason::no_device);
    const std::optional<compute::Gap> no_cuda = fake.device("cuda", d);
    CHECK(no_cuda.has_value() && no_cuda->reason == compute::Reason::not_built);
    CHECK_EQ(compute::name(compute::Reason::not_built), std::string_view("not_built"));
    CHECK(!fake.device("cpu", d).has_value());
    CHECK(same(d, cpu()));
}

QIVEN_CASE(compute, memory_and_size) {
    // H3: allocate past the device's memory: memory; upload or download of a
    // span of another size than the buffer's: size; only what succeeded counted.
    FakeCompute fake(config({gpu()}));
    Arena& arena = qiven_ctx.counted.arena(arena_budget);
    compute::Device d(arena);
    CHECK(!fake.device("gpu", d).has_value());
    CHECK(refused([&] { (void)fake.allocate(d, 12 * gib + 1); }) == compute::Code::memory);
    FakeBuffer buffer = fake.allocate(d, 16);
    CHECK_EQ(buffer.size(), 16U);
    std::array<std::byte, 8> eight{};
    std::array<std::byte, 24> twenty_four{};
    CHECK(refused([&] { fake.upload(buffer, eight); }) == compute::Code::size);
    CHECK(refused([&] { fake.download(buffer, twenty_four); }) == compute::Code::size);
    CHECK_EQ(compute::name(compute::Code::size), std::string_view("size"));
    const compute::fake::Counts& counts = fake.counts();
    CHECK_EQ(counts.allocations, 1U);
    CHECK_EQ(counts.uploads, 0U);
    CHECK_EQ(counts.downloads, 0U);
    CHECK_EQ(counts.runs, 0U);
}

QIVEN_CASE(compute, kernel) {
    // H4: run of a name no kernel has: kernel, its detail naming it.
    FakeCompute fake(config({cpu()}));
    Arena& arena = qiven_ctx.counted.arena(arena_budget);
    compute::Device d(arena);
    CHECK(!fake.device("cpu", d).has_value());
    bool named = false;
    try {
        fake.run(d, compute::Launch<FakeBuffer>{"no_such_kernel", {}, {}, {1, 1, 1}});
    } catch (const compute::HalError& e) {
        named = e.code == compute::Code::kernel && e.detail.view().find("no_such_kernel") != std::string_view::npos;
    }
    CHECK(named);
    CHECK_EQ(fake.counts().runs, 0U);
}

QIVEN_CASE(compute, reference) {
    // H5: a registered kernel adding two vectors of 1,000 int64 on the fake:
    // its CPU reference's result.
    std::vector<std::int64_t> a(1000), b(1000), sum(1000), out(1000);
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto n = static_cast<std::int64_t>(i);
        a[i] = 7 * n - 3000;
        b[i] = (i % 2 == 0 ? 1 : -1) * n * n * 1000003;
        sum[i] = a[i] + b[i];
    }
    const std::uint64_t size = 8 * a.size();
    FakeCompute fake(config({cpu()}, 2));
    Arena& arena = qiven_ctx.counted.arena(arena_budget);
    compute::Device d(arena);
    CHECK(!fake.device("cpu", d).has_value());
    {
        std::array<FakeBuffer, 3> buffers = {fake.allocate(d, size), fake.allocate(d, size), fake.allocate(d, size)};
        fake.upload(buffers[0], bytes_of(a));
        fake.upload(buffers[1], bytes_of(b));
        const std::array<FakeBuffer*, 3> launched = {&buffers[0], &buffers[1], &buffers[2]};
        fake.run(d, compute::Launch<FakeBuffer>{"add_i64", launched, {}, {1000, 1, 1}});
        fake.download(buffers[2], std::as_writable_bytes(std::span<std::int64_t>(out)));
        CHECK_EQ(fake.used("cpu0"), 3 * size);
        CHECK_EQ(fake.live(), 3U);
    }
    CHECK(out == sum);
    const compute::fake::Counts& counts = fake.counts();
    CHECK_EQ(counts.allocations, 3U);
    CHECK_EQ(counts.uploads, 2U);
    CHECK_EQ(counts.downloads, 1U);
    CHECK_EQ(counts.runs, 1U);
    CHECK_EQ(fake.used("cpu0"), 0U);
    CHECK_EQ(fake.live(), 0U);
}

QIVEN_CASE(compute, arena) {
    // facts and device write the caller's out on its arena, within the
    // case's 64 KiB, nothing held once out ends; a byte short of a peak,
    // MemoryError budget. allocate, upload, download and run place nothing
    // on it.
    const FakeCompute fake(config({cpu(), gpu()}, 2));
    std::size_t facts_peak = 0, device_peak = 0;
    {
        Arena& arena = qiven_ctx.counted.arena(arena_budget);
        {
            compute::Facts f(arena);
            fake.facts(f);
            CHECK_EQ(f.devices.size(), 2U);
            facts_peak = arena.peak();
        }
        CHECK(facts_peak <= arena_budget);
        CHECK_EQ(arena.used(), 0U);
    }
    {
        Arena& arena = qiven_ctx.counted.arena(arena_budget);
        {
            compute::Device d(arena);
            CHECK(!fake.device("gpu", d).has_value());
            device_peak = arena.peak();
        }
        CHECK(device_peak <= arena_budget);
        CHECK_EQ(arena.used(), 0U);
    }
    {
        FakeCompute mutable_fake(config({cpu(), gpu()}, 2));
        Arena& arena = qiven_ctx.counted.arena(arena_budget);
        compute::Device d(arena);
        CHECK(!mutable_fake.device("cpu", d).has_value());
        const std::size_t held = arena.used(), peak = arena.peak();
        std::vector<std::int64_t> values(64);
        std::array<FakeBuffer, 3> buffers = {mutable_fake.allocate(d, 8 * 64), mutable_fake.allocate(d, 8 * 64),
                                             mutable_fake.allocate(d, 8 * 64)};
        mutable_fake.upload(buffers[0], bytes_of(values));
        mutable_fake.upload(buffers[1], bytes_of(values));
        const std::array<FakeBuffer*, 3> launched = {&buffers[0], &buffers[1], &buffers[2]};
        mutable_fake.run(d, compute::Launch<FakeBuffer>{"add_i64", launched, {}, {64, 1, 1}});
        mutable_fake.download(buffers[2], std::as_writable_bytes(std::span<std::int64_t>(values)));
        CHECK_EQ(arena.used(), held);
        CHECK_EQ(arena.peak(), peak);
    }
    std::cout << "compute.arena: facts' peak " << facts_peak << ", device's " << device_peak << ", budget "
              << arena_budget << "\n";
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    if (facts_peak > 0) {
        Arena& shorter = qiven_ctx.counted.arena(facts_peak - 1);
        bool refused_budget = false;
        try {
            compute::Facts f(shorter);
            fake.facts(f);
        } catch (const MemoryError& e) {
            refused_budget = budget_error(e);
        }
        CHECK(refused_budget);
    }
    if (device_peak > 0) {
        Arena& shorter = qiven_ctx.counted.arena(device_peak - 1);
        bool refused_budget = false;
        try {
            compute::Device d(shorter);
            (void)fake.device("gpu", d);
        } catch (const MemoryError& e) {
            refused_budget = budget_error(e);
        }
        CHECK(refused_budget);
    }
#endif
}

QIVEN_CASE(compute, faults) {
#if defined(QIVEN_FAULTS) && (!defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0)
    // Each allocation facts and device make on the caller's arena failed in
    // turn: each failed run MemoryError, nothing held.
    const FakeCompute fake(config({cpu(), gpu()}, 2));
    std::vector<std::byte> pages(arena_budget);
    qiven::memory::Countdown countdown;
    std::optional<Arena> arena;
    const auto check = [&](std::size_t) { return arena->used() == 0; };
    std::size_t devices = 0;
    const auto facts = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        compute::Facts f(*arena);
        fake.facts(f);
        devices = f.devices.size();
    };
    const qiven::test::FaultRuns facts_runs =
        qiven::test::run_faults<MemoryError>(qiven_ctx.log, "compute.facts", countdown, facts, check);
    bool found = false;
    const auto device = [&] {
        arena.emplace(pages);
        arena->attach(&countdown);
        compute::Device d(*arena);
        found = !fake.device("gpu", d).has_value() && same(d, gpu());
    };
    const qiven::test::FaultRuns device_runs =
        qiven::test::run_faults<MemoryError>(qiven_ctx.log, "compute.device", countdown, device, check);
    std::cout << "compute.faults: facts' allocations " << facts_runs.typed << ", device's " << device_runs.typed
              << "\n";
    for (const qiven::test::FaultRuns& runs : {facts_runs, device_runs}) {
        CHECK(runs.completed_at > 0);
        CHECK_EQ(runs.completed_at, runs.typed + 1);
        CHECK_EQ(runs.untyped, 0U);
        CHECK_EQ(runs.unchecked, 0U);
    }
    CHECK(facts_runs.typed > 0);
    CHECK_EQ(devices, 2U);
    CHECK(found);
    arena.reset();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS, or with MSVC's debug containers");
#endif
}
