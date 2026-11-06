# The tests: case binaries on qiven_runner, and the Python tests, each
# case and each Python test its own CTest test with its labels.
#
# The foundation's tests' wiring is the runner's
# (foundation/devkit/test/runner/wiring.cmake): its include gives the
# mechanism (cases.cmake), before every module's tests, which call its
# qiven_add_cases, and the kit's and each foundation module's test
# directories; its qiven_foundation_tests_end(), after the product's below,
# the listing step and the structure records. This file is the product's
# wiring: its test directories, its header units and the binaries the drives
# run.
include("${PROJECT_SOURCE_DIR}/foundation/devkit/test/runner/wiring.cmake")

add_subdirectory("${PROJECT_SOURCE_DIR}/products/qiven3/format/tests" format_tests)
add_subdirectory(support/fixtures)
add_subdirectory(support/checked)
# The product's header units, the analysis's alone, in their pair
# (support/headers/); the units' rule, which gives them, and the
# foundation's pair are foundation/devkit/build/top.cmake's.
if(QIVEN_ANALYZE STREQUAL "analysis")
  add_subdirectory(support/headers)
endif()
add_subdirectory(driver)
add_subdirectory(child)
add_subdirectory(unit)
add_subdirectory(fuzz)

qiven_foundation_tests_end()
