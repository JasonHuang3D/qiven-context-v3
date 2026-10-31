# qiven3_headers, the analysis preset's alone: a translation unit for each
# header-only module of src/ and tests/support/, a header with no source of
# its own beside it, so that /analyze and clang-tidy read the header and its
# templates' bodies, which a source reads only where it includes the header
# and instantiates them. Each unit includes its header and instantiates its
# templates with the types the code gives them: the fake platform, v3's
# allocator and the standard's, the stores' default hooks. An object
# library: compiled, and taken by no binary. Configure fails on a
# header-only module without its unit, <directory>_<name>.cpp from the
# header's path under src/, support_<directory>_<name>.cpp under
# tests/support/, or on a unit with no such header.
file(GLOB_RECURSE qiven3_headers RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/tests/support/*.hpp")
set(qiven3_header_units "")
foreach(header IN LISTS qiven3_headers)
  string(REGEX REPLACE "[.]hpp$" ".cpp" own "${header}")
  if(EXISTS "${PROJECT_SOURCE_DIR}/${own}")
    continue()
  endif()
  string(REGEX REPLACE "^src/" "" unit "${own}")
  string(REGEX REPLACE "^tests/support/" "support/" unit "${unit}")
  string(REPLACE "/" "_" unit "${unit}")
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${unit}")
    message(FATAL_ERROR "qiven3: the header-only module ${header} has no unit, tests/support/headers/${unit}")
  endif()
  list(APPEND qiven3_header_units "${unit}")
endforeach()
file(GLOB qiven3_units RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(unit IN LISTS qiven3_units)
  if(NOT unit IN_LIST qiven3_header_units)
    message(FATAL_ERROR "qiven3: tests/support/headers/${unit} is no header-only module's unit")
  endif()
endforeach()

add_library(qiven3_headers OBJECT ${qiven3_header_units})
# faults.hpp holds its loop under QIVEN_FAULTS alone; its unit defines it,
# where the build does not.
set_source_files_properties(support_faults_faults.cpp PROPERTIES COMPILE_DEFINITIONS QIVEN_FAULTS)
target_include_directories(qiven3_headers PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_headers PRIVATE qiven3_checked qiven3_store qiven3_platform_fake qiven3_fixtures qiven3_faults
  qiven3_log qiven3_diag qiven3_runner qiven_build)
list(LENGTH qiven3_header_units qiven3_count)
message(STATUS "qiven3: ${qiven3_count} header-only modules, each its unit in qiven3_headers")
