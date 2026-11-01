# qiven3_headers, the analysis preset's alone: a translation unit for each
# header-only module of the trees the analysis reads (tools/trees.json, by
# the root CMakeLists.txt), a header with no source of its own, so that
# /analyze and clang-tidy read the header and its templates' bodies, which a
# source reads only where it includes the header and instantiates them.
# Each unit includes its header and instantiates its templates with the
# types the code gives them: the fake platform, v3's allocator and the
# standard's, the stores' default hooks. An object library: compiled, and
# taken by no binary.
#
# A header's own source is <name>.cpp beside it or, for include/qiven/<m>/<p>.hpp
# of a module of a layer's tree, the module's src/<p>.cpp. Its unit is its path
# under its analysed root, / as _ and .cpp for .hpp, after the root's last
# name and _ where the root is no walked tree (support_ for tests/support/),
# or its path under include/qiven/ the same way. The headers checked: those of
# an analysed root that holds a unit (src/ and tests/support/), and those the
# analysis reads of a layer's module that holds a unit or no source outside
# its tests/. Configure fails on a checked header with neither, or on a unit
# with no such header.
file(GLOB qiven3_units RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
set(qiven3_header_units "")
# qiven3_check(<headers>): each header with no own source is <header>=<unit> in
# qiven3_wanted; qiven3_holds TRUE where one of those units is present.
function(qiven3_check)
  set(wanted "")
  set(holds FALSE)
  foreach(entry IN LISTS ARGN)
    string(REPLACE "=" ";" entry "${entry}")
    list(GET entry 0 header)
    list(GET entry 1 unit)
    list(GET entry 2 own)
    if(EXISTS "${PROJECT_SOURCE_DIR}/${own}")
      continue()
    endif()
    string(REGEX REPLACE "[.]hpp$" ".cpp" beside "${header}")
    if(EXISTS "${PROJECT_SOURCE_DIR}/${beside}")
      continue()
    endif()
    list(APPEND wanted "${header}=${unit}")
    if(unit IN_LIST qiven3_units)
      set(holds TRUE)
    endif()
  endforeach()
  set(qiven3_wanted "${wanted}" PARENT_SCOPE)
  set(qiven3_holds ${holds} PARENT_SCOPE)
endfunction()
# qiven3_take(<force>): each wanted header's unit, its absence fatal where the
# headers' tree holds a unit or force.
macro(qiven3_take force)
  foreach(qiven3_entry IN LISTS qiven3_wanted)
    string(REPLACE "=" ";" qiven3_entry "${qiven3_entry}")
    list(GET qiven3_entry 0 header)
    list(GET qiven3_entry 1 unit)
    if(unit IN_LIST qiven3_units)
      list(APPEND qiven3_header_units "${unit}")
    elseif(qiven3_holds OR ${force})
      message(FATAL_ERROR "qiven3: the header-only module ${header} has no unit, tests/support/headers/${unit}")
    endif()
  endforeach()
endmacro()
foreach(root IN LISTS qiven3_analysed)
  set(qiven3_inside FALSE)
  foreach(layer IN LISTS qiven3_layers)
    qiven3_under("${root}" "${layer}" under)
    if(under)
      set(qiven3_inside TRUE)  # its headers its layer's below
    endif()
  endforeach()
  if(qiven3_inside)
    continue()
  endif()
  set(prefix "")
  if(NOT root IN_LIST qiven3_walked)
    get_filename_component(prefix "${root}" NAME)
    string(APPEND prefix "_")
  endif()
  file(GLOB_RECURSE headers RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/${root}/*.hpp")
  set(entries "")
  foreach(header IN LISTS headers)
    string(LENGTH "${root}/" at)
    string(SUBSTRING "${header}" ${at} -1 unit)
    string(REGEX REPLACE "[.]hpp$" ".cpp" unit "${prefix}${unit}")
    string(REPLACE "/" "_" unit "${unit}")
    list(APPEND entries "${header}=${unit}=-")  # its own source beside it alone
  endforeach()
  qiven3_check(${entries})
  qiven3_take(FALSE)
endforeach()
# A layer's modules, each the directory of a module.json, its headers those the
# analysis reads below it and above any module under it.
qiven3_table_globs("${PROJECT_SOURCE_DIR}" qiven3_globs)
file(GLOB_RECURSE qiven3_modules RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS ${qiven3_globs})
set(qiven3_layer_globs "")
foreach(layer IN LISTS qiven3_layers)
  list(APPEND qiven3_layer_globs "${PROJECT_SOURCE_DIR}/${layer}/*.hpp" "${PROJECT_SOURCE_DIR}/${layer}/*.cpp")
endforeach()
file(GLOB_RECURSE qiven3_layer_files RELATIVE "${PROJECT_SOURCE_DIR}" CONFIGURE_DEPENDS ${qiven3_layer_globs})
foreach(module IN LISTS qiven3_modules)
  get_filename_component(module "${module}" DIRECTORY)
  set(entries "")
  set(sources FALSE)
  foreach(file IN LISTS qiven3_layer_files)
    qiven3_under("${file}" "${module}" under)
    set(nearest "${file}")
    while(under)
      get_filename_component(nearest "${nearest}" DIRECTORY)
      if(nearest STREQUAL module OR EXISTS "${PROJECT_SOURCE_DIR}/${nearest}/module.json")
        break()
      endif()
    endwhile()
    qiven3_analysed("${file}" read)
    if(NOT under OR NOT read OR NOT nearest STREQUAL module)
      continue()
    endif()
    if(file MATCHES "[.]cpp$")
      set(sources TRUE)
      continue()
    endif()
    string(LENGTH "${module}/" at)
    string(SUBSTRING "${file}" ${at} -1 inner)
    set(own "${module}/-")
    set(unit "${inner}")
    if(inner MATCHES "^include/qiven/[^/]+/(.+)[.]hpp$")
      set(own "${module}/src/${CMAKE_MATCH_1}.cpp")
      string(REGEX REPLACE "^include/qiven/" "" unit "${inner}")
    endif()
    string(REGEX REPLACE "[.]hpp$" ".cpp" unit "${unit}")
    string(REPLACE "/" "_" unit "${unit}")
    list(APPEND entries "${file}=${unit}=${own}")
  endforeach()
  qiven3_check(${entries})
  if(sources)
    qiven3_take(FALSE)
  else()
    qiven3_take(TRUE)
  endif()
endforeach()
list(REMOVE_DUPLICATES qiven3_header_units)
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
  qiven3_log qiven3_diag qiven_runner qiven_build)
list(LENGTH qiven3_header_units qiven3_count)
message(STATUS "qiven3: ${qiven3_count} header-only modules, each its unit in qiven3_headers")
