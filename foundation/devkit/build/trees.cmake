# The trees of tools/trees.json, the one place that names them (tools/loop_modules.py
# reads it too), and the rules the build's globs, filters and header units take
# from them.
# Included by the root CMakeLists.txt; run by tests/drive/test_trees_build.py as
# cmake -P with ROOT (the checkout) and SHOW (what to print).

# The structure checks' C++ lexer, which reads the header names a unit includes.
get_filename_component(qiven3_lexer "${CMAKE_CURRENT_LIST_DIR}/../../../tests/structure/lexer.py" ABSOLUTE)

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
# dir, has own sources, read by qiven3_source_path, and each is test code,
# but qiven3_headers: its units sit in the tests' tree and stand for every
# library's headers.
function(qiven3_test_target dir target out)
  set(${out} FALSE PARENT_SCOPE)
  if(target STREQUAL "qiven3_headers")
    return()
  endif()
  set(own FALSE)
  get_target_property(sources ${target} SOURCES)
  foreach(source IN LISTS sources)
    qiven3_source_path("${dir}" "${source}" rel)
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

# qiven3_header_unit(<root> <header> <unit> <own>): the unit in
# tests/support/headers/ of header, from the root, and the path of the source
# that is its own besides <name> under a source suffix beside it (empty where
# none). Under a layer's tree, the name of its module (the nearest directory
# above it with a module.json) and _, then its path under the module,
# include/qiven/<m>/ cut (its own source the module's src/<p> under a source
# suffix); under an analysed root outside the layers, the root's last name
# and _ where the root is no walked tree, then its path under the root; / as
# _, .cpp for its suffix, as every unit is named.
function(qiven3_header_unit root header unit own)
  set(${own} "" PARENT_SCOPE)
  set(name "")
  set(found FALSE)
  foreach(layer IN LISTS qiven3_layers)
    qiven3_under("${header}" "${layer}" under)
    if(NOT under)
      continue()
    endif()
    get_filename_component(module "${header}" DIRECTORY)
    while(NOT EXISTS "${root}/${module}/module.json")
      if(module STREQUAL layer)
        message(FATAL_ERROR "qiven3: the header ${header} lies in no module")
      endif()
      get_filename_component(module "${module}" DIRECTORY)
    endwhile()
    file(READ "${root}/${module}/module.json" text)
    string(JSON name GET "${text}" name)
    string(LENGTH "${module}/" at)
    string(SUBSTRING "${header}" ${at} -1 inner)
    if(inner MATCHES "^include/qiven/[^/]+/(.+)${qiven3_header_suffix}")
      qiven3_own_source("${root}" "${module}/src/${CMAKE_MATCH_1}" source)
      set(${own} "${source}" PARENT_SCOPE)
      set(inner "${CMAKE_MATCH_1}.cpp")
    endif()
    set(name "${name}_${inner}")
    set(found TRUE)
    break()
  endforeach()
  if(NOT found)
    foreach(tree IN LISTS qiven3_analysed)
      qiven3_under("${header}" "${tree}" under)
      if(under)
        if(NOT tree IN_LIST qiven3_walked)
          get_filename_component(name "${tree}" NAME)
          string(APPEND name "_")
        endif()
        string(LENGTH "${tree}/" at)
        string(SUBSTRING "${header}" ${at} -1 inner)
        string(APPEND name "${inner}")
        break()
      endif()
    endforeach()
  endif()
  string(REGEX REPLACE "${qiven3_header_suffix}" ".cpp" name "${name}")
  string(REPLACE "/" "_" name "${name}")
  set(${unit} "${name}" PARENT_SCOPE)
endfunction()

# qiven3_includes(<root> <sources> <out>): <source>|<file> for each file,
# from the root, that a source, from the root, includes. The structure
# checks' C++ lexer reads the header names of its #include lines; each
# resolves as the build's include roots give it: a quoted name from the
# source's directory first, then from the trees' included roots, then from
# the include/ of each module under the layers.
function(qiven3_includes root sources out)
  set(${out} "" PARENT_SCOPE)
  list(REMOVE_ITEM sources "")
  if(NOT sources)
    return()
  elseif(NOT Python3_EXECUTABLE)
    message(FATAL_ERROR "qiven3: no interpreter (Python3_EXECUTABLE) to read the includes of the header units")
  endif()
  if(NOT CMAKE_SCRIPT_MODE_FILE)  # the sources and the lexer that reads them
    list(TRANSFORM sources PREPEND "${root}/" OUTPUT_VARIABLE read)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${read} "${qiven3_lexer}")
  endif()
  execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${qiven3_lexer}" ${sources} WORKING_DIRECTORY "${root}"
    OUTPUT_VARIABLE found RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "qiven3: the lexer could not read the header units' includes: ${status}")
  endif()
  qiven3_table_globs("${root}" tables)
  file(GLOB_RECURSE tables RELATIVE "${root}" ${tables})
  set(roots ${qiven3_included})
  foreach(table IN LISTS tables)
    get_filename_component(dir "${table}" DIRECTORY)
    if(IS_DIRECTORY "${root}/${dir}/include")
      list(APPEND roots "${dir}/include")
    endif()
  endforeach()
  string(REPLACE "\n" ";" found "${found}")
  set(included "")
  foreach(line IN LISTS found)
    if(NOT line MATCHES "^([^\t]+)\t([\"<])(.+)$")
      continue()
    endif()
    set(source "${CMAKE_MATCH_1}")
    set(name "${CMAKE_MATCH_3}")
    set(bases ${roots})
    if(CMAKE_MATCH_2 STREQUAL "\"")
      get_filename_component(dir "${source}" DIRECTORY)
      list(PREPEND bases "${dir}")
    endif()
    foreach(base IN LISTS bases)
      cmake_path(SET path NORMALIZE "${base}/${name}")
      if(EXISTS "${root}/${path}" AND NOT IS_DIRECTORY "${root}/${path}")
        list(APPEND included "${source}|${path}")
        break()
      endif()
    endforeach()
  endforeach()
  set(${out} ${included} PARENT_SCOPE)
endfunction()

# qiven3_any_case(<word> <out>): a regular expression matching word in either
# case, as CMake reads a command's name.
function(qiven3_any_case word out)
  set(pattern "")
  string(LENGTH "${word}" length)
  math(EXPR last "${length} - 1")
  foreach(at RANGE ${last})
    string(SUBSTRING "${word}" ${at} 1 char)
    string(TOUPPER "${char}" upper)
    string(TOLOWER "${char}" lower)
    if(upper STREQUAL lower)
      string(APPEND pattern "${char}")
    else()
      string(APPEND pattern "[${upper}${lower}]")
    endif()
  endforeach()
  set(${out} "${pattern}" PARENT_SCOPE)
endfunction()

# qiven3_listed(<root> <source> <out>): TRUE where the CMakeLists.txt nearest
# above source, from the root, names it as a path from that file's directory,
# alone or after a variable's directory (}/), once its comments and its source
# properties are stripped: each set_source_files_properties(...) and each
# set_property(SOURCE ...), the command in either case. That is a source a
# target's list holds, on whichever system its branch is taken, which this
# configure's targets alone cannot tell. Else FALSE, with <out>_in the list
# read, from the root ("" where no CMakeLists.txt is above it), and <out>_as
# the name looked for.
function(qiven3_listed root source out)
  set(${out} FALSE PARENT_SCOPE)
  set(${out}_in "" PARENT_SCOPE)
  set(${out}_as "" PARENT_SCOPE)
  get_filename_component(dir "${source}" DIRECTORY)
  while(NOT EXISTS "${root}/${dir}/CMakeLists.txt")
    if(dir STREQUAL "")
      return()
    endif()
    get_filename_component(dir "${dir}" DIRECTORY)
  endwhile()
  set(file "${root}/${dir}/CMakeLists.txt")
  if(NOT CMAKE_SCRIPT_MODE_FILE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  endif()
  file(READ "${file}" text)
  string(REGEX REPLACE "#[^\n]*" "" text "${text}")
  qiven3_any_case("set_source_files_properties" properties)
  qiven3_any_case("set_property" property)
  qiven3_any_case("SOURCE" keyword)
  string(REGEX REPLACE "${properties}[^)]*[)]" "" text "${text}")
  string(REGEX REPLACE "${property}[ \t\r\n]*[(][ \t\r\n]*${keyword}[ \t\r\n][^)]*[)]" "" text "${text}")
  set(name "${source}")
  if(NOT dir STREQUAL "")
    string(LENGTH "${dir}/" at)
    string(SUBSTRING "${source}" ${at} -1 name)
  endif()
  string(REGEX REPLACE "^/" "" list "${dir}/CMakeLists.txt")
  set(${out}_in "${list}" PARENT_SCOPE)
  set(${out}_as "${name}" PARENT_SCOPE)
  string(REGEX REPLACE "([][.^$|()*+?{}\\\\])" "\\\\\\1" name "${name}")
  if("\n${text}\n" MATCHES "([ \t\r\n\"(]|[}]/)${name}[ \t\r\n\")]")
    set(${out} TRUE PARENT_SCOPE)
  endif()
endfunction()

# qiven3_header_units(<root> <out>): the units of tests/support/headers/ of
# the checkout at root, one for each header of a suffix of
# qiven3_header_suffixes the analysis reads (under an analysed root or a
# layer's tree) that has no source of its own, so that the analysis reads it
# whatever includes it: an own source stands in for a unit where its target
# compiles it (qiven3_listed) and it includes its header (qiven3_includes).
# An error names each such header with no unit, two headers of one unit, a
# unit no such header wants, and a unit that does not include its header.
# <out>_of: header=unit for each such header, unit the source, from the root,
# it compiles through (the listing of tools/loop_listing.cmake reads it).
# Every configure with an interpreter runs it (tests/CMakeLists.txt).
function(qiven3_header_units root out)
  set(depends "")
  if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(depends CONFIGURE_DEPENDS)
  endif()
  file(GLOB units RELATIVE "${root}/tests/support/headers" ${depends} "${root}/tests/support/headers/*.cpp")
  qiven3_header_globs("${root}" globs ${qiven3_analysed} ${qiven3_layers})
  file(GLOB_RECURSE headers RELATIVE "${root}" ${depends} ${globs})
  list(REMOVE_DUPLICATES headers)
  list(SORT headers)
  set(reads "")
  set(owns "")
  foreach(header IN LISTS headers)
    qiven3_analysed("${header}" read)
    if(NOT read)
      continue()
    endif()
    string(REGEX REPLACE "${qiven3_header_suffix}" "" stem "${header}")
    qiven3_own_source("${root}" "${stem}" beside)
    if(NOT beside)
      qiven3_header_unit("${root}" "${header}" unit beside)
    endif()
    if(NOT beside)
      set(beside "-")
    else()
      qiven3_listed("${root}" "${beside}" listed)
      if(NOT listed)  # an own source its list does not name stands in for no unit
        set(beside "?${beside}|${listed_in}|${listed_as}")
      endif()
    endif()
    list(APPEND reads "${header}")
    list(APPEND owns "${beside}")
  endforeach()
  set(sources ${owns})
  list(FILTER sources EXCLUDE REGEX "^[-?]")
  list(TRANSFORM units PREPEND "tests/support/headers/" OUTPUT_VARIABLE paths)
  qiven3_includes("${root}" "${sources};${paths}" included)
  set(wanted "")
  set(owners "")
  set(through "")
  foreach(header own IN ZIP_LISTS reads owns)
    if("${own}|${header}" IN_LIST included)
      list(APPEND through "${header}=${own}")
      continue()
    endif()
    set(not_own "")
    if(own MATCHES "^[?]([^|]+)[|][|]$")
      set(not_own " (its own source ${CMAKE_MATCH_1} is not listed: no CMakeLists.txt is above it)")
    elseif(own MATCHES "^[?]([^|]+)[|]([^|]+)[|](.+)$")
      string(CONCAT not_own " (its own source ${CMAKE_MATCH_1} is not listed: ${CMAKE_MATCH_2} names neither "
             "${CMAKE_MATCH_3} nor \${...}/${CMAKE_MATCH_3} outside its comments and source properties)")
    elseif(NOT own STREQUAL "-")
      set(not_own " (its own source ${own} does not include it)")
    endif()
    qiven3_header_unit("${root}" "${header}" unit ignored)
    list(APPEND through "${header}=tests/support/headers/${unit}")
    list(FIND wanted "${unit}" at)
    if(at GREATER -1)
      list(GET owners ${at} other)
      message(SEND_ERROR "qiven3: the headers ${other} and ${header} share the unit tests/support/headers/${unit}")
    elseif(NOT unit IN_LIST units)
      message(SEND_ERROR
        "qiven3: the header-only header ${header} has no unit, tests/support/headers/${unit}${not_own}")
    elseif(NOT "tests/support/headers/${unit}|${header}" IN_LIST included)
      message(SEND_ERROR "qiven3: the unit tests/support/headers/${unit} does not include its header ${header}")
    endif()
    list(APPEND wanted "${unit}")
    list(APPEND owners "${header}")
  endforeach()
  foreach(unit IN LISTS units)
    if(NOT unit IN_LIST wanted)
      message(SEND_ERROR "qiven3: tests/support/headers/${unit} is no header-only header's unit")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES wanted)
  set(taken "")
  foreach(unit IN LISTS wanted)
    if(unit IN_LIST units)
      list(APPEND taken "${unit}")
    endif()
  endforeach()
  set(${out} ${taken} PARENT_SCOPE)
  set(${out}_of "${through}" PARENT_SCOPE)
endfunction()

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
  endif()
endif()
