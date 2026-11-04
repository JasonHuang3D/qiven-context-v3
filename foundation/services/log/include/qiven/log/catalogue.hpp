#pragma once

// catalogue: log's own events, which its writer emits, and error.typed, a
// typed error any main writes, each as a descriptor: its level, its name,
// its fields' keys and their types. An event takes a descriptor, never a
// name, and its fields' types exactly. A consumer's events are its own,
// each descriptor beside its writer.
//
// thread: none

#include <array>
#include <string_view>

#include "qiven/log/log.hpp"

namespace qiven::log::events {

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

}  // namespace qiven::log::events
