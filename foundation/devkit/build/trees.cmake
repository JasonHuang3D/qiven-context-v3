# The trees of tools/trees.json, the one place that names them (foundation/devkit/python/table.py
# reads it too), and the rules the build's globs and filters take from them; the
# header units' are header_units.cmake's, which it includes.
# Included by the root CMakeLists.txt; run by tests/test_trees_build.py as
# cmake -P with ROOT (the checkout) and SHOW (what to print).

# qiven_trees(<file>): qiven_layers, qiven_walked, qiven_analysed,
# qiven_included, qiven_vendored (the vendored files, which the analysis
# never reads), qiven_tables, and the C++ suffixes qiven_source_suffixes
# and qiven_header_suffixes, its lists, in the caller's scope; with
# qiven_source_suffix and qiven_header_suffix, the regular expressions
# ending a path in one; qiven_tests, the top-level test tree; and
# qiven_homes, each module directory of the checkout the file is in.
macro(qiven_trees file)
  file(READ "${file}" qiven_trees_text)
  string(JSON qiven_tests GET "${qiven_trees_text}" tests)
  foreach(qiven_key IN ITEMS layers walked analysed included vendored tables source_suffixes header_suffixes)
    set(qiven_${qiven_key} "")
    string(JSON qiven_count LENGTH "${qiven_trees_text}" ${qiven_key})
    math(EXPR qiven_last "${qiven_count} - 1")
    foreach(qiven_at RANGE ${qiven_last})
      string(JSON qiven_tree GET "${qiven_trees_text}" ${qiven_key} ${qiven_at})
      list(APPEND qiven_${qiven_key} "${qiven_tree}")
    endforeach()
  endforeach()
  foreach(qiven_key IN ITEMS source header)
    list(JOIN qiven_${qiven_key}_suffixes "|" qiven_${qiven_key}_suffix)
    string(REPLACE "." "[.]" qiven_${qiven_key}_suffix "(${qiven_${qiven_key}_suffix})$")
  endforeach()
  get_filename_component(qiven_root "${file}/../.." ABSOLUTE)
  qiven_table_globs("${qiven_root}" qiven_globs)
  file(GLOB_RECURSE qiven_homes RELATIVE "${qiven_root}" ${qiven_globs})
  foreach(qiven_table IN LISTS qiven_tables)
    if(EXISTS "${qiven_root}/${qiven_table}")
      list(APPEND qiven_homes "${qiven_table}")
    endif()
  endforeach()
  list(TRANSFORM qiven_homes REPLACE "/?module[.]json$" "")
endmacro()

# qiven_header_globs(<root> <out> <tree>...): a glob under each tree of root
# for each header suffix.
function(qiven_header_globs root out)
  set(globs "")
  foreach(tree IN LISTS ARGN)
    foreach(suffix IN LISTS qiven_header_suffixes)
      list(APPEND globs "${root}/${tree}/*${suffix}")
    endforeach()
  endforeach()
  set(${out} ${globs} PARENT_SCOPE)
endfunction()

# qiven_own_source(<root> <stem> <out>): stem, from the root, with the first
# source suffix it exists under; empty where none does.
function(qiven_own_source root stem out)
  set(${out} "" PARENT_SCOPE)
  foreach(suffix IN LISTS qiven_source_suffixes)
    if(EXISTS "${root}/${stem}${suffix}")
      set(${out} "${stem}${suffix}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# qiven_under(<path> <tree> <out>): TRUE where path, from the root, lies
# under tree.
function(qiven_under path tree out)
  string(FIND "${path}/" "${tree}/" at)
  if(at EQUAL 0)
    set(${out} TRUE PARENT_SCOPE)
  else()
    set(${out} FALSE PARENT_SCOPE)
  endif()
endfunction()

# qiven_test_path(<path> <out>): the one reader of test code, test_path in
# foundation/devkit/python/table.py in Python: path, from the root, under the top-level
# tests/ as it stands; under the tests/ of a module directory as the
# top-level tree would hold it; any other path, empty.
function(qiven_test_path path out)
  set(${out} "" PARENT_SCOPE)
  set(from 0)
  while(TRUE)
    string(SUBSTRING "/${path}" ${from} -1 rest)
    string(FIND "${rest}" "/${qiven_tests}/" at)
    if(at LESS 0)
      return()
    endif()
    math(EXPR at "${from} + ${at}")
    string(SUBSTRING "${path}" 0 ${at} home)
    string(REGEX REPLACE "/$" "" home "${home}")
    if(at EQUAL 0 OR home IN_LIST qiven_homes)
      string(SUBSTRING "${path}" ${at} -1 rest)
      set(${out} "${rest}" PARENT_SCOPE)
      return()
    endif()
    math(EXPR from "${at} + 1")
  endwhile()
endfunction()

# qiven_source_path(<dir> <source> <out>): a source of a target defined in
# dir, from the root, as qiven_analysed reads it: a generated one as its
# directory's own.
function(qiven_source_path dir source out)
  set(${out} "" PARENT_SCOPE)
  get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${dir}")
  qiven_under("${source}" "${PROJECT_BINARY_DIR}" generated)
  if(generated)
    get_filename_component(name "${source}" NAME)
    set(source "${dir}/${name}")
  endif()
  file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}" "${source}")
  set(${out} "${rel}" PARENT_SCOPE)
endfunction()

# qiven_tidy_dir(<source> <out>): the checkout's directory whose .clang-tidy
# files hold for source: its own, for one generated, its directory in the build.
function(qiven_tidy_dir source out)
  get_filename_component(dir "${source}" DIRECTORY)
  qiven_under("${source}" "${PROJECT_BINARY_DIR}" generated)
  if(generated)
    file(RELATIVE_PATH in_build "${PROJECT_BINARY_DIR}" "${dir}")
    set(dir "${PROJECT_SOURCE_DIR}/${in_build}")
  endif()
  set(${out} "${dir}" PARENT_SCOPE)
endfunction()

# qiven_test_target(<dir> <target> <out>): TRUE where target, defined in
# dir, has own sources, read by qiven_source_path, and each is test code.
# A header's unit (header_units.cmake's qiven_header_targets) reads as the header
# it stands for, its source property QIVEN_HEADER.
function(qiven_test_target dir target out)
  set(${out} FALSE PARENT_SCOPE)
  set(own FALSE)
  get_target_property(sources ${target} SOURCES)
  foreach(source IN LISTS sources)
    qiven_source_path("${dir}" "${source}" rel)
    if(NOT rel STREQUAL "")
      get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${dir}")
      get_source_file_property(header "${path}" DIRECTORY "${dir}" QIVEN_HEADER)
      if(header)
        set(rel "${header}")
      endif()
    endif()
    qiven_test_path("${rel}" test)
    if(NOT rel STREQUAL "" AND test STREQUAL "")
      return()
    elseif(NOT rel STREQUAL "")
      set(own TRUE)
    endif()
  endforeach()
  set(${out} ${own} PARENT_SCOPE)
endfunction()

# qiven_test_targets(<out>): the project's compiled targets, a plant's
# (QIVEN_PLANT) aside, that take test code's analysis: qiven_test_target's,
# with a source the analysis reads (qiven_analysed).
function(qiven_test_targets out)
  set(found "")
  set(dirs "${PROJECT_SOURCE_DIR}")
  while(dirs)
    list(POP_FRONT dirs dir)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    list(APPEND dirs ${subdirs})
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      get_target_property(plant ${target} QIVEN_PLANT)
      if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$" AND NOT plant)
        qiven_test_target("${dir}" ${target} test)
        get_target_property(sources ${target} SOURCES)
        foreach(source IN LISTS sources)
          qiven_source_path("${dir}" "${source}" rel)
          qiven_analysed("${rel}" read)
          if(test AND read AND rel MATCHES "${qiven_source_suffix}")
            list(APPEND found ${target})
            break()
          endif()
        endforeach()
      endif()
    endforeach()
  endwhile()
  set(${out} ${found} PARENT_SCOPE)
endfunction()

# qiven_analysed(<path> <out>): TRUE where the analysis reads the file at
# path, from the root: under an analysed root, as it stands or as the
# top-level tree would hold it, or under a layer's tree and no test code, a
# module's own tests, which an analysed root names where the analysis reads
# them; never a vendored file (qiven_vendored).
function(qiven_analysed path out)
  set(${out} FALSE PARENT_SCOPE)
  if(path IN_LIST qiven_vendored)
    return()
  endif()
  qiven_test_path("${path}" test)
  foreach(root IN LISTS qiven_analysed)
    qiven_under("${path}" "${root}" under)
    qiven_under("${test}" "${root}" as_test)
    if(under OR as_test)
      set(${out} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(layer IN LISTS qiven_layers)
    qiven_under("${path}" "${layer}" under)
    if(under AND test STREQUAL "")
      set(${out} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()
endfunction()

# qiven_path_regex(<path> <out>): a regular expression matching path, its
# separators either / or \, a drive letter in either case.
function(qiven_path_regex path out)
  string(REGEX REPLACE "([][.^$|()*+?{}\\\\])" "\\\\\\1" regex "${path}")
  string(REPLACE "/" "[/\\]" regex "${regex}")
  if(regex MATCHES "^([A-Za-z]):")
    string(TOUPPER "${CMAKE_MATCH_1}" upper)
    string(TOLOWER "${CMAKE_MATCH_1}" lower)
    string(REGEX REPLACE "^[A-Za-z]:" "[${upper}${lower}]:" regex "${regex}")
  endif()
  set(${out} "${regex}" PARENT_SCOPE)
endfunction()

# qiven_header_filters(<root> <headers> <filter> <exclude>): clang-tidy's
# header filters for the checkout at root: filter, the headers under the
# analysed roots and the layers' trees; exclude, the directories of those
# headers, from the root, that the analysis does not read (empty where none).
function(qiven_header_filters root headers filter exclude)
  qiven_path_regex("${root}" root_regex)
  set(trees "")
  foreach(tree IN LISTS qiven_analysed qiven_layers)
    qiven_path_regex("${tree}" tree_regex)
    list(APPEND trees "${tree_regex}")
  endforeach()
  list(JOIN trees "|" trees)
  set(${filter} "${root_regex}[/\\](${trees})[/\\]" PARENT_SCOPE)
  set(dirs "")
  foreach(header IN LISTS headers)
    qiven_analysed("${header}" read)
    if(NOT read)
      get_filename_component(dir "${header}" DIRECTORY)
      qiven_path_regex("${dir}" dir_regex)
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

# qiven_table_globs(<root> <out>): the globs of the table's files under the
# layers' trees, each module.json at any depth.
function(qiven_table_globs root out)
  set(globs "")
  foreach(layer IN LISTS qiven_layers)
    list(APPEND globs "${root}/${layer}/module.json")
  endforeach()
  set(${out} ${globs} PARENT_SCOPE)
endfunction()

# qiven_python_globs(<root> <out>): the globs of the Python files under the
# walked trees and the layers', where the listing finds the Python tests.
function(qiven_python_globs root out)
  set(globs "")
  foreach(tree IN LISTS qiven_walked qiven_layers)
    list(APPEND globs "${root}/${tree}/*.py")
  endforeach()
  set(${out} ${globs} PARENT_SCOPE)
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/header_units.cmake")  # the header units

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  qiven_trees("${ROOT}/tools/trees.json")
  if(SHOW STREQUAL "filters")
    qiven_header_globs("${ROOT}" globs ${qiven_layers} ${qiven_analysed})  # the root CMakeLists.txt's
    file(GLOB_RECURSE headers RELATIVE "${ROOT}" ${globs})
    qiven_header_filters("${ROOT}" "${headers}" filter exclude)
    message("filter=${filter}")
    message("exclude=${exclude}")
  elseif(SHOW STREQUAL "analysed")  # for the lines of PATHFILE
    file(STRINGS "${PATHFILE}" paths)
    foreach(path IN LISTS paths)
      qiven_analysed("${path}" read)
      qiven_test_path("${path}" test)
      message("${path}=${read} ${test}")
    endforeach()
  elseif(SHOW STREQUAL "globs")
    qiven_table_globs("${ROOT}" table)
    qiven_python_globs("${ROOT}" python)
    message("table=${table}")
    message("python=${python}")
  elseif(SHOW STREQUAL "units")
    qiven_header_units("${ROOT}" units)
    message("units=${units}")
  elseif(SHOW STREQUAL "through")  # header=source each header compiles through
    qiven_header_units("${ROOT}" units)
    foreach(pair IN LISTS units_of)
      message("${pair}")
    endforeach()
  endif()
endif()
