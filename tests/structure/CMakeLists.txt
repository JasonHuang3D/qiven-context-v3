# The structure records (QIVEN3_STRUCTURE, the structure preset): after
# every target is built, records.py reruns each compile command with
# /showIncludes, reads each object's symbols and the runtimes' names with
# dumpbin, the link edges with CMake's --graphviz, and builds and records
# its planted project (plants.py), into build/<preset>/structure/. The
# structure tests read those files and start no process.
if(NOT MSVC)
  message(FATAL_ERROR "qiven3: QIVEN3_STRUCTURE reads MSVC's records; it is the structure preset's, on Windows")
endif()
if(NOT Python3_EXECUTABLE)
  message(FATAL_ERROR "qiven3: QIVEN3_STRUCTURE writes its records with the interpreter: name it")
endif()
get_filename_component(qiven3_linker_dir "${CMAKE_LINKER}" DIRECTORY)
find_program(QIVEN3_DUMPBIN dumpbin HINTS "${qiven3_linker_dir}" REQUIRED)

# Every built target of the project, so that the records follow each build.
set(qiven3_built "")
set(qiven3_files "")
set(qiven3_dirs "${PROJECT_SOURCE_DIR}")
while(qiven3_dirs)
  list(POP_FRONT qiven3_dirs dir)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  list(APPEND qiven3_dirs ${subdirs})
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    if(type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|EXECUTABLE)$")
      list(APPEND qiven3_built ${target})
      list(APPEND qiven3_files "$<TARGET_FILE:${target}>")
    endif()
  endforeach()
endwhile()

# Each configure rewrites compile_commands.json, its content the same or
# not; the records depend on a copy that changes only with the content, so
# that a configure that changed nothing leaves them as they are.
set(qiven3_commands "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json")
add_custom_command(OUTPUT "${qiven3_commands}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_BINARY_DIR}/compile_commands.json"
    "${qiven3_commands}"
  DEPENDS "${PROJECT_BINARY_DIR}/compile_commands.json"
  COMMENT "Copying the compile commands the records follow"
  VERBATIM)

set(qiven3_index "${PROJECT_BINARY_DIR}/structure/index.json")
add_custom_command(OUTPUT "${qiven3_index}"
  COMMAND "${Python3_EXECUTABLE}" -I "${CMAKE_CURRENT_SOURCE_DIR}/records.py"
    "${PROJECT_SOURCE_DIR}" "${PROJECT_BINARY_DIR}" "${CMAKE_COMMAND}" "${QIVEN3_DUMPBIN}"
  DEPENDS ${qiven3_files} "${CMAKE_CURRENT_SOURCE_DIR}/records.py" "${CMAKE_CURRENT_SOURCE_DIR}/plants.py"
    "${qiven3_commands}"
  COMMENT "Writing the structure records"
  VERBATIM)
add_custom_target(qiven3_structure_records ALL DEPENDS "${qiven3_index}")
add_dependencies(qiven3_structure_records ${qiven3_built})
