# qiven_matcher: the foundation's linear matcher of a regular expression
# subset, over memory alone: compile into a position automaton, hit on a
# string; its instantiation over memory's allocator. Its tests (tests/) are
# added by the runner's wiring.cmake.
add_library(qiven_matcher STATIC
  src/matcher.cpp
  include/qiven/matcher/matcher.hpp
  include/qiven/matcher/basic.hpp
  include/qiven/matcher/read.hpp
)
target_include_directories(qiven_matcher PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_matcher PUBLIC qiven_memory PRIVATE qiven_build)
