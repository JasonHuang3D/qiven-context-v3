# Memory's own tests: the cases of memory, arena, pool and its frees, each
# case its own CTest test; fail_fast's demo, which test_fail.py runs; and the
# frees that end the process, which test_free.py runs. Added by
# tests/CMakeLists.txt, where qiven3_add_cases, qiven_faults and
# qiven_case_main are made.
qiven3_add_cases(qiven_memory_test LABELS fast fake
  SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp
  LINK qiven_faults)

qiven_executable(qiven_memory_fail_demo SOURCES fail_demo.cpp LINK qiven_case_main)

# The frees that end the process, which test_free.py runs.
qiven_executable(qiven_memory_free_demo SOURCES free_demo.cpp LINK qiven_case_main)

# Under AddressSanitizer with QIVEN_SYSTEM_HEAP, memory's own code, the
# runner over it, memory's cases and the frees' demo are built a second time
# without the option: the blocks are the span's own, which the allocators
# poison but for what they hand out (qiven/memory/poison.hpp), so the code
# that places a block runs under the sanitizer. test_free.py runs both
# binaries; every other target keeps the system heap.
if(QIVEN_SYSTEM_HEAP AND QIVEN_SANITIZE MATCHES "address")
  get_target_property(qiven_memory_sources qiven_memory SOURCES)
  get_target_property(qiven_memory_dir qiven_memory SOURCE_DIR)
  list(TRANSFORM qiven_memory_sources PREPEND "${qiven_memory_dir}/")
  add_library(qiven_memory_poisoned STATIC ${qiven_memory_sources})
  target_include_directories(qiven_memory_poisoned PUBLIC "${qiven_memory_dir}/include")
  target_link_libraries(qiven_memory_poisoned PRIVATE qiven_build)

  # The entry's targets, the base and the log the runner's log takes, and
  # the platform whose codes it names, over the poisoned memory, so that one
  # copy of memory is linked.
  qiven3_diag_targets(_poisoned qiven_memory_poisoned)
  foreach(pair IN ITEMS "qiven3_base;qiven3;qiven_memory_poisoned"
                        "qiven3_log;log;qiven3_base_poisoned;qiven_memory_poisoned"
                        "qiven3_platform;platform;qiven_memory_poisoned")
    list(POP_FRONT pair name dir)
    get_target_property(sources ${name} SOURCES)
    list(TRANSFORM sources PREPEND "${PROJECT_SOURCE_DIR}/src/${dir}/")
    add_library(${name}_poisoned STATIC ${sources})
    target_include_directories(${name}_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(${name}_poisoned PUBLIC ${pair} PRIVATE qiven_build)
  endforeach()

  add_library(qiven3_runner_poisoned STATIC "${PROJECT_SOURCE_DIR}/foundation/devkit/test/runner/src/runner.cpp"
    "${PROJECT_SOURCE_DIR}/foundation/devkit/test/case_main/src/main.cpp")
  target_include_directories(qiven3_runner_poisoned PUBLIC "${PROJECT_SOURCE_DIR}/foundation/devkit/test/runner/include"
    "${PROJECT_SOURCE_DIR}/foundation/devkit/test/case_main/include")
  target_link_libraries(qiven3_runner_poisoned PUBLIC qiven_memory_poisoned qiven3_log_poisoned
    PRIVATE qiven_build qiven3_diag_poisoned qiven3_platform_poisoned)
  qiven_executable(qiven_memory_test_poisoned
    SOURCES memory_test.cpp arena_test.cpp pool_test.cpp free_test.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  # The faults' header by its path, as qiven_faults gives it, without that
  # target's second copy of memory.
  target_include_directories(qiven_memory_test_poisoned PRIVATE
    "${PROJECT_SOURCE_DIR}/foundation/devkit/test/faults/include")
  qiven_executable(qiven_memory_free_demo_poisoned SOURCES free_demo.cpp LINK qiven3_runner_poisoned DIAG _poisoned)
  set_target_properties(qiven_memory_poisoned qiven3_base_poisoned qiven3_log_poisoned qiven3_platform_poisoned
    qiven3_runner_poisoned qiven_memory_test_poisoned
    qiven_memory_free_demo_poisoned qiven3_diag_poisoned qiven3_diag_entry_poisoned PROPERTIES QIVEN3_POISONED ON)
endif()
