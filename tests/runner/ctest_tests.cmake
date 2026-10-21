# Run after the build (cmake -P): lists each case binary's cases and each
# Python test's id, and writes one add_test() each into OUT.
# Inputs: INPUTS (lines "<labels>\t<limit>\t<binary>", labels by commas, the
# limit empty or a binary's own), OUT, SOURCE, BIN, BUILD, CTEST, SKIP,
# ADDR2LINE where the build found one, and PYTHON when the Python tests are
# registered. With PYTHON every test runs behind the watcher, tools/watch.py,
# under its limit: by its labels, a slow test 1,800 s, a fast one 60 s, any
# other 300 s, unless it states its own. CTest's TIMEOUT is the limit plus
# 30 s, so that the watcher acts first.
cmake_minimum_required(VERSION 3.28)

set(body "# Written by tests/runner/ctest_tests.cmake after the build.\n")
set(seen "")
set(cases 0)
set(python_tests 0)
set(SLOW_LIMIT 1800)
set(FAST_LIMIT 60)
set(OTHER_LIMIT 300)
set(AFTER_LIMIT 30)
set(environment "QIVEN3_BIN=${BIN};QIVEN3_BUILD=${BUILD};QIVEN3_CTEST=${CTEST}")
if(ADDR2LINE)
  string(APPEND environment ";QIVEN3_ADDR2LINE=${ADDR2LINE}")
endif()
if(DEFINED PYTHON)
  file(TO_CMAKE_PATH "${PYTHON}" PYTHON)
endif()

# qiven3_add_test(<name> <labels;...> <serial> <directory> <limit> <command>...):
# one test, behind the watcher where there is an interpreter, and its
# properties; <directory> is its working directory, or empty; <limit> its
# own seconds, or empty for its labels'.
function(qiven3_add_test name labels serial directory limit)
  if("${name}" IN_LIST seen)
    message(FATAL_ERROR "two tests are named ${name}")
  endif()
  list(APPEND seen "${name}")
  if(NOT limit STREQUAL "")
    if(NOT limit MATCHES "^[1-9][0-9]*$")
      message(FATAL_ERROR "${name}: a limit of '${limit}' is no whole number of seconds")
    endif()
  elseif("slow" IN_LIST labels)
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
    set(watched "${PYTHON}" -I -X faulthandler "${SOURCE}/tools/watch.py" --limit ${limit} --name "${name}" -- ${ARGN})
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
  if(NOT entry MATCHES "^([a-z,]*)\t([0-9]*)\t(.+)$")
    message(FATAL_ERROR "a malformed line of ${INPUTS}: ${entry}")
  endif()
  string(REPLACE "," ";" binary_labels "cxx,${CMAKE_MATCH_1}")
  set(binary_limit "${CMAKE_MATCH_2}")
  set(binary "${CMAKE_MATCH_3}")
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
    if(NOT line MATCHES "^([A-Za-z0-9_]+\\.[A-Za-z0-9_.]+)(\tserial)?$")
      message(FATAL_ERROR "${binary} --list gave a malformed line: '${line}'")
    endif()
    set(name "${CMAKE_MATCH_1}")
    set(serial FALSE)
    if(CMAKE_MATCH_2)
      set(serial TRUE)
    endif()
    qiven3_add_test("${name}" "${binary_labels}" ${serial} "" "${binary_limit}" "${binary}" --run "${name}")
    math(EXPR cases "${cases} + 1")
  endforeach()
endforeach()

if(DEFINED PYTHON)
  execute_process(COMMAND "${PYTHON}" -I "${SOURCE}/tests/runner/list_tests.py"
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
    if(NOT line MATCHES "^(tests\\.[A-Za-z0-9_.]+)\t([a-z,]+)\t([01])\t([0-9]*)$")
      message(FATAL_ERROR "the Python listing gave a malformed line: '${line}'")
    endif()
    set(id "${CMAKE_MATCH_1}")
    string(REPLACE "," ";" labels "py,${CMAKE_MATCH_2}")
    set(serial FALSE)
    if(CMAKE_MATCH_3 STREQUAL "1")
      set(serial TRUE)
    endif()
    set(test_limit "${CMAKE_MATCH_4}")
    # From the root: -I keeps the working directory off sys.path, and
    # discover puts the top directory there. A -k pattern holding * is
    # matched whole, so '*<id>' selects that one test.
    qiven3_add_test("${id}" "${labels}" ${serial} "${SOURCE}" "${test_limit}"
      "${PYTHON}" -I -m unittest discover -s tests -t . -k "*${id}")
    math(EXPR python_tests "${python_tests} + 1")
  endforeach()
endif()

file(WRITE "${OUT}" "${body}")
message(STATUS "qiven3: ${cases} C++ cases and ${python_tests} Python tests listed for CTest")
