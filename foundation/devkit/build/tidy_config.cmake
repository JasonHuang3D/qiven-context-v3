# A test source's clang-tidy configuration, composed at configure: its
# directory's configurations in the checkout with the test configuration
# over them, in one file of the build tree its step names by path. As a
# script (cmake -P), with ROOT (the checkout), BUILD (the build tree), TIDY
# (clang-tidy) and PATHFILE, it prints each source's composed file, as
# tests/drive/test_trees_build.py reads it.

# qiven3_tidy_chain(<dir> <test> <out>): the configurations clang-tidy reads
# for a source of dir under test, the first read first: dir's nearest
# .clang-tidy in the checkout and its parents' while each inherits
# (InheritParentConfig), the root's first, then test.
function(qiven3_tidy_chain dir test out)
  set(chain "${test}")
  set(file "${test}")
  set(done FALSE)
  while(file)
    file(STRINGS "${file}" inherit REGEX "^InheritParentConfig:[ \t]*true[ \t]*(#.*)?$")
    set(file "")
    while(inherit AND NOT file AND NOT done)
      if(EXISTS "${dir}/.clang-tidy")
        set(file "${dir}/.clang-tidy")
        list(PREPEND chain "${file}")
      endif()
      get_filename_component(parent "${dir}" DIRECTORY)
      if(dir STREQUAL PROJECT_SOURCE_DIR OR parent STREQUAL dir)
        set(done TRUE)
      endif()
      set(dir "${parent}")
    endwhile()
  endwhile()
  set(${out} "${chain}" PARENT_SCOPE)
endfunction()

# qiven3_tidy_options(<dump> <out> <config>...): dump's text, a dumped
# configuration, with its CheckOptions block holding every CheckOptions
# entry of each config too, in the map form or the list of key and value,
# a later entry winning on a key, as clang-tidy merges them: the dump holds
# only the options its enabled checks store, never the analyzer's
# (clang-analyzer-<checker>:<option>). A value is kept as written; a form
# neither reads (a flow, a multi-line value) is an error, not an entry lost.
function(qiven3_tidy_options dump out)
  set(keys "")
  set(before "")
  set(after "")
  foreach(config IN ITEMS "${dump}" ${ARGN})
    file(READ "${config}" text)
    string(APPEND text "\n")
    set(in FALSE)
    set(key "")
    set(kept "")
    while(NOT text STREQUAL "")
      string(FIND "${text}" "\n" at)
      string(SUBSTRING "${text}" 0 ${at} line)
      math(EXPR at "${at} + 1")
      string(SUBSTRING "${text}" ${at} -1 text)
      string(REGEX REPLACE "\r$" "" line "${line}")
      set(value "")
      if(line MATCHES "^CheckOptions:[ \t]*(#.*)?$")
        set(in TRUE)
        if(config STREQUAL dump)
          set(before "${kept}")
          set(kept "")
        endif()
        continue()
      elseif(in AND line MATCHES "^[ \t]*(#.*)?$")
        continue()
      elseif(in AND line MATCHES "^[ \t]+-[ \t]+key:[ \t]+([^ \t'\"]+)[ \t]*$")
        set(key "${CMAKE_MATCH_1}")
      elseif(in AND key AND line MATCHES "^[ \t]+value:[ \t]+(.*)$")
        set(value "${CMAKE_MATCH_1}")
      elseif(in AND line MATCHES "^[ \t]+([^- \t#'\"{[][^ \t]*):[ \t]+(.*)$")
        set(key "${CMAKE_MATCH_1}")
        set(value "${CMAKE_MATCH_2}")
      elseif(line MATCHES "^CheckOptions:" OR (in AND line MATCHES "^[ \t]"))
        message(FATAL_ERROR "qiven3: ${config}: a CheckOptions line not read: ${line}")
      else()
        set(in FALSE)
        string(APPEND kept "${line}\n")
      endif()
      if(NOT value STREQUAL "")
        if(NOT value MATCHES "^('.*'|\".*\"|[^'\"|>{[&*!%@`].*)$")
          message(FATAL_ERROR "qiven3: ${config}: a CheckOptions value not read: ${line}")
        endif()
        list(FIND keys "${key}" i)
        if(i EQUAL -1)
          list(LENGTH keys i)
          list(APPEND keys "${key}")
        endif()
        set("value_${i}" "${value}")
        set(key "")
      endif()
    endwhile()
    if(config STREQUAL dump)
      set(after "${kept}")
    endif()
  endforeach()
  if(before STREQUAL "")
    message(FATAL_ERROR "qiven3: ${dump} holds no CheckOptions")
  endif()
  set(block "CheckOptions:\n")
  set(i 0)
  foreach(key IN LISTS keys)
    string(APPEND block "  ${key}: ${value_${i}}\n")
    math(EXPR i "${i} + 1")
  endforeach()
  string(REGEX REPLACE "\n$" "" after "${after}")
  set(${out} "${before}${block}${after}" PARENT_SCOPE)
endfunction()

# qiven3_tidy_test_config(<source> <test> <tidy> <out>): the file a test source's
# clang-tidy step names, in out: its directory's configurations (qiven3_tidy_dir,
# qiven3_tidy_chain) with test's lines over it, as tidy dumps them, their every
# CheckOptions entry kept (qiven3_tidy_options), in the build tree's
# tidy/config/, rewritten only when it changes. Named by path, it rests on no
# search up from the source's path, which finds none for a source generated in a
# build tree outside the checkout.
function(qiven3_tidy_test_config source test tidy out)
  qiven3_tidy_dir("${source}" dir)
  get_filename_component(name "${source}" NAME)
  execute_process(COMMAND "${tidy}" "--config-file=${test}" --dump-config "${dir}/${name}" --
    OUTPUT_VARIABLE text ERROR_VARIABLE errors RESULT_VARIABLE failed)
  if(failed OR NOT text MATCHES "\nChecks:")
    message(FATAL_ERROR "qiven3: ${tidy} gives no configuration for ${dir}/${name}: ${errors}")
  endif()
  file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}" "${dir}")
  string(MAKE_C_IDENTIFIER "${rel}" id)
  set(config "${PROJECT_BINARY_DIR}/tidy/config/test_${id}.clang-tidy")
  file(WRITE "${config}.dump" "${text}")
  qiven3_tidy_chain("${dir}" "${test}" chain)
  qiven3_tidy_options("${config}.dump" text ${chain})
  file(WRITE "${config}.composed" "${text}")
  file(COPY_FILE "${config}.composed" "${config}" ONLY_IF_DIFFERENT)
  set(${out} "${config}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  include("${CMAKE_CURRENT_LIST_DIR}/trees.cmake")
  set(PROJECT_SOURCE_DIR "${ROOT}")
  set(PROJECT_BINARY_DIR "${BUILD}")
  file(STRINGS "${PATHFILE}" paths)
  foreach(path IN LISTS paths)
    qiven3_tidy_test_config("${path}" "${ROOT}/foundation/devkit/build/analysis-test.clang-tidy" "${TIDY}" config)
    message("${path}=${config}")
  endforeach()
endif()
