# The structure records (QIVEN_STRUCTURE, the structure preset): after
# every target is built, records.py reruns each compile command with
# /showIncludes, reads each object's symbols and the runtimes' names with
# dumpbin and the link edges with CMake's --graphviz, into
# build/<preset>/structure/. The
# structure tests read those files and start no process. The records'
# trees are tools/loop_modules.py's, read from tools/trees.json: both,
# and foundation/devkit/python/git.py and tools/loop_table.py the reader
# loads, are the step's inputs, with records.py, every built target and the compile
# commands; and, by the depfile records.py writes beside index.json, every
# file index.json digests, so that a recorded file's change or removal
# reruns the step though no target compiles it.
if(NOT MSVC)
  message(FATAL_ERROR "qiven: QIVEN_STRUCTURE reads MSVC's records; it is the structure preset's, on Windows")
endif()
if(NOT Python3_EXECUTABLE)
  message(FATAL_ERROR "qiven: QIVEN_STRUCTURE writes its records with the interpreter: name it")
endif()
get_filename_component(qiven_linker_dir "${CMAKE_LINKER}" DIRECTORY)
find_program(QIVEN_DUMPBIN dumpbin HINTS "${qiven_linker_dir}" REQUIRED)

# Every built target of the project, so that the records follow each build.
set(qiven_built "")
set(qiven_files "")
set(qiven_dirs "${PROJECT_SOURCE_DIR}")
while(qiven_dirs)
  list(POP_FRONT qiven_dirs dir)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  list(APPEND qiven_dirs ${subdirs})
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|EXECUTABLE)$")
      list(APPEND qiven_built ${target})
      list(APPEND qiven_files "$<TARGET_FILE:${target}>")
    endif()
  endforeach()
endwhile()

# Each configure rewrites compile_commands.json, its content the same or
# not; the records depend on a copy that changes only with the content, so
# that a configure that changed nothing leaves them as they are.
set(qiven_commands "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json")
add_custom_command(OUTPUT "${qiven_commands}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_BINARY_DIR}/compile_commands.json"
    "${qiven_commands}"
  DEPENDS "${PROJECT_BINARY_DIR}/compile_commands.json"
  COMMENT "Copying the compile commands the records follow"
  VERBATIM)

set(qiven_index "${PROJECT_BINARY_DIR}/structure/index.json")
add_custom_command(OUTPUT "${qiven_index}"
  COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/records.py"
    "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}" "${CMAKE_COMMAND}" "${QIVEN_DUMPBIN}"
  DEPENDS ${qiven_files} "${CMAKE_CURRENT_SOURCE_DIR}/records.py"
    "${PROJECT_SOURCE_DIR}/tools/loop_modules.py" "${PROJECT_SOURCE_DIR}/foundation/devkit/python/git.py"
    "${PROJECT_SOURCE_DIR}/tools/loop_table.py"
    "${PROJECT_SOURCE_DIR}/tools/trees.json" "${qiven_commands}"
  DEPFILE "${PROJECT_BINARY_DIR}/structure/index.d"
  COMMENT "Writing the structure records"
  VERBATIM)
add_custom_target(qiven_structure_records ALL DEPENDS "${qiven_index}")
add_dependencies(qiven_structure_records ${qiven_built})
