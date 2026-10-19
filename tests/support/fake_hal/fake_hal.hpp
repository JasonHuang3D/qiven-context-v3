#pragma once

// The HAL's fake, qiven3_hal_fake: the compute contract on the C++ standard
// library alone, so it builds and runs on every system. It reports the
// devices the test gives, keeps each buffer in host memory counted against
// its device's given memory, and runs each kernel's CPU reference.
//
// device(capability) gives the first device that matches ("cpu": back end
// "cpu"; "gpu": any other back end; else that back end), else the test's gap
// for the capability (for "gpu", the first for a back end not "cpu"), else
// no_device for "cpu" or "gpu" and not_built for a back end's name.
//
// A device, buffer or launch the fake did not give, or a null buffer, is
// std::invalid_argument: a caller's mistake, not a compute failure.

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "hal/hal.hpp"

namespace qiven3::hal::fake {

struct Config {
    unsigned cpu_threads = 1;
    std::set<Vector> vectors;
    std::vector<Device> devices;  // each id once
    // A capability's gap when no device matches it, by "cpu", "gpu" or a
    // back end's name: no_device, no_driver, not_built or not_pinned.
    std::map<std::string, std::string> gaps;
    Kernels kernels;
};

// What the fake did, each counted when it succeeds.
struct Counts {
    std::uint64_t allocations = 0, uploads = 0, downloads = 0, runs = 0;
};

class FakeCompute final : public Compute {
public:
    explicit FakeCompute(Config config);
    ~FakeCompute() override;
    FakeCompute(const FakeCompute&) = delete;
    FakeCompute& operator=(const FakeCompute&) = delete;

    Facts facts() override;
    std::variant<Device, Gap> device(std::string_view capability) override;
    std::unique_ptr<Buffer> allocate(const Device& d, std::uint64_t size) override;
    void upload(Buffer& b, std::span<const std::byte> bytes) override;
    void download(const Buffer& b, std::span<std::byte> bytes) override;
    void run(const Device& d, const Launch& launch) override;

    Counts counts() const;
    // The bytes the device's live buffers hold.
    std::uint64_t used(std::string_view device_id) const;

    struct State;

private:
    const Device& known(const Device& d, const char* operation) const;

    Config config_;
    std::shared_ptr<State> state_;  // shared with the buffers, which may outlive the fake
};

}  // namespace qiven3::hal::fake
