# The structure records and their rules as steps of the build
# (QIVEN_STRUCTURE: the daily tree ci-windows and the structure preset),
# steps.py's, into build/<preset>/structure/. Each object's compile, through
# steps.py as its launcher, writes its record from its own /showIncludes and
# dumpbin /symbols and applies the rules that record decides alone: Ninja
# reruns it exactly when the object recompiles. After every target, one step
# a target that holds objects applies the rules over the names its link
# closure defines, rerun when it relinks or its closure's records change
# (its depfile). The link rules run over the tree's graph, rerun only when
# the configure's account of the link properties and the cache changes
# (file(CONFIGURE) leaves it as it is otherwise): a C++ source the header
# units' lexer reads reconfigures the tree too (header_units.cmake). A
# finding fails the build, naming file and, where one holds it, line.
if(NOT MSVC)
  message(FATAL_ERROR "qiven: QIVEN_STRUCTURE reads MSVC's records; it is a Windows tree's")
endif()
# The interpreter, QIVEN_PYTHON's first (build.cmake).
if(NOT Python3_EXECUTABLE)
  message(FATAL_ERROR "qiven: QIVEN_STRUCTURE writes its records with the interpreter: name it")
endif()
get_filename_component(qiven_linker_dir "${CMAKE_LINKER}" DIRECTORY)
find_program(QIVEN_DUMPBIN dumpbin HINTS "${qiven_linker_dir}" REQUIRED)

set(qiven_records "${PROJECT_BINARY_DIR}/structure")
set(qiven_steps "${CMAKE_CURRENT_SOURCE_DIR}/steps.py")
# The rules' code and the trees' table: a change reruns every target's step and the link step.
file(GLOB qiven_rules_code "${CMAKE_CURRENT_SOURCE_DIR}/*.py" "${qiven_root}/foundation/devkit/python/*.py")
list(APPEND qiven_rules_code "${qiven_trees_file}")

# The runtimes' and the network's names, before any compile reads them.
set(qiven_libraries "")
string(REPLACE "\\" "/" qiven_lib_dirs "$ENV{LIB}")
foreach(lib IN ITEMS libcmt.lib libcpmt.lib libvcruntime.lib libucrt.lib ws2_32.lib winhttp.lib wininet.lib)
  find_file(qiven_lib_${lib} ${lib} PATHS ${qiven_lib_dirs} NO_DEFAULT_PATH NO_CACHE)
  if(qiven_lib_${lib})
    list(APPEND qiven_libraries "${qiven_lib_${lib}}")
  endif()
endforeach()
add_custom_command(OUTPUT "${qiven_records}/names.json"
  COMMAND "${Python3_EXECUTABLE}" -I "${qiven_steps}" libraries "${qiven_records}" "${QIVEN_DUMPBIN}"
  DEPENDS ${qiven_libraries} "${qiven_steps}" "${CMAKE_CURRENT_SOURCE_DIR}/records.py"
  COMMENT "Reading the runtimes' and the network's names"
  VERBATIM)
add_custom_target(qiven_structure_libraries DEPENDS "${qiven_records}/names.json")

# Each configure rewrites compile_commands.json, its content the same or
# not; the steps depend on a copy that changes only with the content.
set(qiven_commands "${qiven_records}/compile_commands.json")
add_custom_command(OUTPUT "${qiven_commands}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${PROJECT_BINARY_DIR}/compile_commands.json"
    "${qiven_commands}"
  DEPENDS "${PROJECT_BINARY_DIR}/compile_commands.json"
  COMMENT "Copying the compile commands the steps follow"
  VERBATIM)

# Every target of the project: the link account names each; a target that
# holds objects compiles through the launcher; one built by the build has
# its step.
set(qiven_launcher "${Python3_EXECUTABLE}" -I "${qiven_steps}" compile "${qiven_root}" "${qiven_records}"
  "${QIVEN_DUMPBIN}" --)
set(qiven_links "")
set(qiven_built "")
set(qiven_stamps "")
set(qiven_dirs "${PROJECT_SOURCE_DIR}")
while(qiven_dirs)
  list(POP_FRONT qiven_dirs dir)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
  list(APPEND qiven_dirs ${subdirs})
  get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    get_target_property(links ${target} LINK_LIBRARIES)
    get_target_property(interface ${target} INTERFACE_LINK_LIBRARIES)
    string(APPEND qiven_links "${target}\t${type}\t${links}\t${interface}\n")
    if(NOT type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
      continue()
    endif()
    set_target_properties(${target} PROPERTIES C_COMPILER_LAUNCHER "${qiven_launcher}"
      CXX_COMPILER_LAUNCHER "${qiven_launcher}")
    add_dependencies(${target} qiven_structure_libraries)
    get_target_property(excluded ${target} EXCLUDE_FROM_ALL)
    if(excluded)
      continue()
    endif()
    list(APPEND qiven_built ${target})
    if(type STREQUAL "OBJECT_LIBRARY")
      set(made "$<TARGET_OBJECTS:${target}>")
    else()
      set(made "$<TARGET_FILE:${target}>")
    endif()
    set(stamp "${qiven_records}/targets/${target}.stamp")
    add_custom_command(OUTPUT "${stamp}"
      COMMAND "${Python3_EXECUTABLE}" -I "${qiven_steps}" target "${qiven_root}" "${qiven_records}" ${target}
      DEPENDS ${made} "${qiven_records}/targets.dot" "${qiven_records}/names.json" "${qiven_commands}"
        ${qiven_rules_code}
      DEPFILE "${qiven_records}/targets/${target}.d"
      COMMENT "The structure rules over ${target} and its link closure"
      VERBATIM)
    list(APPEND qiven_stamps "${stamp}")
  endforeach()
endwhile()

# The link account: the cache, as the graph's configure takes it, then each
# target's link properties; written only where its content changes.
set(qiven_account "")
get_cmake_property(cached CACHE_VARIABLES)
foreach(name IN LISTS cached)
  get_property(type CACHE "${name}" PROPERTY TYPE)
  if(NOT type MATCHES "^(INTERNAL|STATIC)$")
    get_property(value CACHE "${name}" PROPERTY VALUE)
    string(APPEND qiven_account "-D${name}:${type}=${value}\n")
  endif()
endforeach()
string(APPEND qiven_account "${qiven_links}")
file(CONFIGURE OUTPUT "${qiven_records}/links.txt" CONTENT "@qiven_account@" @ONLY)
add_custom_command(OUTPUT "${qiven_records}/links.stamp"
  BYPRODUCTS "${qiven_records}/targets.dot"
  COMMAND "${Python3_EXECUTABLE}" -I "${qiven_steps}" links "${qiven_root}" "${qiven_records}"
    "${PROJECT_BINARY_DIR}" "${CMAKE_COMMAND}" "${qiven_records}/links.txt"
  DEPENDS "${qiven_records}/links.txt" "${qiven_commands}" ${qiven_rules_code}
  COMMENT "The link rules over the tree's graph"
  VERBATIM)

add_custom_target(qiven_structure_rules ALL DEPENDS "${qiven_records}/links.stamp" ${qiven_stamps})
add_dependencies(qiven_structure_rules ${qiven_built})
