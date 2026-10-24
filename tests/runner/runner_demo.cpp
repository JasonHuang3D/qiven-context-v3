// qiven3_runner_demo: one case per route of the runner, for
// tests/drive/test_runner.py to run as processes. CTest never runs these
// cases themselves: four of them fail and one crashes by design.

#include <stdexcept>

#include "runner/runner.hpp"

QIVEN3_CASE(demo, pass) {
    CHECK(true);
}

QIVEN3_CASE(demo, fail) {
    CHECK(1 + 1 == 3);
}

QIVEN3_CASE(demo, require) {
    REQUIRE(false);
    CHECK(false);  // not reached: a second failure would be counted
}

QIVEN3_CASE(demo, skip) {
    QIVEN3_SKIP("not applicable: the demo's skip");
}

QIVEN3_CASE(demo, exception) {
    throw std::runtime_error("the demo's exception");
}

namespace {
int* volatile nowhere = nullptr;
}

QIVEN3_CASE(demo, crash) {
    *nowhere = 1;
}

QIVEN3_SERIAL_CASE(demo, serial) {
    CHECK(true);
}

// A block of a counted arena never given back: the count after the case
// fails it.
QIVEN3_CASE(demo, held) {
    static_cast<void>(qiven3_ctx.counted.arena(4096).allocate(16, 8));
}
