# qiven3_memory: v3's own memory, which every target of src/ links and which
# links nothing of v3's: the arena, the pool and Heap, the allocator and its
# containers, the budget's error, the lock, checked sizes and fail_fast.
add_library(qiven3_memory STATIC
  arena.cpp
  arena.hpp
  fail.cpp
  fail.hpp
  memory.cpp
  memory.hpp
  pool.cpp
  pool.hpp
)
target_include_directories(qiven3_memory PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_memory PRIVATE qiven3_flags)
