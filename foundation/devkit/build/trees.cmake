# The trees of tools/trees.json, the one place that names them (tools/loop_modules.py
# reads it too), and the rules the build's globs and filters take from them; the
# header units' are header_units.cmake's, which it includes.
# Included by the root CMakeLists.txt; run by tests/drive/test_trees_build.py as
# cmake -P with ROOT (the checkout) and SHOW (what to print).

# qiven3_trees(<file>): qiven3_layers, qiven3_walked, qiven3_analysed,
# qiven3_included, qiven3_tables, and the C++ suffixes qiven3_source_suffixes
# and qiven3_header_suffixes, its lists, in the caller's scope; with
# qiven3_source_suffix and qiven3_header_suffix, the regular expressions
# ending a path in one; qiven3_tests, the top-level test tree; and
# qiven3_homes, each module directory of the checkout the file is in.
macro(qiven3_trees file)
  file(READ "${file}" qiven3_trees_text)
  string(JSON qiven3_tests GET "${qiven3_trees_text}" tests)
  foreach(qiven3_key IN ITEMS layers walked analysed included tables source_suffixes header_suffixes)
    set(qiven3_${qiven3_key} "")
    string(JSON qiven3_count LENGTH "${qiven3_trees_text}" ${qiven3_key})
    math(EXPR qiven3_last "${qiven3_count} - 1")
    foreach(qiven3_at RANGE ${qiven3_last})
      string(JSON qiven3_tree GET "${qiven3_trees_text}" ${qiven3_key} ${qiven3_at})
      list(APPEND qiven3_${qiven3_key} "${qiven3_tree}")
    endforeach()
  endforeach()
  foreach(qiven3_key IN ITEMS source header)
    list(JOIN qiven3_${qiven3_key}_suffixes "|" qiven3_${qiven3_key}_suffix)
    string(REPLACE "." "[.]" qiven3_${qiven3_key}_suffix "(${qiven3_${qiven3_key}_suffix})$")
  endforeach()
  get_filename_component(qiven3_root "${file}/../.." ABSOLUTE)
  qiven3_table_globs("${qiven3_root}" qiven3_globs)
  file(GLOB_RECURSE qiven3_homes RELATIVE "${qiven3_root}" ${qiven3_globs})
  foreach(qiven3_table IN LISTS qiven3_tables)
    if(EXISTS "${qiven3_root}/${qiven3_table}")
      list(APPEND qiven3_homes "${qiven3_table}")
    endif()
  endforeach()
  list(TRANSFORM qiven3_homes REPLACE "/?module[.]json$" "")
endmacro()

# qiven3_header_globs(<root> <out> <tree>...): a glob under each tree of root
# for each header suffix.
function(qiven3_header_globs root out)
  set(globs "")
  foreach(tree IN LISTS ARGN)
    foreach(suffix IN LISTS qiven3_header_suffixes)
      list(APPEND globs "${root}/${tree}/*${suffix}")
    endforeach()
  endforeach()
  set(${out} ${globs} PARENT_SCOPE)
endfunction()

# qiven3_own_source(<root> <stem> <out>): stem, from the root, with the first
# source suffix it exists under; empty where none does.
function(qiven3_own_source root stem out)
  set(${out} "" PARENT_SCOPE)
  foreach(suffix IN LISTS qiven3_source_suffixes)
    if(EXISTS "${root}/${stem}${suffix}")
      set(${out} "${stem}${suffix}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# qiven3_under(<path> <tree> <out>): TRUE where path, from the root, lies
# under tree.
function(qiven3_under path tree out)
  string(FIND "${path}/" "${tree}/" at)
  if(at EQUAL 0)
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

# qiven3_test_path(<path> <out>): the one reader of test code, test_path in
# tools/loop_modules.py in Python: path, from the root, under the top-level
# tests/ as it stands; under the tests/ of a module directory as the
# top-level tree would hold it; any other path, empty.
function(qiven3_test_path path out)
  set(${out} "" PARENT_SCOPE)
  set(from 0)
  while(TRUE)
    string(SUBSTRING "/${path}" ${from} -1 rest)
    string(FIND "${rest}" "/${qiven3_tests}/" at)
    if(at LESS 0)
      return()
    endif()
    math(EXPR at "${from} + ${at}")
    string(SUBSTRING "${path}" 0 ${at} home)
    string(REGEX REPLACE "/$" "" home "${home}")
    if(at EQUAL 0 OR home IN_LIST qiven3_homes)
      string(SUBSTRING "${path}" ${at} -1 rest)
      set(${out} "${rest}" PARENT_SCOPE)
      return()
    endif()
    math(EXPR from "${at} + 1")
  endwhile()
endfunction()

# qiven3_source_path(<dir> <source> <out>): a source of a target defined in
# dir, from the root, as qiven3_analysed reads it: a generated one as its
# directory's own; empty for the diagnostic entry's objects, which every
# executable takes (qiven_executable) and are none of its own sources.
function(qiven3_source_path dir source out)
  set(${out} "" PARENT_SCOPE)
  if(source MATCHES "^[$]<TARGET_OBJECTS:qiven3_diag_entry[A-Za-z0-9_]*>$")
    return()
  endif()
  get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${dir}")
  qiven3_under("${source}" "${PROJECT_BINARY_DIR}" generated)
  if(generated)
    get_filename_component(name "${source}" NAME)
    set(source "${dir}/${name}")
  endif()
  file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}" "${source}")
  set(${out} "${rel}" PARENT_SCOPE)
endfunction()

# qiven3_tidy_dir(<source> <out>): the checkout's directory whose .clang-tidy
# files hold for source: its own, for one generated, its directory in the build.
function(qiven3_tidy_dir source out)
  get_filename_component(dir "${source}" DIRECTORY)
  qiven3_under("${source}" "${PROJECT_BINARY_DIR}" generated)
  if(generated)
    file(RELATIVE_PATH in_build "${PROJECT_BINARY_DIR}" "${dir}")
    set(dir "${PROJECT_SOURCE_DIR}/${in_build}")
  endif()
  set(${out} "${dir}" PARENT_SCOPE)
endfunction()

# qiven3_test_target(<dir> <target> <out>): TRUE where target, defined in
# dir, has own sources, read by qiven3_source_path, and each is test code.
# A header's unit (tests/support/headers/CMakeLists.txt) reads as the header
# it stands for, its source property QIVEN3_HEADER.
function(qiven3_test_target dir target out)
  set(${out} FALSE PARENT_SCOPE)
  set(own FALSE)
  get_target_property(sources ${target} SOURCES)
  foreach(source IN LISTS sources)
    qiven3_source_path("${dir}" "${source}" rel)
    if(NOT rel STREQUAL "")
      get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${dir}")
      get_source_file_property(header "${path}" DIRECTORY "${dir}" QIVEN3_HEADER)
      if(header)
        set(rel "${header}")
      endif()
    endif()
    qiven3_test_path("${rel}" test)
    if(NOT rel STREQUAL "" AND test STREQUAL "")
      return()
    elseif(NOT rel STREQUAL "")
      set(own TRUE)
    endif()
  endforeach()
  set(${out} ${own} PARENT_SCOPE)
endfunction()

# qiven3_test_targets(<out>): the project's compiled targets, a plant's
# (QIVEN3_PLANT) aside, that take test code's analysis: qiven3_test_target's,
# with a source the analysis reads (qiven3_analysed).
function(qiven3_test_targets out)
  set(found "")
  set(dirs "${PROJECT_SOURCE_DIR}")
  while(dirs)
    list(POP_FRONT dirs dir)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    list(APPEND dirs ${subdirs})
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      get_target_property(plant ${target} QIVEN3_PLANT)
      if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$" AND NOT plant)
        qiven3_test_target("${dir}" ${target} test)
        get_target_property(sources ${target} SOURCES)
        foreach(source IN LISTS sources)
          qiven3_source_path("${dir}" "${source}" rel)
          qiven3_analysed("${rel}" read)
          if(test AND read AND rel MATCHES "${qiven3_source_suffix}")
            list(APPEND found ${target})
            break()
          endif()
        endforeach()
      endif()
    endforeach()
  endwhile()
  set(${out} ${found} PARENT_SCOPE)
endfunction()

# qiven3_analysed(<path> <out>): TRUE where the analysis reads the file at
# path, from the root: under an analysed root, as it stands or as the
# top-level tree would hold it, or under a layer's tree and no test code, a
# module's own tests, which an analysed root names where the analysis reads
# them.
function(qiven3_analysed path out)
  set(${out} FALSE PARENT_SCOPE)
  qiven3_test_path("${path}" test)
  foreach(root IN LISTS qiven3_analysed)
    qiven3_under("${path}" "${root}" under)
    qiven3_under("${test}" "${root}" as_test)
    if(under OR as_test)
      set(${out} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(layer IN LISTS qiven3_layers)
    qiven3_under("${path}" "${layer}" under)
    if(under AND test STREQUAL "")
      set(${out} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# qiven3_path_regex(<path> <out>): a regular expression matching path, its
# separators either / or \, a drive letter in either case.
function(qiven3_path_regex path out)
  string(REGEX REPLACE "([][.^$|()*+?{}\\\\])" "\\\\\\1" regex "${path}")
  string(REPLACE "/" "[/\\]" regex "${regex}")
  if(regex MATCHES "^([A-Za-z]):")
    string(TOUPPER "${CMAKE_MATCH_1}" upper)
    string(TOLOWER "${CMAKE_MATCH_1}" lower)
    string(REGEX REPLACE "^[A-Za-z]:" "[${upper}${lower}]:" regex "${regex}")
  endif()
  set(${out} "${regex}" PARENT_SCOPE)
endfunction()

# qiven3_header_filters(<root> <headers> <filter> <exclude>): clang-tidy's
# header filters for the checkout at root: filter, the headers under the
# analysed roots and the layers' trees; exclude, the directories of those
# headers, from the root, that the analysis does not read (empty where none).
function(qiven3_header_filters root headers filter exclude)
  qiven3_path_regex("${root}" root_regex)
  set(trees "")
  foreach(tree IN LISTS qiven3_analysed qiven3_layers)
    qiven3_path_regex("${tree}" tree_regex)
    list(APPEND trees "${tree_regex}")
  endforeach()
  list(JOIN trees "|" trees)
  set(${filter} "${root_regex}[/\\](${trees})[/\\]" PARENT_SCOPE)
  set(dirs "")
  foreach(header IN LISTS headers)
    qiven3_analysed("${header}" read)
    if(NOT read)
      get_filename_component(dir "${header}" DIRECTORY)
      qiven3_path_regex("${dir}" dir_regex)
      list(APPEND dirs "${dir_regex}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES dirs)
  list(SORT dirs)
  set(${exclude} "" PARENT_SCOPE)
  if(dirs)
    list(JOIN dirs "|" dirs)
    set(${exclude} "${root_regex}[/\\](${dirs})[/\\][^/\\]*$" PARENT_SCOPE)
  endif()
endfunction()

# qiven3_table_globs(<root> <out>): the globs of the table's files under the
# layers' trees, each module.json at any depth.
function(qiven3_table_globs root out)
  set(globs "")
  foreach(layer IN LISTS qiven3_layers)
    list(APPEND globs "${root}/${layer}/module.json")
  endforeach()
  set(${out} ${globs} PARENT_SCOPE)
endfunction()

# qiven3_python_globs(<root> <out>): the globs of the Python files under the
# walked trees and the layers', where the listing finds the Python tests.
function(qiven3_python_globs root out)
  set(globs "")
  foreach(tree IN LISTS qiven3_walked qiven3_layers)
    list(APPEND globs "${root}/${tree}/*.py")
  endforeach()
  set(${out} ${globs} PARENT_SCOPE)
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/header_units.cmake")  # the header units

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  qiven3_trees("${ROOT}/tools/trees.json")
  if(SHOW STREQUAL "filters")
    qiven3_header_globs("${ROOT}" globs ${qiven3_layers} ${qiven3_analysed})  # the root CMakeLists.txt's
    file(GLOB_RECURSE headers RELATIVE "${ROOT}" ${globs})
    qiven3_header_filters("${ROOT}" "${headers}" filter exclude)
    message("filter=${filter}")
    message("exclude=${exclude}")
  elseif(SHOW STREQUAL "analysed")  # for the lines of PATHFILE
    file(STRINGS "${PATHFILE}" paths)
    foreach(path IN LISTS paths)
      qiven3_analysed("${path}" read)
      qiven3_test_path("${path}" test)
      message("${path}=${read} ${test}")
    endforeach()
  elseif(SHOW STREQUAL "globs")
    qiven3_table_globs("${ROOT}" table)
    qiven3_python_globs("${ROOT}" python)
    message("table=${table}")
    message("python=${python}")
  elseif(SHOW STREQUAL "units")
    qiven3_header_units("${ROOT}" units)
    message("units=${units}")
  elseif(SHOW STREQUAL "through")  # header=source each header compiles through
    qiven3_header_units("${ROOT}" units)
    foreach(pair IN LISTS units_of)
      message("${pair}")
    endforeach()
  endif()
endif()
