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
# The modules of tools/modules.json, each "<name>;<regex>" of one of its
# paths: a path ending in / holds every file under it, any other is a
# pattern on the whole path, * any run of characters (tools/loop_modules.py).
file(READ "${PROJECT_SOURCE_DIR}/tools/modules.json" qiven3_table)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tools/modules.json")
set(qiven3_module_paths "")
string(JSON qiven3_count LENGTH "${qiven3_table}" modules)
math(EXPR qiven3_last "${qiven3_count} - 1")
foreach(qiven3_i RANGE ${qiven3_last})
  string(JSON qiven3_name GET "${qiven3_table}" modules ${qiven3_i} name)
  string(JSON qiven3_paths GET "${qiven3_table}" modules ${qiven3_i} paths)
  string(JSON qiven3_count LENGTH "${qiven3_paths}")
  math(EXPR qiven3_paths_last "${qiven3_count} - 1")
  foreach(qiven3_j RANGE ${qiven3_paths_last})
    string(JSON qiven3_path GET "${qiven3_paths}" ${qiven3_j})
    string(REPLACE "." "\\." qiven3_regex "${qiven3_path}")
    string(REPLACE "+" "\\+" qiven3_regex "${qiven3_regex}")
    string(REPLACE "*" ".*" qiven3_regex "${qiven3_regex}")
    if(NOT qiven3_path MATCHES "/$")
      string(APPEND qiven3_regex "$")
    endif()
    list(APPEND qiven3_module_paths "${qiven3_name}|^${qiven3_regex}")
  endforeach()
endforeach()

# The module of the file at path, from the root, into out: the first whose
# path holds it; a file in none stops the configure.
function(qiven3_module_of path out)
  foreach(entry IN LISTS qiven3_module_paths)
    string(FIND "${entry}" "|" bar)
    string(SUBSTRING "${entry}" 0 ${bar} name)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${entry}" ${start} -1 regex)
    if(path MATCHES "${regex}")
      set(${out} "${name}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "qiven3: ${path} is in no module of tools/modules.json")
endfunction()

# A case binary: its sources, linked with the log's main and the runner
# (support/log); each case a CTest test labelled cxx, LABELS and its
# module's name (its first source's module), under the watcher's limit by
# its labels, or LIMIT where the binary states its own
# (runner/ctest_tests.cmake).
function(qiven3_add_cases target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "LIMIT" "LABELS;SOURCES;LINK")
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "qiven3_add_cases(${target}): no SOURCES")
  endif()
  set(links qiven3_case_main ${arg_LINK})
  qiven3_executable(${target} SOURCES ${arg_SOURCES} LINK ${links})
  list(GET arg_SOURCES 0 first)
  cmake_path(ABSOLUTE_PATH first BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
  cmake_path(RELATIVE_PATH first BASE_DIRECTORY "${PROJECT_SOURCE_DIR}")
  qiven3_module_of("${first}" module)
  list(APPEND arg_LABELS ${module})
  list(JOIN arg_LABELS "," labels)
  set_property(GLOBAL APPEND PROPERTY QIVEN3_CASE_BINARIES ${target})
  set_property(GLOBAL PROPERTY QIVEN3_CASE_LABELS_${target} "${labels}")
  set_property(GLOBAL PROPERTY QIVEN3_CASE_LIMIT_${target} "${arg_LIMIT}")
endfunction()

add_subdirectory(runner)
add_subdirectory(support/log)
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

# The loop's own tests, those that start git, registered on one build a
# system (CMakePresets.json: ci-windows and linux-gcc), every Python test
# there behind the counting shim, so that the git they start is started in
# one step a run (tests/drive/test_git_shim.py); never on a tree of the
# structure records, a second tree of a job whose structure step would
# run the loop's structure test again.
option(QIVEN3_LOOP_TESTS "Register the loop's own tests, every Python test behind the counting shim" OFF)
set(qiven3_python_args "")
set(qiven3_python_files "")
if(Python3_EXECUTABLE)
  set(qiven3_python_args -D "PYTHON=${Python3_EXECUTABLE}")
  if(QIVEN3_LOOP_TESTS AND NOT QIVEN3_STRUCTURE)
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
  set_tests_properties(qiven3.not_built PROPERTIES LABELS \"cxx;py;fast;fake;support\")
  message(WARNING \"qiven3: the tests are not listed yet: build first\")
endif()
")
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${qiven3_include}")

# The structure records, the structure preset alone.
if(QIVEN3_STRUCTURE)
  add_subdirectory(structure)
endif()
