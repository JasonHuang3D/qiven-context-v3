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
