#pragma once

#include <optional>
#include <string_view>

namespace qiven3 {

inline constexpr std::string_view ENTRY_TAG = "qiven-entry/1";
inline constexpr std::string_view BUNDLE_FORMAT = "qiven-bundle";
inline constexpr int BUNDLE_MAJOR = 1;
inline constexpr int BUNDLE_MINOR = 0;
inline constexpr std::string_view BUNDLE_VERSION = "1.0";

struct Version {
    int major;
    int minor;
};

// A marker's version, <major>.<minor>, each 0 or 1 to 4 digits with no
// leading zero; anything else gives no value.
std::optional<Version> parse_version(std::string_view text);

}  // namespace qiven3
