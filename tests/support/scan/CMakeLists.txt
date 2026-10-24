# qiven3_scan: the raw-byte scans over a directory's files, on the
# standard library alone.
add_library(qiven3_scan STATIC
  scan.cpp
  scan.hpp
)
target_include_directories(qiven3_scan PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_scan PRIVATE qiven3_flags)
