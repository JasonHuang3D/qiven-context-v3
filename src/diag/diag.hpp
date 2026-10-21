#pragma once

// diag: the diagnostic entry, which every binary of the build runs before
// main (entry.cpp, by qiven3_executable). A fault ends the process with one
// report on standard error, never with a window:
//
//   fatal: <cause>
//   fatal: stack <n>
//     #<i> <module>!<function>+0x<offset> <file>:<line>
//   fatal: events <n>
//   fatal: dump <file name>            (or: fatal: dump none)
//   fatal: end <status>
//
// at most 32 frames, each line at most 300 code points. The install and
// the report are by system, diag_windows.cpp and diag_linux.cpp; this
// file's Line and terminate handler are both systems'. Not core logic: it
// includes the system's headers, and links qiven3_memory alone of v3's.
//
// thread: install() once, from the first initializer; thread_start() first
// in each thread v3 starts; a report once, from any thread.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace qiven3::diag {

// Sets the error mode, the C runtime's reports and every handler. Under a
// sanitizer the fault handlers stay uninstalled, its own report standing;
// the terminate handler, which reports fail_fast's cause, stays.
void install() noexcept;

// A thread's own share: on Windows its stack guarantee, so that a stack
// overflow's handler has a stack.
void thread_start() noexcept;

namespace detail {

// A report line's bound, in code points; a line holds bytes, so a line of
// at most this many bytes holds at most this many code points.
inline constexpr std::size_t line_max = 300;

// One line of a report, built in place: nothing allocated. Text past the
// bound is cut at a code point's start.
class Line {
public:
    Line& text(std::string_view part) noexcept;
    // A wide string's ASCII, each other code unit '?'.
    Line& wide(std::wstring_view part) noexcept;
    Line& number(std::uint64_t value) noexcept;
    // 0x and the digits, at least `digits` of them, upper case.
    Line& hex(std::uint64_t value, std::size_t digits = 1) noexcept;
    // The text so far.
    [[nodiscard]] std::string_view view() const noexcept { return {bytes_.data(), size_}; }
    // The line with its newline.
    [[nodiscard]] std::string_view line() noexcept;

private:
    void put(char c) noexcept;
    std::array<char, line_max + 1> bytes_{};
    std::size_t size_ = 0;
    bool cut_ = false;
};

// How a report ends the process: the C runtime's abort status, 3 on Windows
// and SIGABRT elsewhere, or the fail-fast's, 0xC0000409 on Windows.
enum class End : std::uint8_t { abort, fail_fast };

// The system's half: writes the report, the cause first, and ends the
// process. A second report, from any thread, ends it at once with the
// first's status.
[[noreturn]] void fatal(std::string_view cause, End end) noexcept;

// The terminate handler: fail_fast's kept cause, else the unhandled
// exception's what(), else std::terminate's own.
[[noreturn]] void on_terminate();

}  // namespace detail

}  // namespace qiven3::diag
