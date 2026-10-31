# The build: its options, pins, flags, hardening, sanitizers, clean binaries,
# analysis and qiven_executable, every target's; included by the root
# CMakeLists.txt after project(), before any target.
option(QIVEN_PIN_TOOLS "Fail configure on any compiler, SDK, CMake, Ninja or interpreter but the pinned ones; off on CI, which is unpinned" OFF)
option(QIVEN_HAL "Build qiven3_hal, its fake and its cases" ON)
option(QIVEN3_STRUCTURE "Write the records the structure tests read" OFF)
option(QIVEN_SYSTEM_HEAP "Send each block of v3's arenas and pools to the system heap, for AddressSanitizer; the counts and budgets unchanged" OFF)
option(QIVEN_FAULTS "Give the arenas, pools and fake platform their failing countdown" OFF)
set(QIVEN_SANITIZE "" CACHE STRING "Sanitizers for every target: empty, address, fuzzer,address; elsewhere address,undefined, thread, fuzzer,address,undefined or memory")
set_property(CACHE QIVEN_SANITIZE PROPERTY STRINGS "" "address" "fuzzer,address" "address,undefined" "thread" "fuzzer,address,undefined" "memory")
set(QIVEN_ANALYZE "" CACHE STRING "Static analysis in the build, MSVC's: empty, stack or analysis")
set_property(CACHE QIVEN_ANALYZE PROPERTY STRINGS "" "stack" "analysis")
set(QIVEN_LOG_MIN "" CACHE STRING "The log's minimum level: trace, debug, info, warn or error; empty, trace in a Debug build and info in every other")
set_property(CACHE QIVEN_LOG_MIN PROPERTY STRINGS "" "trace" "debug" "info" "warn" "error")

# The standard is set through CMake, never by a raw /std: flag.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# C, which sqlite3.c alone is, likewise: C17 without GNU extensions, so that
# its dialect never follows a compiler's default. C17 is the newest standard
# each compiler here takes whole (MSVC as /std:c17); SQLite needs C89, and
# strict C only drops its forced inlining of one function (__STRICT_ANSI__).
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# Headers, not named modules: no module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# The C runtime linked statically, so that a binary imports the system's
# libraries alone (tests/structure/imports.py); libFuzzer needs the DLL.
if(QIVEN_SANITIZE MATCHES "fuzzer")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
# Debug information inside each object (/Z7), so that the linker never looks
# for a compiler PDB by a path that /pathmap rewrote; each binary's own PDB
# stays apart.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
# On Linux every executable position-independent, stated here rather than
# left to the compiler's default, which differs between builds of the same
# compiler: a fault dump's frames are backtrace_symbols_fd's module offsets,
# which the watcher symbolizes, and a non-PIE executable's frames carry none.
if(NOT MSVC AND NOT APPLE)
  cmake_policy(SET CMP0083 NEW)
  include(CheckPIESupported)
  check_pie_supported(OUTPUT_VARIABLE qiven_pie_output LANGUAGES C CXX)
  if(NOT CMAKE_C_LINK_PIE_SUPPORTED OR NOT CMAKE_CXX_LINK_PIE_SUPPORTED)
    message(FATAL_ERROR "qiven3: the toolchain does not link position-independent executables: "
      "${qiven_pie_output}")
  endif()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
# On Linux the rest of the hardening a distribution's compiler may turn on
# by default, stated here as PIE is, for every target, SQLite's C included,
# so that a compiler built with its own defaults, which turns on none of it,
# builds as CI's image compilers do: the stack protector, stack-clash
# probes, CET's branch and return checks, relro and now, and a build id.
# These are ELF and x86-64 options; macos-clang, Mach-O on arm64, a preset
# no job builds now, takes its platform's defaults.
# _FORTIFY_SOURCE=3 where the build optimizes and no sanitizer runs; always
# undefined first, so that a compiler defining it by default builds the same
# and meets no redefinition. The presets under a sanitizer, linux-gcc,
# linux-clang, linux-tsan, linux-fuzz and linux-msan, leave it undefined:
# the runtimes intercept a few of glibc's checking functions, not
# __memcpy_chk and its kin, so a call the checks take goes around the
# sanitizer, and glibc's own abort stands for the sanitizer's report.
if(NOT MSVC AND NOT APPLE)
  add_compile_options(-fstack-protector-strong -fstack-clash-protection -fcf-protection=full
    -U_FORTIFY_SOURCE)
  if(NOT QIVEN_SANITIZE)
    add_compile_options("$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-D_FORTIFY_SOURCE=3>")
  endif()
  add_link_options(LINKER:-z,relro,-z,now LINKER:--build-id)
endif()

# The pins of this machine's toolchain, which QIVEN_PIN_TOOLS checks: MSVC
# on Windows; in WSL2 g++ 13 and clang 18 as tools/linux-pins.txt has them.
# CI leaves the option off and takes its image's tools.
set(QIVEN_PIN_MSVC 19.44.35228)
set(QIVEN_PIN_GCC 13.3.0)
set(QIVEN_PIN_CLANG 18.1.3)
set(QIVEN_PIN_SDK 10.0.26100.0)
set(QIVEN_PIN_CMAKE 4.4.3)
set(QIVEN_PIN_NINJA 1.13.2)
set(QIVEN_PIN_PYTHON 3.15.0)

message(STATUS "qiven3: C++ compiler ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "qiven3: C compiler ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
# The C++ compiler's id in the cache, where K11 reads it: clang's sanitizer
# runtimes, not gcc's, link a residual (tests/structure/imports.py).
set(QIVEN_CXX_COMPILER_ID "${CMAKE_CXX_COMPILER_ID}" CACHE INTERNAL "the C++ compiler's CMake id")

if(QIVEN_PIN_TOOLS)
  # MSVC's version by VERSION_EQUAL: CMake gives it a fourth part, 19.44.35228.0.
  foreach(lang IN ITEMS C CXX)
    set(qiven_id "${CMAKE_${lang}_COMPILER_ID}")
    set(qiven_version "${CMAKE_${lang}_COMPILER_VERSION}")
    if(WIN32)
      set(qiven_pinned "MSVC ${QIVEN_PIN_MSVC}")
      set(qiven_held FALSE)
      if(qiven_id STREQUAL "MSVC" AND qiven_version VERSION_EQUAL QIVEN_PIN_MSVC)
        set(qiven_held TRUE)
      endif()
    else()
      set(qiven_pinned "GNU ${QIVEN_PIN_GCC} or Clang ${QIVEN_PIN_CLANG}")
      set(qiven_held FALSE)
      if((qiven_id STREQUAL "GNU" AND qiven_version VERSION_EQUAL QIVEN_PIN_GCC)
         OR (qiven_id STREQUAL "Clang" AND qiven_version VERSION_EQUAL QIVEN_PIN_CLANG))
        set(qiven_held TRUE)
      endif()
    endif()
    if(NOT qiven_held)
      message(FATAL_ERROR "qiven3: the ${lang} compiler is ${qiven_id} ${qiven_version}; "
        "the pin is ${qiven_pinned}")
    endif()
  endforeach()
  set(qiven_sdk "none off Windows")
  if(WIN32)
    # vcvars sets WindowsSDKVersion with a trailing backslash. CMAKE_SYSTEM_VERSION
    # reads the running system under Ninja, not the SDK the compiler uses.
    string(REGEX REPLACE "[\\/]+$" "" qiven_sdk "$ENV{WindowsSDKVersion}")
    if(NOT qiven_sdk STREQUAL QIVEN_PIN_SDK)
      message(FATAL_ERROR "qiven3: the toolchain's environment names the Windows SDK "
        "'${qiven_sdk}'; the pin is ${QIVEN_PIN_SDK}")
    endif()
  endif()
  if(NOT CMAKE_VERSION VERSION_EQUAL QIVEN_PIN_CMAKE)
    message(FATAL_ERROR "qiven3: CMake is ${CMAKE_VERSION}; the pin is ${QIVEN_PIN_CMAKE}")
  endif()
  if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "qiven3: the generator is ${CMAKE_GENERATOR}; the pin is Ninja")
  endif()
  execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
    OUTPUT_VARIABLE qiven_ninja OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE qiven_ninja_status)
  if(NOT qiven_ninja_status EQUAL 0 OR NOT qiven_ninja VERSION_EQUAL QIVEN_PIN_NINJA)
    message(FATAL_ERROR "qiven3: Ninja is '${qiven_ninja}'; the pin is ${QIVEN_PIN_NINJA}")
  endif()
  if(NOT Python3_EXECUTABLE)
    message(FATAL_ERROR "qiven3: name the interpreter, CPython ${QIVEN_PIN_PYTHON}, "
      "by Python3_EXECUTABLE (see README.md)")
  endif()
  message(STATUS "qiven3: pinned tools hold: SDK ${qiven_sdk}, CMake ${CMAKE_VERSION}, Ninja ${qiven_ninja}")
endif()

# The interpreter: the pin exactly with QIVEN_PIN_TOOLS; else, as on CI,
# whose runner gives its own, CPython 3.15 or later.
if(Python3_EXECUTABLE AND QIVEN_PIN_TOOLS)
  find_package(Python3 ${QIVEN_PIN_PYTHON} EXACT REQUIRED COMPONENTS Interpreter)
elseif(Python3_EXECUTABLE)
  find_package(Python3 3.15 REQUIRED COMPONENTS Interpreter)
endif()
if(Python3_EXECUTABLE)
  message(STATUS "qiven3: interpreter CPython ${Python3_VERSION}")
endif()

# SQLite 3.53.4's amalgamation, vendored unchanged in third_party/sqlite/:
# each file's SHA-256 checked on every configure, pinned or not, so that a
# changed amalgamation never builds.
set(QIVEN_SQLITE_SHA256
  "sqlite3.c=b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189"
  "sqlite3.h=919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d")
foreach(pin IN LISTS QIVEN_SQLITE_SHA256)
  string(REPLACE "=" ";" pin "${pin}")
  list(GET pin 0 qiven_file)
  list(GET pin 1 qiven_pinned)
  set(qiven_path "${PROJECT_SOURCE_DIR}/third_party/sqlite/${qiven_file}")
  if(NOT EXISTS "${qiven_path}")
    message(FATAL_ERROR "qiven3: third_party/sqlite/${qiven_file} is missing")
  endif()
  file(SHA256 "${qiven_path}" qiven_digest)
  if(NOT qiven_digest STREQUAL qiven_pinned)
    message(FATAL_ERROR "qiven3: third_party/sqlite/${qiven_file} has SHA-256 ${qiven_digest}; "
      "the pin is ${qiven_pinned}")
  endif()
endforeach()
message(STATUS "qiven3: SQLite's amalgamation holds its pinned digests")

# The flags of every core and test target, through qiven_build. RTTI is
# off: no code of v3's uses dynamic_cast or typeid, and no standard facility
# v3 uses needs RTTI. MSVC still compiles typeid of a type with no virtual
# function under /GR-, so the structure check bans both names either way.
if(MSVC)
  # At /W4, C4244 and C4267, the narrowing conversions, are errors by /WX.
  set(qiven_cxx_flags /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /EHsc /GR-)
else()
  set(qiven_cxx_flags -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    -Wshadow -Werror -finput-charset=UTF-8 -fno-rtti)
  # No frame of v3's own code passes 4,096 bytes. The bound is measured on
  # uninstrumented builds: a sanitizer enlarges frames, so a build under one
  # leaves the bound out, and linux-plain and macos-clang enforce it.
  if(NOT QIVEN_SANITIZE)
    list(APPEND qiven_cxx_flags -Wframe-larger-than=4096)
  endif()
  # On clang, and AppleClang, the constant evaluator's steps are twice its
  # default: check's vectors, each a whole entry, take up to about 1.2
  # million on libstdc++ 13 under its assertions.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    list(APPEND qiven_cxx_flags -Wthread-safety -fconstexpr-steps=2097152)
  endif()
endif()
add_library(qiven_build INTERFACE)
target_compile_options(qiven_build INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:${qiven_cxx_flags}>")
if(MSVC)
  # /RTCs fills each frame with 0xcc; MSVC has no optimized equivalent.
  target_compile_options(qiven_build INTERFACE "$<$<AND:$<COMPILE_LANGUAGE:CXX>,$<CONFIG:Debug>>:/RTCs>")
elseif(NOT QIVEN_SANITIZE MATCHES "memory")
  # MemorySanitizer reports a read of a byte never written; the pattern
  # would write every automatic one first, so its build leaves the fill out.
  target_compile_options(qiven_build INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-ftrivial-auto-var-init=pattern>")
endif()
# The linker's warnings are errors too, on the same targets. Each binary
# reserves 1 MiB of stack for its main thread. On Windows each target names
# the system's libraries it links, and CMake's default list, which named
# some of them again on every link line, is emptied.
if(MSVC)
  set(CMAKE_C_STANDARD_LIBRARIES "")
  set(CMAKE_CXX_STANDARD_LIBRARIES "")
  target_link_options(qiven_build INTERFACE /WX /STACK:1048576)
elseif(APPLE)
  target_link_options(qiven_build INTERFACE LINKER:-fatal_warnings)
else()
  target_link_options(qiven_build INTERFACE LINKER:--fatal-warnings)
endif()
# On Linux the C++ runtime and gcc's own static, gcc's sanitizer runtimes
# too (clang's are by default); glibc stays shared. MemorySanitizer's
# libc++ and libc++abi are the preset's archives.
if(NOT MSVC AND NOT APPLE)
  add_link_options(-static-libgcc)
  if(NOT QIVEN_SANITIZE STREQUAL "memory")
    add_link_options(-static-libstdc++)
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach(sanitizer IN ITEMS address undefined thread)
      if(QIVEN_SANITIZE MATCHES "${sanitizer}")
        string(REPLACE "address" "asan" lib "${sanitizer}")
        string(REPLACE "undefined" "ubsan" lib "${lib}")
        string(REPLACE "thread" "tsan" lib "${lib}")
        add_link_options("-static-lib${lib}")
      endif()
    endforeach()
  endif()
endif()

# Configure reads __cplusplus itself, under the build's flags: a /std: the
# compiler does not know only warns and leaves an older language.
include(CheckCXXSourceCompiles)
list(JOIN qiven_cxx_flags " " CMAKE_REQUIRED_FLAGS)
unset(QIVEN_CPLUSPLUS_20 CACHE)
check_cxx_source_compiles("
#if __cplusplus < 202002L
#error __cplusplus is below 202002
#endif
int main() { return 0; }" QIVEN_CPLUSPLUS_20)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT QIVEN_CPLUSPLUS_20)
  message(FATAL_ERROR "qiven3: __cplusplus is below 202002 under the build's flags")
endif()

# The sanitizers, libFuzzer's main on the fuzz targets alone.
if(QIVEN_SANITIZE)
  include("${CMAKE_CURRENT_LIST_DIR}/sanitize.cmake")
endif()

# The standard library's hardened mode, in every build: an index past a
# span's or a container's end stops the process.
if(MSVC)
  add_compile_definitions(_MSVC_STL_HARDENING=1)
else()
  add_compile_definitions(_GLIBCXX_ASSERTIONS
    _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE)
endif()

# The floors, chosen stable releases: Windows 11 24H2, Ubuntu 24.04, macOS
# 15. The product needs no system feature past them.
# - Windows: every target at 24H2's target-version macros, which the SDK's
#   sdkddkver.h names NTDDI_WIN11_GE and _WIN32_WINNT_WIN10, so that a newer
#   SDK hides what it declares past 24H2. Configure reads them under the
#   build's flags, each set before sdkddkver.h, since its defaults may
#   match; tests/structure/test_floors.py holds every compile command and
#   source to them.
# - Linux: no binary needs a glibc symbol version past GLIBC_2.39, which K11
#   reads (tests/structure/test_binaries.py).
# - macOS: the deployment target 15.0, from the macos-clang preset, and a
#   call newer than it an error.
if(WIN32)
  add_compile_definitions(NTDDI_VERSION=0x0A000010 _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  get_directory_property(qiven_definitions COMPILE_DEFINITIONS)
  list(FILTER qiven_definitions EXCLUDE REGEX "[$]<")
  list(TRANSFORM qiven_definitions PREPEND "-D")
  set(CMAKE_REQUIRED_DEFINITIONS ${qiven_definitions})
  list(JOIN qiven_cxx_flags " " CMAKE_REQUIRED_FLAGS)
  unset(QIVEN_WINDOWS_FLOOR CACHE)
  check_cxx_source_compiles("
#if !defined(NTDDI_VERSION) || !defined(_WIN32_WINNT) || !defined(WINVER)
#error the floor's macros are not all set before sdkddkver.h
#endif
#include <sdkddkver.h>
#if !defined(NTDDI_WIN11_GE) || NTDDI_VERSION != NTDDI_WIN11_GE || NTDDI_VERSION != 0x0A000010
#error NTDDI_VERSION is not NTDDI_WIN11_GE, 0x0A000010
#endif
#if _WIN32_WINNT != _WIN32_WINNT_WIN10 || _WIN32_WINNT != 0x0A00 || WINVER != _WIN32_WINNT_WIN10
#error _WIN32_WINNT and WINVER are not _WIN32_WINNT_WIN10, 0x0A00
#endif
int main() { return 0; }" QIVEN_WINDOWS_FLOOR)
  unset(CMAKE_REQUIRED_DEFINITIONS)
  unset(CMAKE_REQUIRED_FLAGS)
  if(NOT QIVEN_WINDOWS_FLOOR)
    message(FATAL_ERROR "qiven3: the build's flags do not set NTDDI_VERSION 0x0A000010 (NTDDI_WIN11_GE) "
      "and _WIN32_WINNT and WINVER 0x0A00 (_WIN32_WINNT_WIN10), Windows 11 24H2's floor")
  endif()
elseif(APPLE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "15.0")
    message(FATAL_ERROR "qiven3: CMAKE_OSX_DEPLOYMENT_TARGET is '${CMAKE_OSX_DEPLOYMENT_TARGET}', "
      "not macOS 15.0's floor")
  endif()
  add_compile_options(-Wunguarded-availability -Werror=unguarded-availability)
endif()

# Clean binaries: no source, toolset or SDK path of the building machine in
# an object, a library or a binary, and the same bytes from the same
# sources. Each path comes from configure. cl ignores /pathmap without
# /experimental:deterministic (D9007), and mapping the source alone leaves
# the toolset's install path in the object. The linker maps no path: its
# own record in each binary's PDB keeps them, a residual.
if(MSVC)
  get_filename_component(qiven_toolset_dir "${CMAKE_CXX_COMPILER}/../../../.." ABSOLUTE)
  set(qiven_path_maps "${PROJECT_SOURCE_DIR}=." "${qiven_toolset_dir}=msvc")
  if(DEFINED ENV{WindowsSdkDir})
    file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" qiven_sdk_dir)
    string(REGEX REPLACE "/+$" "" qiven_sdk_dir "${qiven_sdk_dir}")
    list(APPEND qiven_path_maps "${qiven_sdk_dir}=sdk")
  else()
    message(WARNING "qiven3: WindowsSdkDir is unset: the SDK's paths stay unmapped")
  endif()
  add_compile_options(/experimental:deterministic /Brepro)
  foreach(map IN LISTS qiven_path_maps)
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
  get_filename_component(qiven_toolchain_dir "${CMAKE_CXX_COMPILER}/../.." ABSOLUTE)
  if(qiven_toolchain_dir STREQUAL "/")
    set(qiven_toolchain_dir "/usr")
  endif()
  set(qiven_path_maps "${qiven_toolchain_dir}=toolchain")
  if(IS_ABSOLUTE "${CMAKE_OSX_SYSROOT}")
    list(APPEND qiven_path_maps "${CMAKE_OSX_SYSROOT}=sdk")
  endif()
  list(APPEND qiven_path_maps "${PROJECT_SOURCE_DIR}=.")
  foreach(map IN LISTS qiven_path_maps)
    add_compile_options("-ffile-prefix-map=${map}")
  endforeach()
endif()
list(JOIN qiven_path_maps ", " qiven_maps_text)
message(STATUS "qiven3: paths mapped: ${qiven_maps_text}")

# Static analysis of v3's own sources, never third_party/, every finding an
# error under /WX. stack: C6262 alone, a frame past 4,096 bytes. analysis:
# the Core Guidelines checker and its lifetime rules, then clang-tidy over
# compile_commands.json (below). The rule sets list each rule turned off
# with its reason.
if(QIVEN_ANALYZE AND NOT MSVC)
  message(FATAL_ERROR "qiven3: QIVEN_ANALYZE is MSVC's; gcc and clang check frames by -Wframe-larger-than")
endif()
if(QIVEN_ANALYZE STREQUAL "stack")
  target_compile_options(qiven_build INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:/analyze;/analyze:stacksize4096;/analyze:ruleset${CMAKE_CURRENT_LIST_DIR}/stack.ruleset>")
elseif(QIVEN_ANALYZE STREQUAL "analysis")
  # The rule set includes Visual Studio's own by name, found in its directory.
  # Each /analyze option is one token, its value attached, so that the
  # clang-tidy step can drop them (below).
  file(TO_CMAKE_PATH "$ENV{VSINSTALLDIR}" qiven_vs_dir)
  set(qiven_rule_sets "${qiven_vs_dir}/Team Tools/Static Analysis Tools/Rule Sets")
  if(NOT EXISTS "${qiven_rule_sets}/CppCoreCheckRules.ruleset")
    message(FATAL_ERROR "qiven3: no CppCoreCheckRules.ruleset under VSINSTALLDIR; configure from the toolset's environment")
  endif()
  # On the targets of src/ and tests/support/ alone (below).
  set(qiven_analysis_options /analyze /analyze:pluginEspXEngine.dll
    "/analyze:ruleset${CMAKE_CURRENT_LIST_DIR}/analysis.ruleset"
    "/analyze:rulesetdirectory${qiven_rule_sets}" /analyze:external- /external:anglebrackets /external:W0)
  find_program(QIVEN_CLANG_TIDY clang-tidy HINTS "$ENV{ProgramFiles}/LLVM/bin" REQUIRED)
  execute_process(COMMAND "${QIVEN_CLANG_TIDY}" --version OUTPUT_VARIABLE qiven_tidy_version)
  string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" qiven_tidy_version "${qiven_tidy_version}")
  message(STATUS "qiven3: clang-tidy ${qiven_tidy_version}")
elseif(QIVEN_ANALYZE)
  message(FATAL_ERROR "qiven3: QIVEN_ANALYZE is '${QIVEN_ANALYZE}'; it is empty, stack or analysis")
endif()

# Both options change the memory package's types, so every target sees them;
# QIVEN_SYSTEM_HEAP all but the targets with QIVEN3_POISONED set, memory's
# own code and cases built a second time on the span's own blocks
# (tests/unit/CMakeLists.txt).
if(QIVEN_SYSTEM_HEAP)
  add_compile_definitions($<$<NOT:$<BOOL:$<TARGET_PROPERTY:QIVEN3_POISONED>>>:QIVEN_SYSTEM_HEAP>)
endif()
if(QIVEN_FAULTS)
  add_compile_definitions(QIVEN_FAULTS)
endif()
# The log's minimum, one for every target, so that each sees one catalogue;
# the tests read QIVEN_LOG_LEVEL from the cache (K13).
if(QIVEN_LOG_MIN STREQUAL "")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(qiven_log_level trace)
  else()
    set(qiven_log_level info)
  endif()
elseif(QIVEN_LOG_MIN MATCHES "^(trace|debug|info|warn|error)$")
  set(qiven_log_level "${QIVEN_LOG_MIN}")
else()
  message(FATAL_ERROR "qiven3: QIVEN_LOG_MIN is '${QIVEN_LOG_MIN}'; it is empty, trace, debug, info, warn or error")
endif()
set(QIVEN_LOG_LEVEL "${qiven_log_level}" CACHE INTERNAL "The log's minimum level in effect")
add_compile_definitions(QIVEN_LOG_MIN=${qiven_log_level})

message(STATUS "qiven3: QIVEN_HAL ${QIVEN_HAL}, QIVEN3_STRUCTURE ${QIVEN3_STRUCTURE}, "
  "QIVEN_SYSTEM_HEAP ${QIVEN_SYSTEM_HEAP}, QIVEN_FAULTS ${QIVEN_FAULTS}")

# Every binary in one directory: the tests find each there, and the
# sanitizer's runtime sits beside them all.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

# qiven_executable(<name> SOURCES <source>... [LINK <target>...] [DIAG <suffix>] [WIDE])
# Every executable of the build, and the one add_executable: its sources
# with the diagnostic entry's object, linked with qiven3_diag and LINK, the
# entry point qiven3_entry on Windows, and the binary listed in the build's
# binaries.txt. DIAG names a variant of the entry's targets
# (src/diag/CMakeLists.txt), for a binary on another runtime or memory.
# WIDE, for a binary whose entry is wmain on Windows (the product's), takes
# the entry's wide variant there.
# Each library is on the link line once (tests/structure/test_links.py):
# CMake keeps a binary's own list in its order, puts what they link after
# it, and names a library again when one after it links it. So LINK names
# a target before none that links it, and qiven3_diag, last, follows each
# that links it: a binary whose targets bring the runner names it.
function(qiven_executable name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "WIDE" "DIAG" "SOURCES;LINK")
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "qiven_executable(${name}): no SOURCES")
  endif()
  set(entry "qiven3_diag_entry${arg_DIAG}")
  if(arg_WIDE AND WIN32)
    set(entry "qiven3_diag_entry_wide")
  endif()
  add_executable(${name} ${arg_SOURCES} $<TARGET_OBJECTS:${entry}>)
  target_link_libraries(${name} PRIVATE qiven_build ${arg_LINK} qiven3_diag${arg_DIAG})
  if(MSVC)
    target_link_options(${name} PRIVATE /ENTRY:qiven3_entry)
  endif()
  set_property(GLOBAL APPEND PROPERTY QIVEN_BINARIES ${name})
endfunction()
