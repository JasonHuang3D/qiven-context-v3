# The tests: case binaries on qiven_runner, and the Python tests, each
# case and each Python test its own CTest test with its labels.
#
# The mechanism is the runner's (foundation/devkit/test/runner/cases.cmake),
# included before every module's tests, which call its qiven_add_cases; its
# listing step, qiven_list_tests, runs after them. This file is the wiring:
# the kit's and each module's test directories, the header units, the
# binaries the drives run, which the listing waits for, and the structure
# records.
include("${PROJECT_SOURCE_DIR}/foundation/devkit/test/runner/cases.cmake")

add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/devkit/test/runner" runner)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/devkit/test/case_main" case_main)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/devkit/test/faults" faults_kit)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/devkit/test/scan" scan)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/core/memory/tests" memory_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/core/matcher/tests" matcher_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/core/text/tests" text_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/core/time/tests" time_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/core/digest/tests" digest_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/system/diag/tests" diag_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/services/log/tests" log_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/services/transport/tests" transport_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/system/platform/tests" platform_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/services/storage/sqlite/tests" storage_sqlite_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/services/storage/recordlog/tests" recordlog_tests)
add_subdirectory("${PROJECT_SOURCE_DIR}/products/qiven3/format/tests" format_tests)
add_subdirectory(support/fixtures)
add_subdirectory(support/checked)
if(QIVEN_HAL)
  add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/services/compute/tests" compute_tests)
endif()
# The header units' rule on every configure with an interpreter, not the
# analysis's alone: a header the analysis reads with no
# unit fails it (foundation/devkit/build/header_units.cmake). The analysis compiles
# the units it gives.
if(Python3_EXECUTABLE OR QIVEN_ANALYZE STREQUAL "analysis")
  qiven3_header_units("${PROJECT_SOURCE_DIR}" qiven3_header_units)
endif()
if(QIVEN_ANALYZE STREQUAL "analysis")
  add_subdirectory(support/headers)
endif()
add_subdirectory(driver)
add_subdirectory(child)
add_subdirectory(unit)
add_subdirectory(fuzz)

# The listing, after every module's tests; it waits for the binaries the
# drives run: the runner's routes, memory's two demos, diag's fault program
# and the binary that imports its library.
qiven_list_tests(qiven_runner_demo qiven_memory_fail_demo qiven_memory_free_demo qiven_diag_fault
  qiven_diag_fault_import)

# The structure records, the structure preset alone.
if(QIVEN3_STRUCTURE)
  add_subdirectory("${PROJECT_SOURCE_DIR}/foundation/devkit/structure" structure)
endif()
