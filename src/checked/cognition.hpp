#pragma once

// checked/cognition: cognition's record log values, which every product
// site that opens, creates, exports or imports a store passes: the limits,
// the format's thresholds by size and by count (the target, the warning
// and the maximum); and the file identity, application_id 0x71763301. A
// header of values alone, so that a module opening a plain store
// (location) reaches them without the rules.

#include "qiven/recordlog/values.hpp"

namespace qiven3::checked {

inline constexpr qiven::recordlog::Limits cognition_limits{
    512 * qiven::recordlog::mib, 640 * qiven::recordlog::mib, 768 * qiven::recordlog::mib, 400'000, 480'000, 550'000};
inline constexpr qiven::recordlog::Identity cognition_identity{0x71763301};

}  // namespace qiven3::checked
