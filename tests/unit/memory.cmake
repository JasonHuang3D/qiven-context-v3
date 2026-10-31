# The unit cases of v3's memory: memory, arena, pool and its frees, each case
# its own CTest test; the cases of utf, json and canon, with unit/constant's
# vectors, which the compiler evaluates; fail_fast's demo, which
# tests/drive/test_fail.py runs; and the frees that end the process, which
# tests/drive/test_free.py runs.
qiven3_add_cases(qiven3_unit_memory LABELS fast fake
  SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp
  LINK qiven3_faults)

qiven_executable(qiven3_fail_demo SOURCES fail_demo.cpp LINK qiven3_case_main)

# The frees that end the process, which tests/drive/test_free.py runs.
qiven_executable(qiven3_free_demo SOURCES free_demo.cpp LINK qiven3_case_main)

# Under AddressSanitizer with QIVEN_SYSTEM_HEAP, memory's own code, the
# runner over it, memory's cases and the frees' demo are built a second time
# without the option: the blocks are the span's own, which the allocators
# poison but for what they hand out (memory/poison.hpp), so the code that
# places a block runs under the sanitizer. tests/drive/test_free.py runs
# both binaries; every other target keeps the system heap.
if(QIVEN_SYSTEM_HEAP AND QIVEN_SANITIZE MATCHES "address")
  get_target_property(qiven3_memory_sources qiven3_memory SOURCES)
  list(TRANSFORM qiven3_memory_sources PREPEND "${PROJECT_SOURCE_DIR}/src/memory/")
  add_library(qiven3_memory_poisoned STATIC ${qiven3_memory_sources})
  target_include_directories(qiven3_memory_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(qiven3_memory_poisoned PRIVATE qiven_build)

  # The entry's targets, the base and the log the runner's log takes, and
  # the platform whose codes it names, over the poisoned memory, so that one
  # copy of memory is linked.
  qiven3_diag_targets(_poisoned qiven3_memory_poisoned)
  foreach(pair IN ITEMS "qiven3_base;qiven3;qiven3_memory_poisoned"
                        "qiven3_log;log;qiven3_base_poisoned;qiven3_memory_poisoned"
                        "qiven3_platform;platform;qiven3_memory_poisoned")
    list(POP_FRONT pair name dir)
    get_target_property(sources ${name} SOURCES)
    list(TRANSFORM sources PREPEND "${PROJECT_SOURCE_DIR}/src/${dir}/")
    add_library(${name}_poisoned STATIC ${sources})
    target_include_directories(${name}_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${name}_poisoned PUBLIC ${pair} PRIVATE qiven_build)
  endforeach()

  add_library(qiven3_runner_poisoned STATIC "${PROJECT_SOURCE_DIR}/tests/runner/runner.cpp"
    "${PROJECT_SOURCE_DIR}/tests/support/log/main.cpp")
  target_include_directories(qiven3_runner_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/tests")
  target_link_libraries(qiven3_runner_poisoned PUBLIC qiven3_memory_poisoned qiven3_log_poisoned
    PRIVATE qiven_build qiven3_diag_poisoned qiven3_platform_poisoned)
  qiven_executable(qiven3_unit_memory_poisoned
    SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  qiven_executable(qiven3_free_demo_poisoned SOURCES free_demo.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  set_target_properties(qiven3_memory_poisoned qiven3_base_poisoned qiven3_log_poisoned qiven3_platform_poisoned
    qiven3_runner_poisoned qiven3_unit_memory_poisoned
    qiven3_free_demo_poisoned qiven3_diag_poisoned qiven3_diag_entry_poisoned PROPERTIES QIVEN3_POISONED ON)
endif()
