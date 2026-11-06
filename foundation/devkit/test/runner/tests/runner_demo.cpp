// qiven_runner_demo: one case per route of the runner, for
// test_runner.py, beside this file, to run as processes. CTest never runs
// these cases themselves: four of them fail and one crashes by design.

#include <stdexcept>

#include "qiven/runner/runner.hpp"

QIVEN_CASE(demo, pass) {
    CHECK(true);
}

QIVEN_CASE(demo, fail) {
    CHECK(1 + 1 == 3);
}

QIVEN_CASE(demo, require) {
    REQUIRE(false);
    CHECK(false);  // not reached: a second failure would be counted
}

QIVEN_CASE(demo, skip) {
    QIVEN_SKIP("not applicable: the demo's skip");
}

QIVEN_CASE(demo, exception) {
    throw std::runtime_error("the demo's exception");
}

namespace {
// A null pointer read through a volatile local, which the optimizer keeps.
int* nowhere() noexcept {
    int* volatile none = nullptr;
    return none;
}
}  // namespace

QIVEN_CASE(demo, crash) {
    *nowhere() = 1;
}

// A block of a counted arena never given back: the count after the case
// fails it.
QIVEN_CASE(demo, held) {
    static_cast<void>(qiven_ctx.counted.arena(4096).allocate(16, 8));
}
