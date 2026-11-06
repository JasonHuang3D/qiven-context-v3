# qiven_memory: the foundation's memory, which every target of src/ links and
# which links nothing of v3's: the arena, the pool and Heap, the allocator and
# its containers, the budget's error, the lock, checked sizes and fail_fast.
# Its tests (tests/) are added by harness' wiring.cmake.
add_library(qiven_memory STATIC
  src/arena.cpp
  include/qiven/memory/arena.hpp
  src/fail.cpp
  include/qiven/memory/fail.hpp
  src/memory.cpp
  include/qiven/memory/memory.hpp
  src/poison.hpp
  src/pool.cpp
  include/qiven/memory/pool.hpp
)
target_include_directories(qiven_memory PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_memory PRIVATE qiven_build)
