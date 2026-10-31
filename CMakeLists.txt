cmake_minimum_required(VERSION 3.28)
project(qiven3 LANGUAGES C CXX)

include("${PROJECT_SOURCE_DIR}/foundation/devkit/build/build.cmake" NO_POLICY_SCOPE)

# The build's targets and their links, for the modules' check
# (tools/loop_modules.py, against the table the module.json files give).
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

enable_testing()
add_subdirectory(third_party/sqlite)
add_subdirectory(src/memory)
add_subdirectory(src/diag)
add_subdirectory(src/qiven3)
add_subdirectory(src/keys)
add_subdirectory(src/entry)
add_subdirectory(src/log)
add_subdirectory(src/platform)
add_subdirectory(src/wire)
add_subdirectory(src/store)
add_subdirectory(src/checked)
add_subdirectory(src/write)
add_subdirectory(src/app)
# QIVEN_HAL off leaves the HAL, its fake and its cases out of the build (H6).
if(QIVEN_HAL)
  add_subdirectory(src/hal)
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
  endforeach()
endwhile()
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/executables.txt" CONTENT "${qiven3_executables_text}")

# The analysis preset over the sources of src/, tests/support/ and
# tests/child/ (qiven3-child, the Windows adapter's children): /analyze on
# their targets, then clang-tidy over compile_commands.json, each source its
# own step after every target is built, by .clang-tidy. A header-only
# module is read through its unit in tests/support/headers/.
if(QIVEN_ANALYZE STREQUAL "analysis")
  set(qiven3_tidy_targets "")
  set(qiven3_tidy_sources "")
  set(qiven3_dirs "${PROJECT_SOURCE_DIR}")
  while(qiven3_dirs)
    list(POP_FRONT qiven3_dirs dir)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    list(APPEND qiven3_dirs ${subdirs})
    file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}" "${dir}")
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      # A plant that must not compile (QIVEN3_PLANT) is built by its test alone.
      get_target_property(plant ${target} QIVEN3_PLANT)
      if(type STREQUAL "INTERFACE_LIBRARY" OR plant)
        continue()
      endif()
      list(APPEND qiven3_tidy_targets ${target})
      if(NOT rel MATCHES "^(src|tests/support|tests/child)(/|$)")
        continue()
      endif()
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${qiven_analysis_options}>")
      get_target_property(sources ${target} SOURCES)
      foreach(source IN LISTS sources)
        get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${dir}")
        if(source MATCHES "[.]cpp$")
          list(APPEND qiven3_tidy_sources "${source}")
        endif()
      endforeach()
    endforeach()
  endwhile()
  list(REMOVE_DUPLICATES qiven3_tidy_sources)
  file(GLOB_RECURSE qiven3_tidy_inputs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/tests/support/*.hpp")
  # Each .clang-tidy at any depth, a new one reconfiguring; a source's step
  # depends on those of its directory and of each directory above it.
  file(GLOB_RECURSE qiven3_tidy_configs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.clang-tidy" "${PROJECT_SOURCE_DIR}/tests/*.clang-tidy")
  file(GLOB qiven3_tidy_root CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/*.clang-tidy")
  list(APPEND qiven3_tidy_configs ${qiven3_tidy_root})
  list(FILTER qiven3_tidy_configs INCLUDE REGEX "/[.]clang-tidy$")
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
    get_filename_component(source_dir "${source}" DIRECTORY)
    set(configs "")
    foreach(config IN LISTS qiven3_tidy_configs)
      get_filename_component(config_dir "${config}" DIRECTORY)
      string(FIND "${source_dir}/" "${config_dir}/" at)
      if(at EQUAL 0)
        list(APPEND configs "${config}")
      endif()
    endforeach()
    add_custom_command(OUTPUT "${stamp}"
      COMMAND "${QIVEN_CLANG_TIDY}" -p "${PROJECT_BINARY_DIR}/tidy" --quiet "${source}"
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
