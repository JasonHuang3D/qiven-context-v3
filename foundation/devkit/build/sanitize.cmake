# Included by build.cmake: the sanitizers of QIVEN_SANITIZE, on every
# target. libFuzzer's main is linked into the fuzz targets alone, by
# qiven_fuzz_main (below, which tests/fuzz/CMakeLists.txt calls); every other
# target takes libFuzzer's instrumentation without it.
if(MSVC)
  # cl ignores a combined /fsanitize=fuzzer,address with D9002, exit 0 even
  # under /WX: one option per sanitizer. /fsanitize=fuzzer makes each object
  # name libFuzzer's library and main, so that a binary on wmain takes
  # libFuzzer's main and fails to link; the coverage
  # options it implies stand for it, and the fuzz targets take it alone.
  # The counters' runtime, which /fsanitize=fuzzer would name, is named to
  # every link: sancov.lib on the DLL runtime libFuzzer needs.
  set(qiven_fuzz_coverage /fsanitize-coverage=edge /fsanitize-coverage=inline-8bit-counters
    /fsanitize-coverage=trace-cmp /fsanitize-coverage=trace-div)
  string(REPLACE "," ";" qiven_sanitizers "${QIVEN_SANITIZE}")
  foreach(sanitizer IN LISTS qiven_sanitizers)
    if(sanitizer STREQUAL "fuzzer")
      add_compile_options(${qiven_fuzz_coverage})
      add_link_options("/DEFAULTLIB:sancov$<$<CONFIG:Debug>:d>.lib")
    else()
      add_compile_options("/fsanitize=${sanitizer}")
    endif()
  endforeach()
  add_link_options(/INCREMENTAL:NO)
  # AddressSanitizer's runtime, a DLL beside the compiler, its debug form
  # under Debug: qiven_asan_runtime, its path, which the runner copies beside
  # each test binary (foundation/devkit/test/harness/cases.cmake).
  if(QIVEN_SANITIZE MATCHES "address")
    get_filename_component(qiven_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(qiven_asan_runtime "${qiven_cl_dir}/clang_rt.asan_dbg_dynamic-x86_64.dll")
    else()
      set(qiven_asan_runtime "${qiven_cl_dir}/clang_rt.asan_dynamic-x86_64.dll")
    endif()
    if(NOT EXISTS "${qiven_asan_runtime}")
      message(FATAL_ERROR "qiven: ${qiven_asan_runtime} is not beside the compiler")
    endif()
  endif()
else()
  string(REPLACE "fuzzer" "fuzzer-no-link" qiven_sanitizers "${QIVEN_SANITIZE}")
  add_compile_options("-fsanitize=${qiven_sanitizers}" -fno-sanitize-recover=all
    -fno-omit-frame-pointer)
  add_link_options("-fsanitize=${qiven_sanitizers}")
  # MemorySanitizer's report names where the unwritten bytes came from.
  if(QIVEN_SANITIZE STREQUAL "memory")
    add_compile_options(-fsanitize-memory-track-origins=2)
    add_link_options(-fsanitize-memory-track-origins=2)
  endif()
endif()
message(STATUS "qiven: sanitizers ${QIVEN_SANITIZE}")

# qiven_fuzz_main(<target>): libFuzzer's main on one fuzz target. MSVC's
# /fsanitize=fuzzer on the target's own objects names it to the link;
# elsewhere -fsanitize=fuzzer takes it at the link.
function(qiven_fuzz_main target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /fsanitize=fuzzer)
  else()
    target_link_options(${target} PRIVATE -fsanitize=fuzzer)
  endif()
endfunction()
