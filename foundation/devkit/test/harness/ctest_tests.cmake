# Run after the build (cmake -P): lists each case binary's cases and each
# Python test's id, and writes one add_test() each into OUT.
# Inputs: INPUTS (lines "<labels>\t<binary>", labels by commas), OUT,
# SOURCE, BIN, BUILD, CTEST, SKIP,
# PYTHON when the Python tests are
# registered, LISTING, list_tests.py's arguments by commas, if any,
# and FUZZ when the build has fuzz targets (lines
# "<target>	<binary>	<corpus>	<artifacts>"). With PYTHON every test runs behind the watcher, watch.py beside this file,
# under its limit by its labels: a slow test 1,800 s, a fast one 60 s, any
# other 300 s. CTest's TIMEOUT is the limit plus 30 s, so that the watcher
# acts first.
cmake_minimum_required(VERSION 3.28)

set(body "# Written by foundation/devkit/test/harness/ctest_tests.cmake after the build.\n")
set(seen "")
set(cases 0)
set(python_tests 0)
set(SLOW_LIMIT 1800)
set(FAST_LIMIT 60)
set(OTHER_LIMIT 300)
set(AFTER_LIMIT 30)
# A test's skip is its exit, SKIP, which the watcher passes through: a C++
# case's by the runner, a Python test's by its case entry, case.py beside
# this file.
set(environment "QIVEN_BIN=${BIN};QIVEN_BUILD=${BUILD};QIVEN_CTEST=${CTEST}")
if(DEFINED PYTHON)
  file(TO_CMAKE_PATH "${PYTHON}" PYTHON)
endif()

# qiven_add_test(<name> <labels;...> <serial> <directory> <command>...):
# one test, behind the watcher where there is an interpreter, and its
# properties; <serial> TRUE for a fuzz target's run alone (RUN_SERIAL), FALSE
# for every other test; <directory> is its working directory, or empty.
function(qiven_add_test name labels serial directory)
  if("${name}" IN_LIST seen)
    message(FATAL_ERROR "two tests are named ${name}")
  endif()
  list(APPEND seen "${name}")
  if("slow" IN_LIST labels)
    set(limit ${SLOW_LIMIT})
  elseif("fast" IN_LIST labels)
    set(limit ${FAST_LIMIT})
  else()
    set(limit ${OTHER_LIMIT})
  endif()
  math(EXPR timeout "${limit} + ${AFTER_LIMIT}")
  set(command "")
  set(watched ${ARGN})
  if(DEFINED PYTHON)
    set(watched "${PYTHON}" -I -X faulthandler "${SOURCE}/foundation/devkit/test/harness/watch.py" --limit ${limit} --name "${name}" -- ${ARGN})
  endif()
  foreach(arg IN ITEMS ${watched})
    string(APPEND command " [==[${arg}]==]")
  endforeach()
  string(APPEND body "add_test([==[${name}]==]${command})\n")
  string(APPEND body "set_tests_properties([==[${name}]==] PROPERTIES LABELS \"${labels}\" SKIP_RETURN_CODE ${SKIP}"
    " TIMEOUT ${timeout} ENVIRONMENT [==[${environment}]==]")
  if(serial)
    string(APPEND body " RUN_SERIAL TRUE")
  endif()
  if(directory)
    string(APPEND body " WORKING_DIRECTORY [==[${directory}]==]")
  endif()
  string(APPEND body ")\n")
  set(body "${body}" PARENT_SCOPE)
  set(seen "${seen}" PARENT_SCOPE)
endfunction()

file(STRINGS "${INPUTS}" entries)
foreach(entry IN LISTS entries)
  if(NOT entry MATCHES "^([a-z0-9_,]*)\t(.+)$")
    message(FATAL_ERROR "a malformed line of ${INPUTS}: ${entry}")
  endif()
  string(REPLACE "," ";" binary_labels "cxx,${CMAKE_MATCH_1}")
  set(binary "${CMAKE_MATCH_2}")
  execute_process(COMMAND "${binary}" --list
    OUTPUT_VARIABLE listed ERROR_VARIABLE listed_err RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "${binary} --list gave ${status}: ${listed_err}")
  endif()
  string(REPLACE "\n" ";" lines "${listed}")
  foreach(line IN LISTS lines)
    if(line STREQUAL "")
      continue()
    endif()
    if(NOT line MATCHES "^[A-Za-z0-9_]+\\.[A-Za-z0-9_.]+$")
      message(FATAL_ERROR "${binary} --list gave a malformed line: '${line}'")
    endif()
    qiven_add_test("${line}" "${binary_labels}" FALSE "" "${binary}" --run "${line}")
    math(EXPR cases "${cases} + 1")
  endforeach()
endforeach()

# Each fuzz target, fuzz.<target>, labelled cxx and fuzz and alone: 60 s of
# libFuzzer from its corpus, 10 s an input, a finding's input under its
# artifacts.
set(fuzz_targets 0)
if(DEFINED FUZZ)
  file(STRINGS "${FUZZ}" entries)
  foreach(entry IN LISTS entries)
    if(NOT entry MATCHES "^([a-z]+)	([^	]+)	([^	]+)	([^	]+)$")
      message(FATAL_ERROR "a malformed line of ${FUZZ}: ${entry}")
    endif()
    qiven_add_test("fuzz.${CMAKE_MATCH_1}" "cxx;fuzz" TRUE "" "${CMAKE_MATCH_2}" -max_total_time=60
      -timeout=10 "-artifact_prefix=${CMAKE_MATCH_4}/" "${CMAKE_MATCH_3}")
    math(EXPR fuzz_targets "${fuzz_targets} + 1")
  endforeach()
endif()

if(DEFINED PYTHON)
  string(REPLACE "," ";" listing "${LISTING}")
  execute_process(COMMAND "${PYTHON}" -I "${SOURCE}/foundation/devkit/test/harness/list_tests.py" ${listing}
    WORKING_DIRECTORY "${SOURCE}"
    OUTPUT_VARIABLE listed ERROR_VARIABLE listed_err RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "listing the Python tests gave ${status}: ${listed_err}")
  endif()
  string(REPLACE "\n" ";" lines "${listed}")
  foreach(line IN LISTS lines)
    if(line STREQUAL "")
      continue()
    endif()
    if(NOT line MATCHES "^([A-Za-z0-9_]+\\.[A-Za-z0-9_.]+)\t([a-z0-9_,]+)$")
      message(FATAL_ERROR "the Python listing gave a malformed line: '${line}'")
    endif()
    set(id "${CMAKE_MATCH_1}")
    string(REPLACE "," ";" labels "py,${CMAKE_MATCH_2}")
    # From the root, by its case entry, which runs that one id and exits
    # SKIP on a skip, its reason printed.
    set(test_command "${PYTHON}" -I "${SOURCE}/foundation/devkit/test/harness/case.py" --skip ${SKIP} "${id}")
    qiven_add_test("${id}" "${labels}" FALSE "${SOURCE}" ${test_command})
    math(EXPR python_tests "${python_tests} + 1")
  endforeach()
endif()

file(WRITE "${OUT}" "${body}")
message(STATUS "qiven: ${cases} C++ cases, ${fuzz_targets} fuzz targets and ${python_tests} Python tests "
  "listed for CTest")
