#pragma once

// catalogue: every event of v3, as a descriptor: its level, its name, its
// fields' keys and their types. An event takes a descriptor, never a name,
// and its fields' types exactly. A package adds its events here; a test's
// own descriptors stay in the test tree.
//
// thread: none

#include <array>
#include <string_view>

#include "log/log.hpp"

namespace qiven3::log::events {

struct LogStart {
    static constexpr Level level = Level::info;
    static constexpr std::string_view name = "log.start";
    // binary: the binary's name; min: the build's minimum; ring: its bytes.
    static constexpr std::array<std::string_view, 3> keys{"binary", "min", "ring"};
    using fields = Types<Name, Name, Count>;
};
inline constexpr LogStart log_start{};

struct LogDropped {
    static constexpr Level level = Level::warn;
    static constexpr std::string_view name = "log.dropped";
    // count: the lines evicted from the ring before a drain took them.
    static constexpr std::array<std::string_view, 1> keys{"count"};
    using fields = Types<Count>;
};
inline constexpr LogDropped log_dropped{};

struct OpBegin {
    static constexpr Level level = Level::debug;
    static constexpr std::string_view name = "op.begin";
    static constexpr std::array<std::string_view, 1> keys{"name"};
    using fields = Types<Name>;
};
inline constexpr OpBegin op_begin{};

struct OpEnd {
    static constexpr Level level = Level::info;
    static constexpr std::string_view name = "op.end";
    // outcome: ok, an error's code, or dropped; peak: its arena's peak bytes.
    static constexpr std::array<std::string_view, 4> keys{"name", "outcome", "ms", "peak"};
    using fields = Types<Name, Name, Millis, Count>;
};
inline constexpr OpEnd op_end{};

struct ErrorTyped {
    static constexpr Level level = Level::error;
    static constexpr std::string_view name = "error.typed";
    static constexpr std::array<std::string_view, 3> keys{"type", "code", "operation"};
    using fields = Types<Name, Name, Name>;
};
inline constexpr ErrorTyped error_typed{};

struct DriveRequest {
    static constexpr Level level = Level::debug;
    static constexpr std::string_view name = "drive.request";
    // name: from the drive's table of operations, else unknown.
    static constexpr std::array<std::string_view, 2> keys{"name", "bytes"};
    using fields = Types<Name, Count>;
};
inline constexpr DriveRequest drive_request{};

struct DriveReply {
    static constexpr Level level = Level::debug;
    static constexpr std::string_view name = "drive.reply";
    static constexpr std::array<std::string_view, 2> keys{"status", "bytes"};
    using fields = Types<Name, Count>;
};
inline constexpr DriveReply drive_reply{};

struct CaseEnd {
    static constexpr Level level = Level::info;
    static constexpr std::string_view name = "case.end";
    // held: the bytes v3's allocators hold at the case's end.
    static constexpr std::array<std::string_view, 4> keys{"case", "outcome", "ms", "held"};
    using fields = Types<Name, Name, Millis, Count>;
};
inline constexpr CaseEnd case_end{};

struct FaultInjected {
    static constexpr Level level = Level::debug;
    static constexpr std::string_view name = "fault.injected";
    static constexpr std::array<std::string_view, 2> keys{"k", "site"};
    using fields = Types<Count, Name>;
};
inline constexpr FaultInjected fault_injected{};

}  // namespace qiven3::log::events
