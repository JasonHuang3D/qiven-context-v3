# qiven3-fault, the diagnostic entry's planted faults (fault.cpp), which
# tests/drive/test_faults.py runs; never a CTest test itself. It fills a
# ring of its own and links the entry alone, memory through it.
qiven_executable(qiven3-fault SOURCES fault.cpp fault_assert.cpp)

# qiven3-fault-crtdbg: the same plants on the debug runtime (/MTd, _DEBUG)
# in every Windows build, with memory and the entry built again on it, for
# the debug build's assertion report. Under a sanitizer, whose report
# stands for the entry's, the cases are skipped and the binary not built.
if(MSVC AND NOT QIVEN_SANITIZE)
  get_target_property(qiven_memory_sources qiven_memory SOURCES)
  get_target_property(qiven_memory_dir qiven_memory SOURCE_DIR)
  list(TRANSFORM qiven_memory_sources PREPEND "${qiven_memory_dir}/")
  add_library(qiven_memory_crtdbg STATIC ${qiven_memory_sources})
  target_include_directories(qiven_memory_crtdbg PUBLIC "${qiven_memory_dir}/include")
  target_link_libraries(qiven_memory_crtdbg PRIVATE qiven_build)
  qiven_diag_targets(_crtdbg qiven_memory_crtdbg)
  qiven_executable(qiven3-fault-crtdbg SOURCES fault.cpp fault_assert.cpp DIAG _crtdbg)
  foreach(target IN ITEMS qiven_memory_crtdbg qiven_diag_crtdbg qiven_diag_entry_crtdbg qiven3-fault-crtdbg)
    set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDebug)
    target_compile_definitions(${target} PRIVATE _DEBUG)
  endforeach()
endif()

# qiven3-fault-import and its library, the test tree's own (K6, K11): the
# library beside every binary, and no run path in the binary, so that a
# copy of it alone finds no library.
add_library(qiven3_fault_lib SHARED fault_lib.cpp)
target_link_libraries(qiven3_fault_lib PRIVATE qiven_build)
set_target_properties(qiven3_fault_lib PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
qiven_executable(qiven3-fault-import SOURCES fault_import.cpp LINK qiven3_fault_lib)
set_target_properties(qiven3-fault-import PROPERTIES SKIP_BUILD_RPATH ON)
