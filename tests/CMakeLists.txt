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

# qiven3_add_cases(<target> LABELS <label>... SOURCES <source>... [LINK <target>...])
# A case binary: its sources, linked with the runner; each case a CTest test
# labelled cxx and LABELS.
function(qiven3_add_cases target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "LABELS;SOURCES;LINK")
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "qiven3_add_cases(${target}): no SOURCES")
  endif()
  add_executable(${target} ${arg_SOURCES})
  target_link_libraries(${target} PRIVATE qiven3_runner qiven3_flags ${arg_LINK})
  list(JOIN arg_LABELS "," labels)
  set_property(GLOBAL APPEND PROPERTY QIVEN3_CASE_BINARIES ${target})
  set_property(GLOBAL PROPERTY QIVEN3_CASE_LABELS_${target} "${labels}")
endfunction()

add_subdirectory(runner)
add_subdirectory(support/fake_platform)

# The AddressSanitizer runtime beside each test binary: every binary is
# written to one directory.
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
endif()

# The listing step's inputs: each case binary's labels and file.
get_property(qiven3_case_binaries GLOBAL PROPERTY QIVEN3_CASE_BINARIES)
set(qiven3_inputs "")
foreach(target IN LISTS qiven3_case_binaries)
  get_property(labels GLOBAL PROPERTY QIVEN3_CASE_LABELS_${target})
  string(APPEND qiven3_inputs "${labels}\t$<TARGET_FILE:${target}>\n")
endforeach()
set(qiven3_inputs_file "${CMAKE_CURRENT_BINARY_DIR}/case_binaries.txt")
file(GENERATE OUTPUT "${qiven3_inputs_file}" CONTENT "${qiven3_inputs}")

set(qiven3_python_args "")
set(qiven3_python_files "")
if(Python3_EXECUTABLE)
  set(qiven3_python_args -D "PYTHON=${Python3_EXECUTABLE}")
  file(GLOB_RECURSE qiven3_python_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.py")
else()
  message(STATUS "qiven3: no interpreter named; the Python tests are not registered")
endif()

set(qiven3_tests_file "${CMAKE_CURRENT_BINARY_DIR}/ctest_tests.cmake")
add_custom_command(OUTPUT "${qiven3_tests_file}"
  COMMAND "${CMAKE_COMMAND}" -D "INPUTS=${qiven3_inputs_file}" -D "OUT=${qiven3_tests_file}"
    ${qiven3_python_args} -D "SOURCE=${PROJECT_SOURCE_DIR}" -D "BIN=${qiven3_bin}"
    -D "BUILD=${PROJECT_BINARY_DIR}" -D "CTEST=${CMAKE_CTEST_COMMAND}" -D "SKIP=${QIVEN3_SKIP_CODE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/runner/ctest_tests.cmake"
  DEPENDS ${qiven3_case_binaries} qiven3_runner_demo "${qiven3_inputs_file}"
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
