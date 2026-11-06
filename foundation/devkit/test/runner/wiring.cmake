# The foundation's tests' wiring, the one list of their directories, which
# the build's test tree includes at its own directory's scope, so that the
# mechanism's variables reach every test directory after it; each test
# directory's build directory is named under the includer's.
#
# At the include: the runner's mechanism (cases.cmake), before every
# module's tests, which call its qiven_add_cases; then the kit's and each
# foundation module's test directories, compute's under QIVEN_HAL alone.
# qiven_foundation_tests_end(), called once after every other test directory
# of the build (in the root's build, the product's too): the listing step,
# qiven_list_tests, and the structure records.
get_filename_component(qiven_foundation_dir "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

include("${CMAKE_CURRENT_LIST_DIR}/cases.cmake")

add_subdirectory("${qiven_foundation_dir}/devkit/test/runner" runner)
add_subdirectory("${qiven_foundation_dir}/devkit/test/case_main" case_main)
add_subdirectory("${qiven_foundation_dir}/devkit/test/faults" faults_kit)
add_subdirectory("${qiven_foundation_dir}/devkit/test/scan" scan)
add_subdirectory("${qiven_foundation_dir}/core/memory/tests" memory_tests)
add_subdirectory("${qiven_foundation_dir}/core/matcher/tests" matcher_tests)
add_subdirectory("${qiven_foundation_dir}/core/text/tests" text_tests)
add_subdirectory("${qiven_foundation_dir}/core/time/tests" time_tests)
add_subdirectory("${qiven_foundation_dir}/core/digest/tests" digest_tests)
add_subdirectory("${qiven_foundation_dir}/system/diag/tests" diag_tests)
add_subdirectory("${qiven_foundation_dir}/services/log/tests" log_tests)
add_subdirectory("${qiven_foundation_dir}/services/transport/tests" transport_tests)
add_subdirectory("${qiven_foundation_dir}/system/platform/tests" platform_tests)
add_subdirectory("${qiven_foundation_dir}/services/storage/sqlite/tests" storage_sqlite_tests)
add_subdirectory("${qiven_foundation_dir}/services/storage/recordlog/tests" recordlog_tests)
if(QIVEN_HAL)
  add_subdirectory("${qiven_foundation_dir}/services/compute/tests" compute_tests)
endif()

# The listing, after every test the build defines; it waits for the
# binaries the drives run: the runner's routes, memory's two demos, diag's
# fault program and the binary that imports its library. Then the structure
# records, the structure preset alone.
function(qiven_foundation_tests_end)
  qiven_list_tests(qiven_runner_demo qiven_memory_fail_demo qiven_memory_free_demo qiven_diag_fault
    qiven_diag_fault_import)
  if(QIVEN_STRUCTURE)
    add_subdirectory("${qiven_foundation_dir}/devkit/structure" structure)
  endif()
endfunction()
