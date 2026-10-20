# The unit cases of v3's memory: memory, arena and pool, each case its own
# CTest test; the cases of utf and json, with unit/constant's vectors, which
# the compiler evaluates; and fail_fast's demo, which tests/drive/test_fail.py
# runs.
qiven3_add_cases(qiven3_unit_memory LABELS fast fake
  SOURCES memory_test.cpp arena_test.cpp pool_test.cpp
  LINK qiven3_memory qiven3_faults)

qiven3_add_cases(qiven3_unit_text LABELS fast fake
  SOURCES utf_test.cpp json_test.cpp json_memory_test.cpp constant_test.cpp
  LINK qiven3_base qiven3_faults)

add_executable(qiven3_fail_demo fail_demo.cpp)
target_link_libraries(qiven3_fail_demo PRIVATE qiven3_runner qiven3_flags qiven3_memory)
