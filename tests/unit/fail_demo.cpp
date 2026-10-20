// fail_fast's one route, run as a process by tests/drive/test_fail.py; never
// a CTest test itself, since its case ends the process.

#include <iostream>

#include "memory/fail.hpp"
#include "runner/runner.hpp"

QIVEN3_CASE(fail, fail_fast) {
    std::cout << "before\n";
    std::cout.flush();
    qiven3::memory::fail_fast("fail.fail_fast: the case's own invariant");
}
