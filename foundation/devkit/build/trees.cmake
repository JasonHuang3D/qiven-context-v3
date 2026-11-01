# The trees of tools/trees.json, the one place that names them (tools/loop_modules.py
# reads it too), and the rules the build's globs and filters take from them.
# Included by the root CMakeLists.txt; run by tests/drive/test_trees_build.py as
# cmake -P with ROOT (the checkout) and SHOW (what to print).

# qiven3_trees(<file>): qiven3_layers, qiven3_walked and qiven3_analysed, its
# lists, in the caller's scope.
macro(qiven3_trees file)
  file(READ "${file}" qiven3_trees_text)
  foreach(qiven3_key IN ITEMS layers walked analysed)
    set(qiven3_${qiven3_key} "")
    string(JSON qiven3_count LENGTH "${qiven3_trees_text}" ${qiven3_key})
    math(EXPR qiven3_last "${qiven3_count} - 1")
    foreach(qiven3_at RANGE ${qiven3_last})
      string(JSON qiven3_tree GET "${qiven3_trees_text}" ${qiven3_key} ${qiven3_at})
      list(APPEND qiven3_${qiven3_key} "${qiven3_tree}")
    endforeach()
  endforeach()
endmacro()

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

# qiven3_analysed(<path> <out>): TRUE where the analysis reads the file at
# path, from the root: under an analysed root, or under a layer's tree and
# in no directory named tests there, a module's own tests, which an analysed
# root names where the analysis reads them.
function(qiven3_analysed path out)
  set(${out} FALSE PARENT_SCOPE)
  foreach(root IN LISTS qiven3_analysed)
    qiven3_under("${path}" "${root}" under)
    if(under)
      set(${out} TRUE PARENT_SCOPE)
      return()
    endif()
  endforeach()
  foreach(layer IN LISTS qiven3_layers)
    qiven3_under("${path}" "${layer}" under)
    if(under AND NOT "/${path}" MATCHES "/tests/")
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

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  qiven3_trees("${ROOT}/tools/trees.json")
  if(SHOW STREQUAL "filters")
    set(globs "")
    foreach(tree IN LISTS qiven3_layers qiven3_analysed)  # the root CMakeLists.txt's
      list(APPEND globs "${ROOT}/${tree}/*.hpp")
    endforeach()
    file(GLOB_RECURSE headers RELATIVE "${ROOT}" ${globs})
    qiven3_header_filters("${ROOT}" "${headers}" filter exclude)
    message("filter=${filter}")
    message("exclude=${exclude}")
  elseif(SHOW STREQUAL "analysed")
    foreach(path IN LISTS PATHS)
      qiven3_analysed("${path}" read)
      message("${path}=${read}")
    endforeach()
  elseif(SHOW STREQUAL "globs")
    qiven3_table_globs("${ROOT}" table)
    qiven3_python_globs("${ROOT}" python)
    message("table=${table}")
    message("python=${python}")
  endif()
endif()
