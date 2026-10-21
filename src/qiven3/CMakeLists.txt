# qiven3_base: the format's names and versions, then canon, json, utf, utc
# and digest as their packages arrive.
add_library(qiven3_base STATIC
  canon.cpp
  canon.hpp
  json.cpp
  json.hpp
  json_bytes.hpp
  qiven3.cpp
  qiven3.hpp
  utf.cpp
  utf.hpp
)
target_include_directories(qiven3_base PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_base PUBLIC qiven3_memory PRIVATE qiven3_flags)
