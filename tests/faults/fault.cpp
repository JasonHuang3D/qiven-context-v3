// qiven3-fault <plant>: the planted faults of the diagnostic entry's cases,
// each in its own function plant_<plant>, which the report's stack names;
// tests/drive/test_faults.py runs each. On Windows qiven3-fault-crtdbg is
// the same file on the debug runtime, for the debug build's assertion.
//
//   null       a read through a null pointer
//   abort      std::abort()
//   assert     assert(false), in fault_assert.cpp, built without NDEBUG
//   parameter  Windows: std::fclose(nullptr), which the C runtime checks;
//              elsewhere an index past a vector's end, which the hardened
//              library checks
//   overflow   unbounded recursion, about 4 KiB a frame
//   exception  std::out_of_range("plant") out of main
//   failfast   qiven::memory::fail_fast("plant")
//   out        "out: plant" through diag::Out, twice; exit 0
//   hang       the main thread and one more, each blocked for ever
//   watch      a counter written three times, 1, 2 and 3; exit 0
//   span       a read of a span's element past its end, which the hardened
//              library stops before the read
//   span-in    the same read in bounds, span's control; exit 0
//
// Before its plant each run keeps a crash buffer of its own and pushes in
// it 20 lines, operations 1 to 20 "plant" ended "ok", each a JSON object,
// more than the report's 16 events, which so end with the last.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "qiven/diag/diag.hpp"
#include "qiven/diag/out.hpp"
#include "qiven/diag/ring.hpp"
#include "qiven/memory/fail.hpp"

#ifdef _MSC_VER
#define QIVEN3_PLANT __declspec(noinline)
#else
#define QIVEN3_PLANT __attribute__((noinline))
#endif

int plant_assert();

namespace {

constexpr std::size_t frame_bytes = 4000;
constexpr std::uint64_t operations = 20;
// A line's bound with its '\n': the longest, operation 20's, takes 57.
constexpr std::size_t line_bytes = 64;

// The ring's span: every line held, none evicted.
std::array<char, operations * line_bytes> storage{};

// The ring's lines, operations 1 to 20 ended: false when it refuses one.
bool fill(qiven::diag::Ring& ring) {
    for (std::uint64_t op = 1; op <= operations; ++op) {
        const std::string line =
            R"({"event":"op.end","name":"plant","op":)" + std::to_string(op) + R"(,"outcome":"ok"})";
        if (!ring.push(line)) {
            return false;
        }
    }
    return true;
}

// A ring kept for the report while it lives.
class Kept {
public:
    explicit Kept(const qiven::diag::Ring& ring) noexcept : ring_(ring) { qiven::diag::keep(ring_); }
    Kept(const Kept&) = delete;
    Kept& operator=(const Kept&) = delete;
    Kept(Kept&&) = delete;
    Kept& operator=(Kept&&) = delete;
    ~Kept() { qiven::diag::drop(ring_); }

private:
    const qiven::diag::Ring& ring_;
};

}  // namespace

QIVEN3_PLANT int plant_null() {
    int* volatile none = nullptr;
    return *none;
}

QIVEN3_PLANT int plant_abort() { std::abort(); }

QIVEN3_PLANT int plant_parameter() {
#ifdef _WIN32
    std::FILE* volatile none = nullptr;
    // Not a tail call, so that this frame stays in the stack.
    const int closed = std::fclose(none);
    return closed + 1;
#else
    std::vector<int> one(1);
    volatile std::size_t past = 1;
    const int read = one[past];
    return read + 1;
#endif
}

// Index 1 of a span over the first of two elements, or 0, the control.
// Standard error is unbuffered: the line before the read is written, and
// the line after it, naming the element read, only if the read happened.
QIVEN3_PLANT int plant_span(std::size_t index) {
    const std::array<int, 2> both{1, 2};
    const std::span<const int> first(both.data(), 1);
    const volatile std::size_t at = index;
    std::fputs("span: reading\n", stderr);
    const int read = first[at];
    std::fputs(read == both[1] ? "span: read past the end\n" : "span: read in bounds\n", stderr);
    return 0;
}

QIVEN3_PLANT int plant_overflow(int depth) {
    volatile char frame[frame_bytes];
    frame[static_cast<std::size_t>(depth) % frame_bytes] = static_cast<char>(depth);
    volatile bool again = true;
    if (again) {
        return plant_overflow(depth + 1) + frame[0];
    }
    return frame[1];
}

QIVEN3_PLANT int plant_exception() { throw std::out_of_range("plant"); }

QIVEN3_PLANT int plant_failfast() { qiven::memory::fail_fast("plant"); }

// Blocks the calling thread for ever.
[[noreturn]] QIVEN3_PLANT void wait_for_ever() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
}

// Both threads' stacks hold plant_hang: the main thread's frame, and the
// other's lambda, which plant_hang's name qualifies.
[[noreturn]] QIVEN3_PLANT int plant_hang() {
    std::thread other([] { wait_for_ever(); });
    wait_for_ever();
}

// Three writes the debugger's watch stops at, the counter a local, built
// unoptimized so that the debugger finds it at each of its lines.
#if defined(_MSC_VER) && !defined(__clang__)
#pragma optimize("", off)
#define QIVEN3_UNOPTIMIZED
#elif defined(__clang__)
#define QIVEN3_UNOPTIMIZED [[clang::optnone]]
#else
#define QIVEN3_UNOPTIMIZED [[gnu::optimize("O0")]]
#endif
QIVEN3_UNOPTIMIZED QIVEN3_PLANT int plant_watch() {
    volatile std::uint64_t counter;
    counter = 1;  // the break's line
    counter = 2;
    counter = 3;
    return counter == 3 ? 0 : 1;
}
#if defined(_MSC_VER) && !defined(__clang__)
#pragma optimize("", on)
#endif

QIVEN3_PLANT int plant_out() {
    qiven::diag::Out out = qiven::diag::Out::from_environment();
    const bool wrote = out.write("out: plant\n") && out.write("out: plant\n");
    return wrote && !out.failed() ? 0 : 1;
}

int main(int argc, char** argv) {
    const std::string_view plant = argc == 2 ? argv[1] : "";
    qiven::diag::Ring ring(storage);
    const Kept kept(ring);
    if (!fill(ring)) {
        std::fputs("qiven3-fault: the ring refused a line\n", stderr);
        return 1;
    }
    if (plant == "null") {
        return plant_null();
    }
    if (plant == "abort") {
        return plant_abort();
    }
    if (plant == "assert") {
        return plant_assert();
    }
    if (plant == "parameter") {
        return plant_parameter();
    }
    if (plant == "overflow") {
        return plant_overflow(0);
    }
    if (plant == "exception") {
        return plant_exception();
    }
    if (plant == "failfast") {
        return plant_failfast();
    }
    if (plant == "out") {
        return plant_out();
    }
    if (plant == "hang") {
        return plant_hang();
    }
    if (plant == "watch") {
        return plant_watch();
    }
    if (plant == "span" || plant == "span-in") {
        return plant_span(plant == "span" ? 1 : 0);
    }
    std::fputs(
        "usage: qiven3-fault null|abort|assert|parameter|overflow|exception|failfast|out|hang|watch|span|span-in\n",
        stderr);
    return 2;
}
