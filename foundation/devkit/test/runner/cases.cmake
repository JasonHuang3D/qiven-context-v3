# The test tree's mechanism, which the runner's wiring (wiring.cmake, beside
# it) includes before every module's tests: case binaries on qiven_runner,
# and the Python tests, each case and each Python test its own CTest test
# with its labels.
#
# A step after the build (qiven_list_tests, below, which the wiring calls
# after every module's tests) lists each case binary's cases (--list) and
# each Python test's id, and writes one add_test() each into a file that the
# CTest file of that directory includes. Every test carries cxx or py, the
# labels its binary or package gives, and the suite's skip code; a timed
# one runs alone (RUN_SERIAL). The binaries it waits for beside the case
# binaries and the fuzz targets are its caller's to name.

set(QIVEN_SKIP_CODE 125)

# qiven_add_cases(<target> LABELS <label>... SOURCES <source>... [LINK <target>...]
#                 [LIMIT <seconds>])
# The modules of the table, each "<name>|<regex>" of one of its paths: the
# present rows of each built module.json, the table's files the root's,
# tools/ one and each under the layers' trees of tools/trees.json, in sorted
# path order (foundation/devkit/python/table.py). A path ending in / holds every file
# under it, any other is a pattern on the whole path, matched name by name as
# table.py's holds: * any run of characters and ? any one, never a /.
qiven_table_globs("${PROJECT_SOURCE_DIR}" qiven_table_globs)
file(GLOB_RECURSE qiven_table_files LIST_DIRECTORIES false CONFIGURE_DEPENDS
     RELATIVE "${PROJECT_SOURCE_DIR}" ${qiven_table_globs})
list(APPEND qiven_table_files module.json tools/module.json)
list(SORT qiven_table_files)
set(qiven_module_paths "")
foreach(qiven_file IN LISTS qiven_table_files)
  file(READ "${PROJECT_SOURCE_DIR}/${qiven_file}" qiven_table)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${qiven_file}")
  string(JSON qiven_status GET "${qiven_table}" status)
  if(NOT qiven_status STREQUAL "built")
    continue()
  endif()
  string(JSON qiven_count LENGTH "${qiven_table}" present)
  if(qiven_count EQUAL 0)  # its rows another module's (from)
    continue()
  endif()
  math(EXPR qiven_last "${qiven_count} - 1")
  foreach(qiven_i RANGE ${qiven_last})
    string(JSON qiven_name GET "${qiven_table}" present ${qiven_i} name)
    string(JSON qiven_paths GET "${qiven_table}" present ${qiven_i} paths)
    string(JSON qiven_count LENGTH "${qiven_paths}")
    math(EXPR qiven_paths_last "${qiven_count} - 1")
    foreach(qiven_j RANGE ${qiven_paths_last})
      string(JSON qiven_path GET "${qiven_paths}" ${qiven_j})
      if(qiven_path MATCHES "/$")
        string(REGEX REPLACE "([][.^$|()*+?{}\\\\])" "\\\\\\1" qiven_regex "${qiven_path}")
      else()
        string(REGEX REPLACE "([][.^$|()+{}\\\\])" "\\\\\\1" qiven_regex "${qiven_path}")
        string(REPLACE "*" "[^/]*" qiven_regex "${qiven_regex}")
        string(REPLACE "?" "[^/]" qiven_regex "${qiven_regex}")
        string(APPEND qiven_regex "$")
      endif()
      list(APPEND qiven_module_paths "${qiven_name}|^${qiven_regex}")
    endforeach()
  endforeach()
endforeach()

# The module of the file at path, from the root, into out: the first whose
# path holds it; a file in none stops the configure.
function(qiven_module_of path out)
  foreach(entry IN LISTS qiven_module_paths)
    string(FIND "${entry}" "|" bar)
    string(SUBSTRING "${entry}" 0 ${bar} name)
    math(EXPR start "${bar} + 1")
    string(SUBSTRING "${entry}" ${start} -1 regex)
    if(path MATCHES "${regex}")
      set(${out} "${name}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  message(FATAL_ERROR "qiven: ${path} is in no module of the table's present rows (the module.json files)")
endfunction()

# A case binary: its sources, linked with the log's main and the runner
# (qiven_case_main); each case a CTest test labelled cxx, LABELS and its
# module's name (its first source's module), under the watcher's limit by
# its labels, or LIMIT where the binary states its own
# (the runner's ctest_tests.cmake).
function(qiven_add_cases target)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "LIMIT" "LABELS;SOURCES;LINK")
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "qiven_add_cases(${target}): no SOURCES")
  endif()
  set(links qiven_case_main ${arg_LINK})
  qiven_executable(${target} SOURCES ${arg_SOURCES} LINK ${links})
  list(GET arg_SOURCES 0 first)
  cmake_path(ABSOLUTE_PATH first BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
  cmake_path(RELATIVE_PATH first BASE_DIRECTORY "${PROJECT_SOURCE_DIR}")
  qiven_module_of("${first}" module)
  list(APPEND arg_LABELS ${module})
  list(JOIN arg_LABELS "," labels)
  set_property(GLOBAL APPEND PROPERTY QIVEN_CASE_BINARIES ${target})
  set_property(GLOBAL PROPERTY QIVEN_CASE_LABELS_${target} "${labels}")
  set_property(GLOBAL PROPERTY QIVEN_CASE_LIMIT_${target} "${arg_LIMIT}")
endfunction()

# qiven_list_tests(<target>...): the listing step, called once, after every
# module's tests, from the directory whose CTest file holds the tests. The
# listing is written after each case binary, each fuzz target and each
# target named (the binaries the drives run, which it never runs) is built,
# and again when one is relinked; until it is written, CTest finds the one
# failing test below.
function(qiven_list_tests)
  set(qiven_bin "${PROJECT_BINARY_DIR}/bin")
  set(qiven_runner_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")

  # The AddressSanitizer runtime beside each test binary: every binary is
  # written to one directory. The listing step runs the case binaries, so it
  # waits for the copy (below): a binary started while the DLL is being
  # written fails to load it.
  set(qiven_runtime_files "")
  if(MSVC AND QIVEN_SANITIZE MATCHES "address")
    get_filename_component(qiven_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(qiven_asan_dll clang_rt.asan_dbg_dynamic-x86_64.dll)
    else()
      set(qiven_asan_dll clang_rt.asan_dynamic-x86_64.dll)
    endif()
    if(NOT EXISTS "${qiven_cl_dir}/${qiven_asan_dll}")
      message(FATAL_ERROR "qiven: ${qiven_asan_dll} is not beside the compiler")
    endif()
    add_custom_command(OUTPUT "${qiven_bin}/${qiven_asan_dll}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${qiven_cl_dir}/${qiven_asan_dll}" "${qiven_bin}/"
      DEPENDS "${qiven_cl_dir}/${qiven_asan_dll}"
      VERBATIM)
    add_custom_target(qiven_asan_runtime ALL DEPENDS "${qiven_bin}/${qiven_asan_dll}")
    list(APPEND qiven_runtime_files "${qiven_bin}/${qiven_asan_dll}")
  endif()

  # The listing step's inputs: each case binary's labels, its own limit or
  # none, and its file.
  get_property(qiven_case_binaries GLOBAL PROPERTY QIVEN_CASE_BINARIES)
  set(qiven_inputs "")
  foreach(target IN LISTS qiven_case_binaries)
    get_property(labels GLOBAL PROPERTY QIVEN_CASE_LABELS_${target})
    get_property(limit GLOBAL PROPERTY QIVEN_CASE_LIMIT_${target})
    string(APPEND qiven_inputs "${labels}\t${limit}\t$<TARGET_FILE:${target}>\n")
  endforeach()
  set(qiven_inputs_file "${CMAKE_CURRENT_BINARY_DIR}/case_binaries.txt")
  file(GENERATE OUTPUT "${qiven_inputs_file}" CONTENT "${qiven_inputs}")

  set(qiven_python_args "")
  set(qiven_python_files "")
  if(Python3_EXECUTABLE)
    set(qiven_python_args -D "PYTHON=${Python3_EXECUTABLE}")
    # every tree the listing discovers in (foundation/devkit/test/runner/list_tests.py)
    qiven_python_globs("${PROJECT_SOURCE_DIR}" qiven_python_globs)
    file(GLOB_RECURSE qiven_python_files CONFIGURE_DEPENDS ${qiven_python_globs})
  else()
    message(STATUS "qiven: no interpreter named; the Python tests are not registered")
  endif()

  # Under libFuzzer, the fuzz targets' lines (tests/fuzz/CMakeLists.txt), each a
  # CTest test.
  set(qiven_fuzz_args "")
  get_property(qiven_fuzz_binaries GLOBAL PROPERTY QIVEN_FUZZ_BINARIES)
  if(qiven_fuzz_binaries)
    set(qiven_fuzz_args -D "FUZZ=${CMAKE_CURRENT_BINARY_DIR}/fuzz_targets.txt")
  endif()

  set(qiven_tests_file "${CMAKE_CURRENT_BINARY_DIR}/ctest_tests.cmake")
  add_custom_command(OUTPUT "${qiven_tests_file}"
    COMMAND "${CMAKE_COMMAND}" -D "INPUTS=${qiven_inputs_file}" -D "OUT=${qiven_tests_file}"
      ${qiven_python_args} ${qiven_fuzz_args} -D "SOURCE=${PROJECT_SOURCE_DIR}" -D "BIN=${qiven_bin}"
      -D "BUILD=${PROJECT_BINARY_DIR}" -D "CTEST=${CMAKE_CTEST_COMMAND}" -D "SKIP=${QIVEN_SKIP_CODE}"
      -P "${qiven_runner_dir}/ctest_tests.cmake"
    DEPENDS ${qiven_case_binaries} ${qiven_fuzz_binaries} ${ARGN}
      ${qiven_runtime_files} "${qiven_inputs_file}"
      "${qiven_runner_dir}/ctest_tests.cmake" "${qiven_runner_dir}/list_tests.py" ${qiven_python_files}
      # the trees the table's reader reads, which labels each test by its module; the reader is one of the Python files
      "${PROJECT_SOURCE_DIR}/tools/trees.json"
    COMMENT "Listing the C++ cases and the Python tests for CTest"
    VERBATIM)
  add_custom_target(qiven_ctest_tests ALL DEPENDS "${qiven_tests_file}")

  # Before the build has listed the tests, CTest finds one failing test that
  # says so, in every preset's selection but fuzz, bench and drill.
  set(qiven_include "${CMAKE_CURRENT_BINARY_DIR}/ctest_include.cmake")
  file(WRITE "${qiven_include}" "if(EXISTS [==[${qiven_tests_file}]==])
  include([==[${qiven_tests_file}]==])
else()
  add_test(qiven.not_built [==[${CMAKE_COMMAND}]==] -E false)
  set_tests_properties(qiven.not_built PROPERTIES LABELS \"cxx;py;fast;fake;runner\")
  message(WARNING \"qiven: the tests are not listed yet: build first\")
endif()
")
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${qiven_include}")
endfunction()
