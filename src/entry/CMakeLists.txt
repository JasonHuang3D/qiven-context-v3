# qiven3_matcher: the patterns' and syntaxes' matcher alone, which the
# entry's syntax checks and the secret check link.
add_library(qiven3_matcher STATIC
  matcher.cpp
  matcher.hpp
  matcher_basic.hpp
  matcher_read.hpp
)
target_include_directories(qiven3_matcher PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_matcher PUBLIC qiven3_memory PRIVATE qiven3_flags)
