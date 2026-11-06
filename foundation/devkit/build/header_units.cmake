# The header units: for each header the analysis reads with no source of its
# own, the unit it compiles through, in its module's tests/support/headers/
# for a module of the foundation, else in tests/support/headers/; the rule's
# errors; and the object libraries a pair of which compiles them. Included by
# trees.cmake, whose functions and trees it reads, so whatever includes
# trees.cmake (top.cmake, trees.cmake's own SHOW=units and
# SHOW=through) has qiven_header_units.

# The structure checks' C++ lexer, which reads the header names a unit includes.
get_filename_component(qiven_lexer "${CMAKE_CURRENT_LIST_DIR}/../structure/lexer.py" ABSOLUTE)

# qiven_header_unit(<root> <header> <unit> <own>): the unit of header, both
# from the root, and the path of the source that is its own besides <name>
# under a source suffix beside it (empty where none). Under a layer's tree,
# its path under its module (the nearest directory above it with a
# module.json), include/<any>/<m>/ cut (its own source the module's src/<p>
# under a source suffix): a module of the foundation holds its units, in its
# tests/support/headers/, test code by its path; any other's (a product's,
# until its own move) lie in tests/support/headers/, the module's name and _
# before it. Under an analysed root outside the layers, in
# tests/support/headers/, the root's last name and _ where the root is no
# walked tree, then its path under the root. / as _, .cpp for its suffix, as
# every unit is named. <unit>_dir: the module's src/ where header, a private
# header, lies under it (else empty), which the unit's target is given.
function(qiven_header_unit root header unit own)
  set(${own} "" PARENT_SCOPE)
  set(${unit}_dir "" PARENT_SCOPE)
  set(name "")
  set(place "tests/support/headers")
  set(found FALSE)
  foreach(layer IN LISTS qiven_layers)
    qiven_under("${header}" "${layer}" under)
    if(NOT under)
      continue()
    endif()
    get_filename_component(module "${header}" DIRECTORY)
    while(NOT EXISTS "${root}/${module}/module.json")
      if(module STREQUAL layer)
        message(FATAL_ERROR "qiven: the header ${header} lies in no module")
      endif()
      get_filename_component(module "${module}" DIRECTORY)
    endwhile()
    if(layer STREQUAL "foundation")
      set(place "${module}/tests/support/headers")
    else()
      file(READ "${root}/${module}/module.json" text)
      string(JSON name GET "${text}" name)
      string(APPEND name "_")
    endif()
    string(LENGTH "${module}/" at)
    string(SUBSTRING "${header}" ${at} -1 inner)
    if(inner MATCHES "^include/[^/]+/[^/]+/(.+)${qiven_header_suffix}")
      qiven_own_source("${root}" "${module}/src/${CMAKE_MATCH_1}" source)
      set(${own} "${source}" PARENT_SCOPE)
      set(inner "${CMAKE_MATCH_1}.cpp")
    elseif(inner MATCHES "^src/")
      set(${unit}_dir "${module}/src" PARENT_SCOPE)
    endif()
    string(APPEND name "${inner}")
    set(found TRUE)
    break()
  endforeach()
  if(NOT found)
    foreach(tree IN LISTS qiven_analysed)
      qiven_under("${header}" "${tree}" under)
      if(under)
        if(NOT tree IN_LIST qiven_walked)
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
  string(REGEX REPLACE "${qiven_header_suffix}" ".cpp" name "${name}")
  string(REPLACE "/" "_" name "${name}")
  set(${unit} "${place}/${name}" PARENT_SCOPE)
endfunction()

# qiven_includes(<root> <sources> <units> <dirs> <out>): <source>|<file> for
# each file, from the root, that a source or a unit, from the root, includes.
# The structure checks' C++ lexer reads the header names of its #include
# lines; each resolves as the build's include roots give it: a quoted name
# from the source's directory first, then from the trees' included roots,
# then, for a unit, from dirs, the further include directories its target is
# given, then from the include/ of each module under the layers.
function(qiven_includes root sources units dirs out)
  set(${out} "" PARENT_SCOPE)
  list(APPEND sources ${units})
  list(REMOVE_ITEM sources "")
  if(NOT sources)
    return()
  elseif(NOT Python3_EXECUTABLE)
    message(FATAL_ERROR "qiven: no interpreter (Python3_EXECUTABLE) to read the includes of the header units")
  endif()
  if(NOT CMAKE_SCRIPT_MODE_FILE)  # the sources and the lexer that reads them
    list(TRANSFORM sources PREPEND "${root}/" OUTPUT_VARIABLE read)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${read} "${qiven_lexer}")
  endif()
  execute_process(COMMAND "${Python3_EXECUTABLE}" -I "${qiven_lexer}" ${sources} WORKING_DIRECTORY "${root}"
    OUTPUT_VARIABLE found RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "qiven: the lexer could not read the header units' includes: ${status}")
  endif()
  qiven_table_globs("${root}" tables)
  file(GLOB_RECURSE tables RELATIVE "${root}" ${tables})
  set(roots "")
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
    set(bases ${qiven_included})
    if(source IN_LIST units)
      list(APPEND bases ${dirs})
    endif()
    list(APPEND bases ${roots})
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

# qiven_any_case(<word> <out>): a regular expression matching word in either
# case, as CMake reads a command's name.
function(qiven_any_case word out)
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

# qiven_listed(<root> <source> <out>): TRUE where the CMakeLists.txt nearest
# above source, from the root, names it as a path from that file's directory,
# alone or after a variable's directory (}/), once its comments and its source
# properties are stripped: each set_source_files_properties(...) and each
# set_property(SOURCE ...), the command in either case. That is a source a
# target's list holds, on whichever system its branch is taken, which this
# configure's targets alone cannot tell. Else FALSE, with <out>_in the list
# read, from the root ("" where no CMakeLists.txt is above it), and <out>_as
# the name looked for.
function(qiven_listed root source out)
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
  qiven_any_case("set_source_files_properties" properties)
  qiven_any_case("set_property" property)
  qiven_any_case("SOURCE" keyword)
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

# qiven_header_units(<root> <out>): the units, from the root, in
# tests/support/headers/ and in each module directory's of the checkout at
# root, one for each header of a suffix of
# qiven_header_suffixes the analysis reads (under an analysed root or a
# layer's tree, no vendored file) that has no source of its own, so that the analysis reads it
# whatever includes it: an own source stands in for a unit where its target
# compiles it (qiven_listed) and it includes its header (qiven_includes).
# An error names each such header with no unit, two headers of one unit, a
# unit no such header wants, and a unit that does not include its header.
# <out>_of: header=unit for each such header, unit the source, from the root,
# it compiles through (trees.cmake's SHOW=through prints it). <out>_dirs:
# from the root, each module's src/ holding a private header of a unit,
# which the units' targets are given privately and by which a unit's quoted
# name resolves. Every configure with an interpreter runs it (top.cmake).
function(qiven_header_units root out)
  set(depends "")
  if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(depends CONFIGURE_DEPENDS)
  endif()
  set(places "${root}/tests/support/headers/*.cpp")
  foreach(home IN LISTS qiven_homes)
    if(NOT home STREQUAL "")
      list(APPEND places "${root}/${home}/tests/support/headers/*.cpp")
    endif()
  endforeach()
  file(GLOB units RELATIVE "${root}" ${depends} ${places})
  list(SORT units)
  qiven_header_globs("${root}" globs ${qiven_analysed} ${qiven_layers})
  file(GLOB_RECURSE headers RELATIVE "${root}" ${depends} ${globs})
  list(REMOVE_DUPLICATES headers)
  list(SORT headers)
  set(reads "")
  set(owns "")
  set(dirs "")
  foreach(header IN LISTS headers)
    qiven_analysed("${header}" read)
    if(NOT read)
      continue()
    endif()
    string(REGEX REPLACE "${qiven_header_suffix}" "" stem "${header}")
    qiven_own_source("${root}" "${stem}" beside)
    if(NOT beside)
      qiven_header_unit("${root}" "${header}" unit beside)
      list(APPEND dirs ${unit_dir})
    endif()
    if(NOT beside)
      set(beside "-")
    else()
      qiven_listed("${root}" "${beside}" listed)
      if(NOT listed)  # an own source its list does not name stands in for no unit
        set(beside "?${beside}|${listed_in}|${listed_as}")
      endif()
    endif()
    list(APPEND reads "${header}")
    list(APPEND owns "${beside}")
  endforeach()
  set(sources ${owns})
  list(FILTER sources EXCLUDE REGEX "^[-?]")
  list(REMOVE_DUPLICATES dirs)
  qiven_includes("${root}" "${sources}" "${units}" "${dirs}" included)
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
    qiven_header_unit("${root}" "${header}" unit ignored)
    list(APPEND through "${header}=${unit}")
    list(FIND wanted "${unit}" at)
    if(at GREATER -1)
      list(GET owners ${at} other)
      message(SEND_ERROR "qiven: the headers ${other} and ${header} share the unit ${unit}")
    elseif(NOT unit IN_LIST units)
      message(SEND_ERROR "qiven: the header-only header ${header} has no unit, ${unit}${not_own}")
    elseif(NOT "${unit}|${header}" IN_LIST included)
      message(SEND_ERROR "qiven: the unit ${unit} does not include its header ${header}")
    endif()
    list(APPEND wanted "${unit}")
    list(APPEND owners "${header}")
  endforeach()
  foreach(unit IN LISTS units)
    if(NOT unit IN_LIST wanted)
      message(SEND_ERROR "qiven: ${unit} is no header-only header's unit")
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
  set(${out}_dirs ${dirs} PARENT_SCOPE)
endfunction()

# qiven_header_targets(<library> <test> <place>): the analysis's object
# libraries library and test, compiled and taken by no binary, over the
# units of qiven_header_units (its qiven_header_units and
# qiven_header_units_of, top.cmake's) whose path from the root
# begins with place. A unit stands for its header (QIVEN_HEADER, read by
# qiven_test_target) and takes its analysis by it: one of test code, by the
# one reader, compiles in test, whose units are all test code, under test
# code's rule set and configuration; any other in library. The caller gives
# both their include directories and links.
function(qiven_header_targets library test place)
  set(library_units "")
  set(test_units "")
  foreach(pair IN LISTS qiven_header_units_of)
    if(NOT pair MATCHES "^([^=]+)=(.+)$")
      continue()
    endif()
    set(header "${CMAKE_MATCH_1}")
    set(unit "${CMAKE_MATCH_2}")
    string(FIND "${unit}" "${place}" at)
    if(at EQUAL 0 AND unit IN_LIST qiven_header_units)
      set(path "${qiven_root}/${unit}")
      set_source_files_properties("${path}" PROPERTIES QIVEN_HEADER "${header}")
      qiven_test_path("${header}" test_code)
      if(test_code STREQUAL "")
        list(APPEND library_units "${path}")
      else()
        list(APPEND test_units "${path}")
      endif()
    endif()
  endforeach()
  add_library(${library} OBJECT ${library_units})
  add_library(${test} OBJECT ${test_units})
  list(LENGTH library_units library_count)
  list(LENGTH test_units test_count)
  message(STATUS "qiven: header-only headers: ${library_count} in ${library}, ${test_count} in ${test}")
endfunction()
