# qiven3_base: the format's names and versions, then canon, json, utf, utc
# and digest as their packages arrive.
add_library(qiven3_base STATIC
  qiven3.cpp
  qiven3.hpp
)
target_include_directories(qiven3_base PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_base PUBLIC qiven3_memory PRIVATE qiven3_flags)
