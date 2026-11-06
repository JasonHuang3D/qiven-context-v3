# The foundation's tests' wiring, which the build's test tree includes at its
# own directory's scope, so that the mechanism's variables reach every test
# directory after it; each test directory's build directory is named under
# the includer's. It reads qiven_root (build.cmake) and the one list of the
# foundation's modules (modules.cmake, in the build's directory).
#
# At the include: the runner's mechanism (cases.cmake), before every
# module's tests, which call its qiven_add_cases; then the kit's own test
# directories and those of the list's modules that have one, each in
# <module's last name>_tests. qiven_foundation_tests_end(), called once after
# every other test directory of the build (in the root's build, the
# product's too): the listing step, qiven_list_tests, and the structure records.
include("${CMAKE_CURRENT_LIST_DIR}/cases.cmake")

add_subdirectory("${qiven_root}/foundation/devkit/test/runner" runner)
add_subdirectory("${qiven_root}/foundation/devkit/test/case_main" case_main)
add_subdirectory("${qiven_root}/foundation/devkit/test/faults" faults_kit)
add_subdirectory("${qiven_root}/foundation/devkit/test/scan" scan)
foreach(qiven_module IN LISTS qiven_foundation_modules)
  if(EXISTS "${qiven_root}/foundation/${qiven_module}/tests/CMakeLists.txt")
    get_filename_component(qiven_module_name "${qiven_module}" NAME)
    add_subdirectory("${qiven_root}/foundation/${qiven_module}/tests" "${qiven_module_name}_tests")
  endif()
endforeach()

# The listing, after every test the build defines; it waits for the
# binaries the drives run: the runner's routes, memory's two demos, diag's
# fault program and the binary that imports its library. Then the structure
# records, the structure preset alone.
function(qiven_foundation_tests_end)
  qiven_list_tests(qiven_runner_demo qiven_memory_fail_demo qiven_memory_free_demo qiven_diag_fault
    qiven_diag_fault_import)
  if(QIVEN_STRUCTURE)
    add_subdirectory("${qiven_root}/foundation/devkit/structure" structure)
  endif()
endfunction()
