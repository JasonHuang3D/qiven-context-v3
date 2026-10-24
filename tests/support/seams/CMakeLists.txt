# qiven3_seams: the store's seams, hooks types and calls through its
# connection, and the naive file store, M5's and M6's negative control.
add_library(qiven3_seams STATIC
  seams.cpp
  seams.hpp
)
target_include_directories(qiven3_seams PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_seams PUBLIC qiven3_store PRIVATE qiven3_flags)
