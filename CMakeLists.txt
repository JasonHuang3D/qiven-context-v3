cmake_minimum_required(VERSION 3.28)
project(qiven3 LANGUAGES C CXX)

option(QIVEN3_PIN_TOOLS "Fail configure on any compiler, SDK, CMake, Ninja or interpreter but the pinned ones" OFF)
option(QIVEN3_HAL "Build qiven3_hal, its fake and its cases" ON)
option(QIVEN3_STRUCTURE "Write the records the structure tests read" OFF)
set(QIVEN3_SANITIZE "" CACHE STRING "Sanitizers for every target: empty, address, or fuzzer,address")
set_property(CACHE QIVEN3_SANITIZE PROPERTY STRINGS "" "address" "fuzzer,address")
set(QIVEN3_COMPILER_LINE "" CACHE STRING "When set, the compiler's major.minor line, for example 19.44")

# The standard is set through CMake, never by a raw /std: flag.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Headers, not named modules: no module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

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

# The flags of every core and test target, through qiven3_flags.
if(MSVC)
  set(qiven3_cxx_flags /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /EHsc)
else()
  set(qiven3_cxx_flags -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
    -Wshadow -Werror -finput-charset=UTF-8)
endif()
add_library(qiven3_flags INTERFACE)
target_compile_options(qiven3_flags INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:${qiven3_cxx_flags}>")
# The linker's warnings are errors too, on the same targets.
if(MSVC)
  target_link_options(qiven3_flags INTERFACE /WX)
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
    add_compile_options("-fsanitize=${QIVEN3_SANITIZE}")
    add_link_options("-fsanitize=${QIVEN3_SANITIZE}")
  endif()
  message(STATUS "qiven3: sanitizers ${QIVEN3_SANITIZE}")
endif()

message(STATUS "qiven3: QIVEN3_HAL ${QIVEN3_HAL}, QIVEN3_STRUCTURE ${QIVEN3_STRUCTURE}")

# Every binary in one directory: the tests find each there, and the
# sanitizer's runtime sits beside them all.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

enable_testing()
add_subdirectory(src/qiven3)
add_subdirectory(tests)
