# qiven3_headers and qiven3_test_headers, the analysis presets' alone: a
# translation unit for each header the analysis reads (under an analysed root
# of tools/trees.json or a layer's tree, by the root CMakeLists.txt) with no
# source of its own, so that /analyze and clang-tidy read the header and its
# templates' bodies, which a source reads only where it includes the header
# and instantiates them. Each unit includes its header and instantiates its
# templates with the types the code gives them: the fake platform, v3's
# allocator and the standard's, the stores' default hooks. Object libraries:
# compiled, and taken by no binary.
#
# A unit stands for its header (QIVEN3_HEADER, read by qiven3_test_target),
# and takes its analysis by that header: one of test code, by the one reader,
# compiles in qiven3_test_headers, whose units are all test code, under test
# code's rule set and configuration; any other in qiven3_headers.
#
# The units, their names and the errors are foundation/devkit/build/header_units.cmake's
# (qiven3_header_units), which every configure with an interpreter runs
# (tests/CMakeLists.txt): a unit's name carries its header's module, and
# configure fails on such a header with no unit, on two headers of one unit,
# on a unit no such header wants, and on a unit that does not include its
# header (an own source that does not include it, or that no target
# compiles, stands in for none).

set(qiven3_library_units "")
set(qiven3_test_units "")
foreach(pair IN LISTS qiven3_header_units_of)
  if(pair MATCHES "^(.+)=tests/support/headers/(.+)$")
    set(header "${CMAKE_MATCH_1}")
    set(unit "${CMAKE_MATCH_2}")
    if(unit IN_LIST qiven3_header_units)
      set_source_files_properties("${unit}" PROPERTIES QIVEN3_HEADER "${header}")
      qiven3_test_path("${header}" test)
      if(test STREQUAL "")
        list(APPEND qiven3_library_units "${unit}")
      else()
        list(APPEND qiven3_test_units "${unit}")
      endif()
    endif()
  endif()
endforeach()
add_library(qiven3_headers OBJECT ${qiven3_library_units})
add_library(qiven3_test_headers OBJECT ${qiven3_test_units})
# faults.hpp holds its loop under QIVEN_FAULTS alone; its unit defines it,
# where the build does not.
set_source_files_properties(faults_faults.cpp PROPERTIES COMPILE_DEFINITIONS QIVEN_FAULTS)
foreach(target IN ITEMS qiven3_headers qiven3_test_headers)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
  target_link_libraries(${target} PRIVATE qiven3_checked qiven3_store qiven3_platform_fake qiven3_fixtures qiven_faults
    qiven3_log qiven3_diag qiven_runner qiven_case_main qiven_build)
endforeach()
list(LENGTH qiven3_library_units qiven3_count)
list(LENGTH qiven3_test_units qiven3_test_count)
message(STATUS "qiven3: header-only headers: ${qiven3_count} in qiven3_headers, "
  "${qiven3_test_count} in qiven3_test_headers")
