# qiven3-fault, the diagnostic entry's planted faults (fault.cpp), which
# tests/drive/test_faults.py runs; never a CTest test itself.
qiven3_executable(qiven3-fault SOURCES fault.cpp fault_assert.cpp LINK qiven3_memory)

# qiven3-fault-crtdbg: the same plants on the debug runtime (/MTd, _DEBUG)
# in every Windows build, with memory and the entry built again on it, for
# the debug build's assertion report. Under a sanitizer, whose report
# stands for the entry's, the cases are skipped and the binary not built.
if(MSVC AND NOT QIVEN3_SANITIZE)
  get_target_property(qiven3_memory_sources qiven3_memory SOURCES)
  list(TRANSFORM qiven3_memory_sources PREPEND "${PROJECT_SOURCE_DIR}/src/memory/")
  add_library(qiven3_memory_crtdbg STATIC ${qiven3_memory_sources})
  target_include_directories(qiven3_memory_crtdbg PUBLIC "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(qiven3_memory_crtdbg PRIVATE qiven3_flags)
  qiven3_diag_targets(_crtdbg qiven3_memory_crtdbg)
  qiven3_executable(qiven3-fault-crtdbg SOURCES fault.cpp fault_assert.cpp LINK qiven3_memory_crtdbg
    DIAG _crtdbg)
  foreach(target IN ITEMS qiven3_memory_crtdbg qiven3_diag_crtdbg qiven3_diag_entry_crtdbg qiven3-fault-crtdbg)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDebug)
    target_compile_definitions(${target} PRIVATE _DEBUG)
  endforeach()
endif()

# qiven3-fault-import and its library, the test tree's own (K6, K11): the
# library beside every binary, and no run path in the binary, so that a
# copy of it alone finds no library.
add_library(qiven3_fault_lib SHARED fault_lib.cpp)
target_link_libraries(qiven3_fault_lib PRIVATE qiven3_flags)
# MSVC's /fsanitize=fuzzer brings libFuzzer's main, which a library lacks
# the input function for: the library is built without it.
if(MSVC AND QIVEN3_SANITIZE MATCHES "fuzzer")
  get_target_property(qiven3_fault_lib_options qiven3_fault_lib COMPILE_OPTIONS)
  list(REMOVE_ITEM qiven3_fault_lib_options "/fsanitize=fuzzer")
  set_target_properties(qiven3_fault_lib PROPERTIES COMPILE_OPTIONS "${qiven3_fault_lib_options}")
endif()
set_target_properties(qiven3_fault_lib PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
qiven3_executable(qiven3-fault-import SOURCES fault_import.cpp LINK qiven3_fault_lib)
set_target_properties(qiven3-fault-import PROPERTIES SKIP_BUILD_RPATH ON)
