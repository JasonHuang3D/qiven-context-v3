# The unit cases of v3's memory: memory, arena, pool and its frees, each case
# its own CTest test; the cases of utf and json, with unit/constant's
# vectors, which the compiler evaluates; fail_fast's demo, which
# tests/drive/test_fail.py runs; and the frees that end the process, which
# tests/drive/test_free.py runs.
qiven3_add_cases(qiven3_unit_memory LABELS fast fake
  SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp
  LINK qiven3_memory qiven3_faults)

qiven3_add_cases(qiven3_unit_text LABELS fast fake
  SOURCES utf_test.cpp json_test.cpp json_memory_test.cpp constant_test.cpp
  LINK qiven3_base qiven3_faults)

qiven3_executable(qiven3_fail_demo SOURCES fail_demo.cpp LINK qiven3_runner qiven3_memory)

# The frees that end the process, which tests/drive/test_free.py runs.
qiven3_executable(qiven3_free_demo SOURCES free_demo.cpp LINK qiven3_runner qiven3_memory)

# Under AddressSanitizer with QIVEN3_SYSTEM_HEAP, memory's own code, the
# runner over it, memory's cases and the frees' demo are built a second time
# without the option: the blocks are the span's own, which the allocators
# poison but for what they hand out (memory/poison.hpp), so the code that
# places a block runs under the sanitizer. tests/drive/test_free.py runs
# both binaries; every other target keeps the system heap.
if(QIVEN3_SYSTEM_HEAP AND QIVEN3_SANITIZE MATCHES "address")
  get_target_property(qiven3_memory_sources qiven3_memory SOURCES)
  list(TRANSFORM qiven3_memory_sources PREPEND "${PROJECT_SOURCE_DIR}/src/memory/")
  add_library(qiven3_memory_poisoned STATIC ${qiven3_memory_sources})
  target_include_directories(qiven3_memory_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(qiven3_memory_poisoned PRIVATE qiven3_flags)

  add_library(qiven3_runner_poisoned STATIC "${PROJECT_SOURCE_DIR}/tests/runner/runner.cpp")
  target_include_directories(qiven3_runner_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/tests")
  target_link_libraries(qiven3_runner_poisoned PUBLIC qiven3_memory_poisoned PRIVATE qiven3_flags)

  # The entry's own targets over the poisoned memory, so that one copy of
  # memory is linked.
  qiven3_diag_targets(_poisoned qiven3_memory_poisoned)
  qiven3_executable(qiven3_unit_memory_poisoned
    SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  qiven3_executable(qiven3_free_demo_poisoned SOURCES free_demo.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  set_target_properties(qiven3_memory_poisoned qiven3_runner_poisoned qiven3_unit_memory_poisoned
    qiven3_free_demo_poisoned qiven3_diag_poisoned qiven3_diag_entry_poisoned PROPERTIES QIVEN3_POISONED ON)
endif()
