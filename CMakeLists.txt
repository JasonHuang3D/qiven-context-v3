cmake_minimum_required(VERSION 3.28)
project(qiven3 LANGUAGES C CXX)

option(QIVEN3_PIN_TOOLS "Fail configure on any compiler, SDK, CMake, Ninja or interpreter but the pinned ones" OFF)
option(QIVEN3_HAL "Build qiven3_hal, its fake and its cases" ON)
option(QIVEN3_STRUCTURE "Write the records the structure tests read" OFF)
option(QIVEN3_SYSTEM_HEAP "Send each block of v3's arenas and pools to the system heap, for AddressSanitizer; the counts and budgets unchanged" OFF)
option(QIVEN3_FAULTS "Give the arenas, pools and fake platform their failing countdown" OFF)
set(QIVEN3_SANITIZE "" CACHE STRING "Sanitizers for every target: empty, address, fuzzer,address; elsewhere address,undefined, thread or fuzzer,address,undefined")
set_property(CACHE QIVEN3_SANITIZE PROPERTY STRINGS "" "address" "fuzzer,address" "address,undefined" "thread" "fuzzer,address,undefined")
set(QIVEN3_ANALYZE "" CACHE STRING "Static analysis in the build, MSVC's: empty, stack or analysis")
set_property(CACHE QIVEN3_ANALYZE PROPERTY STRINGS "" "stack" "analysis")
set(QIVEN3_COMPILER_LINE "" CACHE STRING "When set, the compiler's major.minor line, for example 19.44")

# The standard is set through CMake, never by a raw /std: flag.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Headers, not named modules: no module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# Debug information inside each object (/Z7), so that the linker never looks
# for a compiler PDB by a path that /pathmap rewrote; each binary's own PDB
# stays apart.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

# The pins of this machine's toolchain.
set(QIVEN3_PIN_MSVC 19.44.35228)
set(QIVEN3_PIN_SDK 10.0.26100.0)
set(QIVEN3_PIN_CMAKE 4.4.3)
set(QIVEN3_PIN_NINJA 1.13.2)
set(QIVEN3_PIN_PYTHON 3.15.0)

message(STATUS "qiven3: C++ compiler ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "qiven3: C compiler ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")

if(QIVEN3_PIN_TOOLS)
  foreach(lang IN ITEMS C CXX)
    if(NOT CMAKE_${lang}_COMPILER_ID STREQUAL "MSVC"
       OR NOT CMAKE_${lang}_COMPILER_VERSION VERSION_EQUAL QIVEN3_PIN_MSVC)
      message(FATAL_ERROR "qiven3: the ${lang} compiler is ${CMAKE_${lang}_COMPILER_ID} "
        "${CMAKE_${lang}_COMPILER_VERSION}; the pin is MSVC ${QIVEN3_PIN_MSVC}")
    endif()
  endforeach()
  # vcvars sets WindowsSDKVersion with a trailing backslash. CMAKE_SYSTEM_VERSION
  # reads the running system under Ninja, not the SDK the compiler uses.
  string(REGEX REPLACE "[\\/]+$" "" qiven3_sdk "$ENV{WindowsSDKVersion}")
  if(NOT qiven3_sdk STREQUAL QIVEN3_PIN_SDK)
    message(FATAL_ERROR "qiven3: the toolchain's environment names the Windows SDK "
      "'${qiven3_sdk}'; the pin is ${QIVEN3_PIN_SDK}")
  endif()
  if(NOT CMAKE_VERSION VERSION_EQUAL QIVEN3_PIN_CMAKE)
    message(FATAL_ERROR "qiven3: CMake is ${CMAKE_VERSION}; the pin is ${QIVEN3_PIN_CMAKE}")
  endif()
  if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "qiven3: the generator is ${CMAKE_GENERATOR}; the pin is Ninja")
  endif()
  execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
    OUTPUT_VARIABLE qiven3_ninja OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE qiven3_ninja_status)
  if(NOT qiven3_ninja_status EQUAL 0 OR NOT qiven3_ninja VERSION_EQUAL QIVEN3_PIN_NINJA)
    message(FATAL_ERROR "qiven3: Ninja is '${qiven3_ninja}'; the pin is ${QIVEN3_PIN_NINJA}")
  endif()
  if(NOT Python3_EXECUTABLE)
    message(FATAL_ERROR "qiven3: name the interpreter, CPython ${QIVEN3_PIN_PYTHON}, "
      "by Python3_EXECUTABLE (see README.md)")
  endif()
  message(STATUS "qiven3: pinned tools hold: SDK ${qiven3_sdk}, CMake ${CMAKE_VERSION}, Ninja ${qiven3_ninja}")
endif()

if(QIVEN3_COMPILER_LINE)
  string(REGEX MATCH "^[0-9]+\\.[0-9]+" qiven3_line "${CMAKE_CXX_COMPILER_VERSION}")
  if(NOT qiven3_line STREQUAL QIVEN3_COMPILER_LINE)
    message(FATAL_ERROR "qiven3: the compiler's line is ${qiven3_line} "
      "(${CMAKE_CXX_COMPILER_VERSION}); the preset asks ${QIVEN3_COMPILER_LINE}")
  endif()
endif()

if(Python3_EXECUTABLE)
  find_package(Python3 ${QIVEN3_PIN_PYTHON} EXACT REQUIRED COMPONENTS Interpreter)
  message(STATUS "qiven3: interpreter CPython ${Python3_VERSION}")
endif()

# The flags of every core and test target, through qiven3_flags. RTTI is
# off: no code of v3's uses dynamic_cast or typeid, and no standard facility
# v3 uses needs RTTI. MSVC still compiles typeid of a type with no virtual
# function under /GR-, so the structure check bans both names either way.
if(MSVC)
  # At /W4, C4244 and C4267, the narrowing conversions, are errors by /WX.
  set(qiven3_cxx_flags /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /EHsc /GR-)
else()
  # No frame of v3's own code passes 4,096 bytes.
  set(qiven3_cxx_flags -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    -Wshadow -Wframe-larger-than=4096 -Werror -finput-charset=UTF-8 -fno-rtti)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND qiven3_cxx_flags -Wthread-safety)
  endif()
endif()
add_library(qiven3_flags INTERFACE)
target_compile_options(qiven3_flags INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:${qiven3_cxx_flags}>")
if(MSVC)
  # /RTCs fills each frame with 0xcc; MSVC has no optimized equivalent.
  target_compile_options(qiven3_flags INTERFACE "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/RTCs>")
else()
  target_compile_options(qiven3_flags INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-ftrivial-auto-var-init=pattern>")
endif()
# The linker's warnings are errors too, on the same targets. Each binary
# reserves 1 MiB of stack for its main thread.
if(MSVC)
  target_link_options(qiven3_flags INTERFACE /WX /STACK:1048576)
elseif(APPLE)
  target_link_options(qiven3_flags INTERFACE LINKER:-fatal_warnings)
else()
  target_link_options(qiven3_flags INTERFACE LINKER:--fatal-warnings)
endif()

# Configure reads __cplusplus itself, under the build's flags: a /std: the
# compiler does not know only warns and leaves an older language.
include(CheckCXXSourceCompiles)
list(JOIN qiven3_cxx_flags " " CMAKE_REQUIRED_FLAGS)
unset(QIVEN3_CPLUSPLUS_20 CACHE)
check_cxx_source_compiles("
#if __cplusplus < 202002L
#error __cplusplus is below 202002
#endif
int main() { return 0; }" QIVEN3_CPLUSPLUS_20)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT QIVEN3_CPLUSPLUS_20)
  message(FATAL_ERROR "qiven3: __cplusplus is below 202002 under the build's flags")
endif()

if(QIVEN3_SANITIZE)
  if(MSVC)
    # cl ignores a combined /fsanitize=fuzzer,address with D9002, exit 0 even
    # under /WX: one option per sanitizer.
    string(REPLACE "," ";" qiven3_sanitizers "${QIVEN3_SANITIZE}")
    foreach(sanitizer IN LISTS qiven3_sanitizers)
      add_compile_options("/fsanitize=${sanitizer}")
    endforeach()
    add_link_options(/INCREMENTAL:NO)
  else()
    # libFuzzer's main is linked into the fuzz targets alone; every other
    # target takes its instrumentation without it.
    string(REPLACE "fuzzer" "fuzzer-no-link" qiven3_sanitizers "${QIVEN3_SANITIZE}")
    add_compile_options("-fsanitize=${qiven3_sanitizers}" -fno-sanitize-recover=all
      -fno-omit-frame-pointer)
    add_link_options("-fsanitize=${qiven3_sanitizers}")
  endif()
  message(STATUS "qiven3: sanitizers ${QIVEN3_SANITIZE}")
endif()

# The standard library's hardened mode, in every build: an index past a
# span's or a container's end stops the process.
if(MSVC)
  add_compile_definitions(_MSVC_STL_HARDENING=1)
else()
  add_compile_definitions(_GLIBCXX_ASSERTIONS
    _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
endif()

# Clean binaries: no source, toolset or SDK path of the building machine in
# an object, a library or a binary, and the same bytes from the same
# sources. Each path comes from configure. cl ignores /pathmap without
# /experimental:deterministic (D9007), and mapping the source alone leaves
# the toolset's install path in the object. The linker maps no path: its
# own record in each binary's PDB keeps them, a residual.
if(MSVC)
  get_filename_component(qiven3_toolset_dir "${CMAKE_CXX_COMPILER}/../../../.." ABSOLUTE)
  set(qiven3_path_maps "${PROJECT_SOURCE_DIR}=." "${qiven3_toolset_dir}=msvc")
  if(DEFINED ENV{WindowsSdkDir})
    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" qiven3_sdk_dir)
    string(REGEX REPLACE "/+$" "" qiven3_sdk_dir "${qiven3_sdk_dir}")
    list(APPEND qiven3_path_maps "${qiven3_sdk_dir}=sdk")
  else()
    message(WARNING "qiven3: WindowsSdkDir is unset: the SDK's paths stay unmapped")
  endif()
  add_compile_options(/experimental:deterministic /Brepro)
  foreach(map IN LISTS qiven3_path_maps)
    file(TO_NATIVE_PATH "${map}" map)
    add_compile_options("/pathmap:${map}")
  endforeach()
  add_link_options(/Brepro /PDBALTPATH:%_PDB%)
  # A static library takes the librarian's options, not the linker's.
  string(APPEND CMAKE_STATIC_LINKER_FLAGS " /Brepro")
else()
  # The toolchain's directory is the one above the compiler's bin/; for a
  # compiler in the system's /usr/bin, Apple's among them, it is /usr, never
  # the root, which would match every path. Of the maps that match a path,
  # gcc and clang take the last, so the source directory's comes last.
  get_filename_component(qiven3_toolchain_dir "${CMAKE_CXX_COMPILER}/../.." ABSOLUTE)
  if(qiven3_toolchain_dir STREQUAL "/")
    set(qiven3_toolchain_dir "/usr")
  endif()
  set(qiven3_path_maps "${qiven3_toolchain_dir}=toolchain")
  if(IS_ABSOLUTE "${CMAKE_OSX_SYSROOT}")
    list(APPEND qiven3_path_maps "${CMAKE_OSX_SYSROOT}=sdk")
  endif()
  list(APPEND qiven3_path_maps "${PROJECT_SOURCE_DIR}=.")
  foreach(map IN LISTS qiven3_path_maps)
    add_compile_options("-ffile-prefix-map=${map}")
  endforeach()
endif()
list(JOIN qiven3_path_maps ", " qiven3_maps_text)
message(STATUS "qiven3: paths mapped: ${qiven3_maps_text}")

# Static analysis of v3's own sources, never third_party/, every finding an
# error under /WX. stack: C6262 alone, a frame past 4,096 bytes. analysis:
# the Core Guidelines checker and its lifetime rules, then clang-tidy over
# compile_commands.json (below). The rule sets list each rule turned off
# with its reason.
if(QIVEN3_ANALYZE AND NOT MSVC)
  message(FATAL_ERROR "qiven3: QIVEN3_ANALYZE is MSVC's; gcc and clang check frames by -Wframe-larger-than")
endif()
if(QIVEN3_ANALYZE STREQUAL "stack")
  target_compile_options(qiven3_flags INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/analyze;/analyze:stacksize4096;/analyze:ruleset${PROJECT_SOURCE_DIR}/stack.ruleset>")
elseif(QIVEN3_ANALYZE STREQUAL "analysis")
  # The rule set includes Visual Studio's own by name, found in its directory.
  # Each /analyze option is one token, its value attached, so that the
  # clang-tidy step can drop them (below).
  file(TO_CMAKE_PATH "$ENV{VSINSTALLDIR}" qiven3_vs_dir)
  set(qiven3_rule_sets "${qiven3_vs_dir}/Team Tools/Static Analysis Tools/Rule Sets")
  if(NOT EXISTS "${qiven3_rule_sets}/CppCoreCheckRules.ruleset")
    message(FATAL_ERROR "qiven3: no CppCoreCheckRules.ruleset under VSINSTALLDIR; configure from the toolset's environment")
  endif()
  # On the targets of src/ and tests/support/ alone (below).
  set(qiven3_analysis_options /analyze /analyze:pluginEspXEngine.dll
    "/analyze:ruleset${PROJECT_SOURCE_DIR}/analysis.ruleset"
    "/analyze:rulesetdirectory${qiven3_rule_sets}" /analyze:external- /external:anglebrackets /external:W0)
  find_program(QIVEN3_CLANG_TIDY clang-tidy HINTS "$ENV{ProgramFiles}/LLVM/bin" REQUIRED)
  execute_process(COMMAND "${QIVEN3_CLANG_TIDY}" --version OUTPUT_VARIABLE qiven3_tidy_version)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" qiven3_tidy_version "${qiven3_tidy_version}")
  message(STATUS "qiven3: clang-tidy ${qiven3_tidy_version}")
elseif(QIVEN3_ANALYZE)
  message(FATAL_ERROR "qiven3: QIVEN3_ANALYZE is '${QIVEN3_ANALYZE}'; it is empty, stack or analysis")
endif()

# Both options change the memory package's types, so every target sees them.
if(QIVEN3_SYSTEM_HEAP)
  add_compile_definitions(QIVEN3_SYSTEM_HEAP)
endif()
if(QIVEN3_FAULTS)
  add_compile_definitions(QIVEN3_FAULTS)
endif()

message(STATUS "qiven3: QIVEN3_HAL ${QIVEN3_HAL}, QIVEN3_STRUCTURE ${QIVEN3_STRUCTURE}, "
  "QIVEN3_SYSTEM_HEAP ${QIVEN3_SYSTEM_HEAP}, QIVEN3_FAULTS ${QIVEN3_FAULTS}")

# Every binary in one directory: the tests find each there, and the
# sanitizer's runtime sits beside them all.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

enable_testing()
add_subdirectory(src/memory)
add_subdirectory(src/qiven3)
add_subdirectory(src/platform)
# QIVEN3_HAL off leaves the HAL, its fake and its cases out of the build (H6).
if(QIVEN3_HAL)
  add_subdirectory(src/hal)
endif()
add_subdirectory(tests)

# The analysis preset over the sources of src/ and tests/support/: /analyze
# on their targets, then clang-tidy over compile_commands.json, each source
# its own step after every target is built, by .clang-tidy.
if(QIVEN3_ANALYZE STREQUAL "analysis")
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
      if(type STREQUAL "INTERFACE_LIBRARY")
        continue()
      endif()
      list(APPEND qiven3_tidy_targets ${target})
      if(NOT rel MATCHES "^(src|tests/support)(/|$)")
        continue()
      endif()
      target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${qiven3_analysis_options}>")
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
    "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/tests/support/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/.clang-tidy" "${PROJECT_SOURCE_DIR}/src/*/.clang-tidy")
  list(APPEND qiven3_tidy_inputs "${PROJECT_SOURCE_DIR}/.clang-tidy")
  # clang-cl reads an option it does not know, /analyze's and /pathmap's, as
  # a file and fails, and warns on /experimental: and /external:; the step
  # reads the database without them.
  file(WRITE "${PROJECT_BINARY_DIR}/tidy/database.cmake" [=[
file(READ "${BUILD}/compile_commands.json" db)
string(JSON count LENGTH "${db}")
math(EXPR last "${count} - 1")
foreach(i RANGE ${last})
  string(JSON command GET "${db}" ${i} command)
  string(REGEX REPLACE " (\"/(analyze|pathmap|experimental|external)[^\"]*\"|/(analyze|pathmap|experimental|external)[^ ]*)" "" command "${command}")
  string(REPLACE "\\" "\\\\" command "${command}")
  string(REPLACE "\"" "\\\"" command "${command}")
  string(JSON db SET "${db}" ${i} command "\"${command}\"")
endforeach()
file(WRITE "${BUILD}/tidy/compile_commands.json" "${db}")
]=])
  add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/tidy/compile_commands.json"
    COMMAND "${CMAKE_COMMAND}" "-DBUILD=${PROJECT_BINARY_DIR}" -P "${PROJECT_BINARY_DIR}/tidy/database.cmake"
    DEPENDS "${PROJECT_BINARY_DIR}/compile_commands.json"
    COMMENT "clang-tidy's database" VERBATIM)
  set(qiven3_stamps "")
  foreach(source IN LISTS qiven3_tidy_sources)
    file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}" "${source}")
    string(MAKE_C_IDENTIFIER "${rel}" stamp)
    set(stamp "${PROJECT_BINARY_DIR}/tidy/${stamp}.stamp")
    add_custom_command(OUTPUT "${stamp}"
      COMMAND "${QIVEN3_CLANG_TIDY}" -p "${PROJECT_BINARY_DIR}/tidy" --quiet "${source}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${stamp}"
      DEPENDS "${source}" "${PROJECT_BINARY_DIR}/tidy/compile_commands.json" ${qiven3_tidy_inputs}
      COMMENT "clang-tidy ${rel}" VERBATIM)
    list(APPEND qiven3_stamps "${stamp}")
  endforeach()
  add_custom_target(qiven3_tidy DEPENDS ${qiven3_stamps})
  add_dependencies(qiven3_tidy ${qiven3_tidy_targets})
  list(LENGTH qiven3_stamps qiven3_count)
  message(STATUS "qiven3: clang-tidy over ${qiven3_count} sources, target qiven3_tidy")
endif()
