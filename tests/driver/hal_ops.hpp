#pragma once

// The drive's HAL half, in a build with QIVEN3_HAL on alone: the state its
// operations keep (driver/hal_ops.cpp), a template on the compute C, held
// by the session and instantiated for the HAL's fake.
//
// thread: one

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "hal/hal.hpp"
#include "support/fake_hal/fake_hal.hpp"

namespace qiven3::drive {

template <hal::Compute C>
struct HalState {
    using Buffer = typename C::Buffer;
    struct Held {
        Buffer buffer;
        std::size_t from;  // the index of the compute that gave it
    };

    // The drive's compute, and the other one, whose buffers a case hands
    // the drive's to show a caller's mistake.
    std::array<std::optional<C>, 2> computes;
    // Declared after the computes, so ended before them.
    std::map<std::int64_t, Held> buffers;
    std::int64_t next = 1;
    std::vector<std::byte> pages;  // the results' arena's, the members' budget at the first request
};

using Hal = HalState<hal::fake::FakeCompute>;

}  // namespace qiven3::drive
