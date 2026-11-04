cmake_minimum_required(VERSION 3.28)
project(qiven3 LANGUAGES C CXX)

include("${PROJECT_SOURCE_DIR}/foundation/devkit/build/build.cmake" NO_POLICY_SCOPE)

# The trees, read from tools/trees.json, the one place that names them
# (tools/loop_modules.py reads it too): its layers, walked and analysed, and
# the rules the globs and filters take from them (foundation/devkit/build/trees.cmake).
include("${PROJECT_SOURCE_DIR}/foundation/devkit/build/trees.cmake")
include("${PROJECT_SOURCE_DIR}/foundation/devkit/build/tidy_config.cmake")  # a test source's composed configuration
set(qiven3_trees_file "${PROJECT_SOURCE_DIR}/tools/trees.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${qiven3_trees_file}")
qiven3_trees("${qiven3_trees_file}")
set(qiven3_roots ${qiven3_layers} ${qiven3_analysed})

# The build's targets and their links, for the modules' check
# (tools/loop_modules.py, against the table the module.json files give).
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

enable_testing()
add_subdirectory(foundation/system/sqlite)
add_subdirectory(foundation/core/memory)
add_subdirectory(foundation/core/matcher)
add_subdirectory(foundation/system/diag)
add_subdirectory(foundation/core/text)
add_subdirectory(foundation/core/time)
add_subdirectory(foundation/core/digest)
add_subdirectory(src/qiven3)
add_subdirectory(src/entry)
add_subdirectory(src/log)
add_subdirectory(foundation/system/platform)
add_subdirectory(products/qiven3/format)
add_subdirectory(src/wire)
add_subdirectory(foundation/services/storage/sqlite)
add_subdirectory(foundation/services/storage/recordlog)
add_subdirectory(src/checked)
add_subdirectory(src/write)
add_subdirectory(src/app)
# QIVEN_HAL off leaves compute, its fake and its cases out of the build (H6).
if(QIVEN_HAL)
  add_subdirectory(foundation/services/compute)
endif()
add_subdirectory(tests)

# binaries.txt: each binary qiven_executable made, one file a line; and
# executables.txt, every executable target of CMake's own records, each
# directory's, which the entry's case reads against it.
get_property(qiven3_binaries GLOBAL PROPERTY QIVEN_BINARIES)
set(qiven3_binaries_text "")
foreach(binary IN LISTS qiven3_binaries)
  string(APPEND qiven3_binaries_text "$<TARGET_FILE:${binary}>\n")
endforeach()
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/binaries.txt" CONTENT "${qiven3_binaries_text}")
set(qiven3_executables_text "")
set(qiven3_defined "")
set(qiven3_dirs "${PROJECT_SOURCE_DIR}")
while(qiven3_dirs)
  list(POP_FRONT qiven3_dirs dir)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  list(APPEND qiven3_dirs ${subdirs})
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
      string(APPEND qiven3_executables_text "${target}\t$<TARGET_FILE:${target}>\n")
    endif()
    get_target_property(plant ${target} QIVEN3_PLANT)
    if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$" AND NOT plant)
      list(APPEND qiven3_defined ${target})
    endif()
  endforeach()
endwhile()
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/executables.txt" CONTENT "${qiven3_executables_text}")

# The targets that take test code's analysis, in every preset's
# test-targets.txt, and beside it defined-targets.txt, every compiled target
# the preset defines, a plant's aside: foundation/devkit/structure/tests/test_exemptions.py
# holds the first to its list, and tells a listed target defined without
# test code's analysis from one the preset does not define. Which preset,
# preset.txt says, from the base preset's QIVEN3_PRESET, each preset's own
# name: a tree's directory may be any, CI's shared one among them. A configure
# with no preset records none.
qiven3_test_targets(qiven3_test_targets)
list(JOIN qiven3_test_targets "\n" qiven3_text)
file(WRITE "${PROJECT_BINARY_DIR}/test-targets.txt" "${qiven3_text}\n")
list(JOIN qiven3_defined "\n" qiven3_text)
file(WRITE "${PROJECT_BINARY_DIR}/defined-targets.txt" "${qiven3_text}\n")
if(DEFINED QIVEN3_PRESET)
  file(WRITE "${PROJECT_BINARY_DIR}/preset.txt" "${QIVEN3_PRESET}\n")
else()
  file(REMOVE "${PROJECT_BINARY_DIR}/preset.txt")
endif()

# The analysis preset over the sources the analysis reads, by their paths
# (qiven3_analysed): those under the analysed roots (tests/child/ holds
# qiven3-child, the tests' children, and a module's own tests/child/ its
# children, as the platform's) and those under the layers'
# trees outside a module's own tests/, whatever directory defines their
# target. /analyze on each such source, then clang-tidy over
# compile_commands.json, each source its own step after every target is
# built, by .clang-tidy, on the headers of the same trees. A header with no
# source of its own is read through its unit in tests/support/headers/.
if(QIVEN_ANALYZE STREQUAL "analysis")
  set(qiven3_tidy_targets "")
  set(qiven3_tidy_sources "")
  set(qiven3_test_sources "")
  set(qiven3_optioned "")
  set(qiven3_dirs "${PROJECT_SOURCE_DIR}")
  while(qiven3_dirs)
    list(POP_FRONT qiven3_dirs dir)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    list(APPEND qiven3_dirs ${subdirs})
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      # A plant that must not compile (QIVEN3_PLANT) is built by its test alone.
      get_target_property(plant ${target} QIVEN3_PLANT)
      if(type STREQUAL "INTERFACE_LIBRARY" OR plant)
        continue()
      endif()
      list(APPEND qiven3_tidy_targets ${target})
      # A target whose own sources are all test code (qiven3_test_target)
      # takes test code's rule set and, each source's step, its clang-tidy
      # configuration; test-targets.txt lists those targets (qiven3_test_targets).
      qiven3_test_target("${dir}" ${target} test_target)
      set(options ${qiven_analysis_options})
      if(test_target)
        set(options ${qiven_analysis_test_options})
      endif()
      get_target_property(sources ${target} SOURCES)
      foreach(source IN LISTS sources)
        qiven3_source_path("${dir}" "${source}" rel)
        get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${dir}")
        qiven3_analysed("${rel}" read)
        if(read AND source MATCHES "${qiven3_source_suffix}")
          if(NOT "${dir}|${source}" IN_LIST qiven3_optioned)  # once a directory
            list(APPEND qiven3_optioned "${dir}|${source}")
            set_property(SOURCE "${source}" TARGET_DIRECTORY ${target} APPEND PROPERTY COMPILE_OPTIONS
              ${options})
            if(test_target)
              list(APPEND qiven3_test_sources "${source}")
            endif()
          endif()
          list(APPEND qiven3_tidy_sources "${source}")
        endif()
      endforeach()
    endforeach()
  endwhile()
  list(REMOVE_DUPLICATES qiven3_tidy_sources)
  # The headers under the analysed roots, and each .clang-tidy under the
  # walked trees at any depth, a new one reconfiguring; a source's step
  # depends on the configs of its directory and of each directory above it.
  qiven3_header_globs("${PROJECT_SOURCE_DIR}" qiven3_tidy_globs ${qiven3_roots})
  file(GLOB_RECURSE qiven3_tidy_inputs CONFIGURE_DEPENDS ${qiven3_tidy_globs})
  # clang-tidy's header filters, from the same trees and rule, under this
  # checkout alone: .clang-tidy names none.
  set(qiven3_headers "")
  foreach(header IN LISTS qiven3_tidy_inputs)
    file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}" "${header}")
    list(APPEND qiven3_headers "${rel}")
  endforeach()
  qiven3_header_filters("${PROJECT_SOURCE_DIR}" "${qiven3_headers}" qiven3_filter qiven3_exclude)
  set(qiven3_filter_args "--header-filter=${qiven3_filter}")
  if(qiven3_exclude)
    list(APPEND qiven3_filter_args "--exclude-header-filter=${qiven3_exclude}")
  endif()
  set(qiven3_tidy_globs "")
  foreach(tree IN LISTS qiven3_walked qiven3_layers)
    list(APPEND qiven3_tidy_globs "${PROJECT_SOURCE_DIR}/${tree}/*.clang-tidy")
  endforeach()
  file(GLOB_RECURSE qiven3_tidy_configs CONFIGURE_DEPENDS ${qiven3_tidy_globs})
  file(GLOB qiven3_tidy_root CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/*.clang-tidy")
  list(APPEND qiven3_tidy_configs ${qiven3_tidy_root})
  list(FILTER qiven3_tidy_configs INCLUDE REGEX "/[.]clang-tidy$")
  # The test sources' configurations are composed from them at configure.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${qiven3_tidy_configs} "${qiven_tidy_test_config}")
  # clang-tidy's copy of the database, without the options clang-cl fails or
  # warns on, written only when it changes (foundation/devkit/build/tidy_database.cmake).
  set(qiven3_tidy_database "${PROJECT_SOURCE_DIR}/foundation/devkit/build/tidy_database.cmake")
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/tidy/compile_commands.json"
    COMMAND "${CMAKE_COMMAND}" "-DBUILD=${PROJECT_BINARY_DIR}" -P "${qiven3_tidy_database}"
    DEPENDS "${PROJECT_BINARY_DIR}/compile_commands.json" "${qiven3_tidy_database}"
    COMMENT "clang-tidy's database" VERBATIM)
  set(qiven3_stamps "")
  foreach(source IN LISTS qiven3_tidy_sources)
    file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}" "${source}")
    string(MAKE_C_IDENTIFIER "${rel}" stamp)
    set(stamp "${PROJECT_BINARY_DIR}/tidy/${stamp}.stamp")
    # A generated source takes its directory's configs, given by name: the
    # search up from the build tree finds none when the tree sits outside.
    qiven3_tidy_dir("${source}" source_dir)
    qiven3_under("${source}" "${PROJECT_BINARY_DIR}" generated)
    set(configs "")
    set(nearest "")
    foreach(config IN LISTS qiven3_tidy_configs)
      get_filename_component(config_dir "${config}" DIRECTORY)
      string(FIND "${source_dir}/" "${config_dir}/" at)
      if(at EQUAL 0)
        list(APPEND configs "${config}")
        string(LENGTH "${nearest}" nearest_length)
        string(LENGTH "${config}" config_length)
        if(config_length GREATER nearest_length)
          set(nearest "${config}")
        endif()
      endif()
    endforeach()
    set(config_arg "")
    if(generated AND nearest)
      set(config_arg "--config-file=${nearest}")
    endif()
    if("${source}" IN_LIST qiven3_test_sources)
      # Its directory's configs with the test configuration over them, composed
      # at configure, named by path, in tree and out of tree alike.
      qiven3_tidy_test_config("${source}" "${qiven_tidy_test_config}" "${QIVEN_CLANG_TIDY}" composed)
      set(config_arg "--config-file=${composed}")
      list(APPEND configs "${qiven_tidy_test_config}" "${composed}")
    endif()
    add_custom_command(OUTPUT "${stamp}"
      COMMAND "${QIVEN_CLANG_TIDY}" -p "${PROJECT_BINARY_DIR}/tidy" ${config_arg} ${qiven3_filter_args} --quiet
        "${source}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
      DEPENDS "${source}" "${PROJECT_BINARY_DIR}/tidy/compile_commands.json" ${qiven3_tidy_inputs} ${configs}
      COMMENT "clang-tidy ${rel}" VERBATIM)
    list(APPEND qiven3_stamps "${stamp}")
  endforeach()
  add_custom_target(qiven3_tidy DEPENDS ${qiven3_stamps})
  add_dependencies(qiven3_tidy ${qiven3_tidy_targets})
  list(LENGTH qiven3_stamps qiven3_count)
  list(LENGTH qiven3_tidy_configs qiven3_config_count)
  message(STATUS "qiven3: clang-tidy over ${qiven3_count} sources, ${qiven3_config_count} .clang-tidy files, target "
    "qiven3_tidy")
endif()
