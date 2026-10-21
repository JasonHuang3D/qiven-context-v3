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
//   failfast   memory::fail_fast("plant")
//   out        "out: plant" through diag::Out, twice; exit 0

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "diag/out.hpp"
#include "memory/fail.hpp"

#ifdef _MSC_VER
#define QIVEN3_PLANT __declspec(noinline)
#else
#define QIVEN3_PLANT __attribute__((noinline))
#endif

int plant_assert();

namespace {

constexpr std::size_t frame_bytes = 4000;

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

QIVEN3_PLANT int plant_failfast() { qiven3::memory::fail_fast("plant"); }

QIVEN3_PLANT int plant_out() {
    qiven3::diag::Out out = qiven3::diag::Out::from_environment();
    const bool wrote = out.write("out: plant\n") && out.write("out: plant\n");
    return wrote && !out.failed() ? 0 : 1;
}

int main(int argc, char** argv) {
    const std::string_view plant = argc == 2 ? argv[1] : "";
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
    std::fputs("usage: qiven3-fault null|abort|assert|parameter|overflow|exception|failfast|out\n", stderr);
    return 2;
}
