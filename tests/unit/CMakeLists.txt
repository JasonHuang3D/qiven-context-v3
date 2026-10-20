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

add_executable(qiven3_fail_demo fail_demo.cpp)
target_link_libraries(qiven3_fail_demo PRIVATE qiven3_runner qiven3_flags qiven3_memory)

# The frees that end the process, which tests/drive/test_free.py runs.
add_executable(qiven3_free_demo free_demo.cpp)
target_link_libraries(qiven3_free_demo PRIVATE qiven3_runner qiven3_flags qiven3_memory)
