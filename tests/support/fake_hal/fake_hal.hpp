#pragma once

// The HAL's fake, qiven3_hal_fake: FakeCompute, a concrete type satisfying
// the concept Compute on the C++ standard library alone, so it builds and
// runs on every system. It reports the devices the test gives, keeps each
// buffer in host memory counted against its device's given memory, counts
// allocations, uploads, downloads and runs, and runs each kernel's CPU
// reference.
//
// device(capability) gives the first device that matches ("cpu": back end
// "cpu"; "gpu": any other back end; else that back end), else the test's gap
// for the capability (for "gpu", the first given for a back end not "cpu"),
// else no_device for "cpu" or "gpu" and not_built for a back end's name.
//
// The ownership tag: each fake holds a tag no other live fake holds, its
// state's address, and each buffer it gives carries it; upload, download and
// run compare the tags. A buffer another fake gave, a moved-from or null
// buffer, a device the fake was not given, a buffer of another device than
// the run's, a device id given twice, or a fake ending with its buffers live
// is a caller's mistake: it stops the process through memory::fail_fast.
//
// thread: one

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "hal/hal.hpp"

namespace qiven3::hal::fake {

struct FakeDevice {
    std::string id, back_end, name;
    std::uint64_t memory = 0;
};

struct Config {
    unsigned cpu_threads = 1;
    Vectors vectors;
    std::vector<FakeDevice> devices;  // each id once
    // A capability's gap when no device matches it, by "cpu", "gpu" or a
    // back end's name, in order.
    std::vector<std::pair<std::string, Reason>> gaps;
    Kernels kernels;
};

// What the fake did, each counted when it succeeds.
struct Counts {
    std::uint64_t allocations = 0, uploads = 0, downloads = 0, runs = 0;
};

class FakeCompute;

// A fake's state: the tag its buffers carry is this object's address.
struct State {
    std::vector<std::uint64_t> used;  // the bytes of live buffers, by device, as given
    Counts counts;
    std::uint64_t live = 0;  // the live buffers
};

// A buffer of host bytes, a move-only value; its end gives its bytes back
// to its device's count. A moved-from buffer holds no tag and no bytes.
class FakeBuffer {
public:
    FakeBuffer(FakeBuffer&& other) noexcept;
    FakeBuffer& operator=(FakeBuffer&& other) noexcept;
    FakeBuffer(const FakeBuffer&) = delete;
    FakeBuffer& operator=(const FakeBuffer&) = delete;
    ~FakeBuffer();

    [[nodiscard]] std::uint64_t size() const noexcept { return bytes_.size(); }

private:
    friend class FakeCompute;
    FakeBuffer(State& owner, std::size_t device, std::vector<std::byte> bytes) noexcept
        : bytes_(std::move(bytes)), owner_(&owner), device_(device) {}
    void release() noexcept;

    std::vector<std::byte> bytes_;
    State* owner_ = nullptr;  // the tag
    std::size_t device_ = 0;  // its index in the owner's devices
};

class FakeCompute {
public:
    using Buffer = FakeBuffer;

    // A device id given twice is a caller's mistake.
    explicit FakeCompute(Config config);
    // The tag is the state's address, so a fake neither moves nor copies.
    FakeCompute(const FakeCompute&) = delete;
    FakeCompute& operator=(const FakeCompute&) = delete;
    FakeCompute(FakeCompute&&) = delete;
    FakeCompute& operator=(FakeCompute&&) = delete;
    // Its buffers live is a caller's mistake.
    ~FakeCompute();

    void facts(Facts& out) const;
    [[nodiscard]] std::optional<Gap> device(std::string_view capability, Device& out) const;
    [[nodiscard]] FakeBuffer allocate(const Device& d, std::uint64_t size);
    void upload(FakeBuffer& b, std::span<const std::byte> bytes);
    void download(const FakeBuffer& b, std::span<std::byte> bytes);
    void run(const Device& d, const Launch<FakeBuffer>& launch);

    [[nodiscard]] const Counts& counts() const noexcept { return state_.counts; }
    // The bytes the device's live buffers hold, 0 for an id not given.
    [[nodiscard]] std::uint64_t used(std::string_view device_id) const noexcept;
    [[nodiscard]] std::uint64_t live() const noexcept { return state_.live; }

private:
    [[nodiscard]] std::size_t known(const Device& d) const noexcept;
    void ours(const FakeBuffer& b) const noexcept;

    Config config_;
    State state_;
};

static_assert(Buffer<FakeBuffer>, "FakeBuffer is a Buffer: move-only, with its size");
static_assert(!std::is_copy_constructible_v<FakeBuffer> && !std::is_copy_assignable_v<FakeBuffer>);
static_assert(std::is_nothrow_move_constructible_v<FakeBuffer> && std::is_nothrow_move_assignable_v<FakeBuffer>);
static_assert(Compute<FakeCompute>, "FakeCompute satisfies Compute");
#if defined(_MSC_VER) && defined(_WIN64) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0
static_assert(sizeof(FakeBuffer) == 40, "FakeBuffer's size is pinned: its bytes, the tag and the device");
#endif

}  // namespace qiven3::hal::fake
