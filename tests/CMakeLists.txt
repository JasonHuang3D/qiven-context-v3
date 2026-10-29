# The tests: case binaries on qiven3_runner, and the Python tests, each
# case and each Python test its own CTest test with its labels.
#
# A step after the build lists each case binary's cases (--list) and each
# Python test's id, and writes one add_test() each into a file that the
# CTest file of this directory includes. Every test carries cxx or py, the
# labels its binary or package gives, and the suite's skip code; a timed
# one runs alone (RUN_SERIAL).

set(QIVEN3_SKIP_CODE 125)
set(qiven3_bin "${PROJECT_BINARY_DIR}/bin")

# qiven3_add_cases(<target> LABELS <label>... SOURCES <source>... [LINK <target>...]
#                  [LIMIT <seconds>])
# A case binary: its sources, linked with the runner; each case a CTest test
# labelled cxx and LABELS, under the watcher's limit by its labels, or LIMIT
# where the binary states its own (runner/ctest_tests.cmake). The runner
# comes first, or where LINK names it: after a target that links it
# (support/stores), so that it is on the link line once.
function(qiven3_add_cases target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "LIMIT" "LABELS;SOURCES;LINK")
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "qiven3_add_cases(${target}): no SOURCES")
  endif()
  set(links qiven3_runner ${arg_LINK})
  if(qiven3_runner IN_LIST arg_LINK)
    set(links ${arg_LINK})
  endif()
  qiven3_executable(${target} SOURCES ${arg_SOURCES} LINK ${links})
  list(JOIN arg_LABELS "," labels)
  set_property(GLOBAL APPEND PROPERTY QIVEN3_CASE_BINARIES ${target})
  set_property(GLOBAL PROPERTY QIVEN3_CASE_LABELS_${target} "${labels}")
  set_property(GLOBAL PROPERTY QIVEN3_CASE_LIMIT_${target} "${arg_LIMIT}")
endfunction()

add_subdirectory(runner)
add_subdirectory(support/faults)
add_subdirectory(support/fake_platform)
add_subdirectory(support/fixtures)
add_subdirectory(support/stores)
add_subdirectory(support/seams)
add_subdirectory(support/scan)
if(QIVEN3_HAL)
  add_subdirectory(support/fake_hal)
endif()
if(QIVEN3_ANALYZE STREQUAL "analysis")
  add_subdirectory(support/headers)
endif()
add_subdirectory(driver)
add_subdirectory(child)
add_subdirectory(unit)
add_subdirectory(faults)
add_subdirectory(fuzz)

# The AddressSanitizer runtime beside each test binary: every binary is
# written to one directory. The listing step runs the case binaries, so it
# waits for the copy (below): a binary started while the DLL is being
# written fails to load it.
set(qiven3_runtime_files "")
if(MSVC AND QIVEN3_SANITIZE MATCHES "address")
  get_filename_component(qiven3_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(qiven3_asan_dll clang_rt.asan_dbg_dynamic-x86_64.dll)
  else()
    set(qiven3_asan_dll clang_rt.asan_dynamic-x86_64.dll)
  endif()
  if(NOT EXISTS "${qiven3_cl_dir}/${qiven3_asan_dll}")
    message(FATAL_ERROR "qiven3: ${qiven3_asan_dll} is not beside the compiler")
  endif()
  add_custom_command(OUTPUT "${qiven3_bin}/${qiven3_asan_dll}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${qiven3_cl_dir}/${qiven3_asan_dll}" "${qiven3_bin}/"
    DEPENDS "${qiven3_cl_dir}/${qiven3_asan_dll}"
    VERBATIM)
  add_custom_target(qiven3_asan_runtime ALL DEPENDS "${qiven3_bin}/${qiven3_asan_dll}")
  list(APPEND qiven3_runtime_files "${qiven3_bin}/${qiven3_asan_dll}")
endif()

# The listing step's inputs: each case binary's labels, its own limit or
# none, and its file.
get_property(qiven3_case_binaries GLOBAL PROPERTY QIVEN3_CASE_BINARIES)
set(qiven3_inputs "")
foreach(target IN LISTS qiven3_case_binaries)
  get_property(labels GLOBAL PROPERTY QIVEN3_CASE_LABELS_${target})
  get_property(limit GLOBAL PROPERTY QIVEN3_CASE_LIMIT_${target})
  string(APPEND qiven3_inputs "${labels}\t${limit}\t$<TARGET_FILE:${target}>\n")
endforeach()
set(qiven3_inputs_file "${CMAKE_CURRENT_BINARY_DIR}/case_binaries.txt")
file(GENERATE OUTPUT "${qiven3_inputs_file}" CONTENT "${qiven3_inputs}")

# The loop's own tests, the counting shim's run of them, registered on one
# build a system (CMakePresets.json: ci-windows and linux-gcc), so that the
# git they start is started in one step a run (tests/drive/test_git_shim.py).
option(QIVEN3_LOOP_TESTS "Register the counting shim's run of the loop's own tests" OFF)
set(qiven3_python_args "")
set(qiven3_python_files "")
if(Python3_EXECUTABLE)
  set(qiven3_python_args -D "PYTHON=${Python3_EXECUTABLE}")
  if(QIVEN3_LOOP_TESTS)
    list(APPEND qiven3_python_args -D "LOOP=ON")
  endif()
  file(GLOB_RECURSE qiven3_python_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
else()
  message(STATUS "qiven3: no interpreter named; the Python tests are not registered")
endif()

# Under libFuzzer, the fuzz targets' lines (fuzz/CMakeLists.txt), each a
# CTest test.
set(qiven3_fuzz_args "")
get_property(qiven3_fuzz_binaries GLOBAL PROPERTY QIVEN3_FUZZ_BINARIES)
if(qiven3_fuzz_binaries)
  set(qiven3_fuzz_args -D "FUZZ=${CMAKE_CURRENT_BINARY_DIR}/fuzz_targets.txt")
endif()

set(qiven3_tests_file "${CMAKE_CURRENT_BINARY_DIR}/ctest_tests.cmake")
add_custom_command(OUTPUT "${qiven3_tests_file}"
  COMMAND "${CMAKE_COMMAND}" -D "INPUTS=${qiven3_inputs_file}" -D "OUT=${qiven3_tests_file}"
    ${qiven3_python_args} ${qiven3_fuzz_args} -D "SOURCE=${PROJECT_SOURCE_DIR}" -D "BIN=${qiven3_bin}"
    -D "BUILD=${PROJECT_BINARY_DIR}" -D "CTEST=${CMAKE_CTEST_COMMAND}" -D "SKIP=${QIVEN3_SKIP_CODE}"
    -D "ADDR2LINE=${CMAKE_ADDR2LINE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/runner/ctest_tests.cmake"
  DEPENDS ${qiven3_case_binaries} ${qiven3_fuzz_binaries} qiven3_runner_demo qiven3_fail_demo qiven3_free_demo
    qiven3-fault qiven3-fault-import
    ${qiven3_runtime_files} "${qiven3_inputs_file}"
    "${CMAKE_CURRENT_SOURCE_DIR}/runner/ctest_tests.cmake" ${qiven3_python_files}
  COMMENT "Listing the C++ cases and the Python tests for CTest"
  VERBATIM)
add_custom_target(qiven3_ctest_tests ALL DEPENDS "${qiven3_tests_file}")

# Before the build has listed the tests, CTest finds one failing test that
# says so, in every preset's selection but fuzz, bench and drill.
set(qiven3_include "${CMAKE_CURRENT_BINARY_DIR}/ctest_include.cmake")
file(WRITE "${qiven3_include}" "if(EXISTS [==[${qiven3_tests_file}]==])
  include([==[${qiven3_tests_file}]==])
else()
  add_test(qiven3.not_built [==[${CMAKE_COMMAND}]==] -E false)
  set_tests_properties(qiven3.not_built PROPERTIES LABELS \"cxx;py;fast;fake\")
  message(WARNING \"qiven3: the tests are not listed yet: build first\")
endif()
")
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${qiven3_include}")

# The structure records, the structure preset alone.
if(QIVEN3_STRUCTURE)
  add_subdirectory(structure)
endif()
