# qiven_scan: the raw-byte scans over a directory's files, on the
# standard library alone.
add_library(qiven_scan STATIC
  src/scan.cpp
  include/qiven/scan/scan.hpp
)
target_include_directories(qiven_scan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_scan PRIVATE qiven_build)

# The scan's own cases, each a CTest test, with the log's main.
qiven_add_cases(qiven_scan_test LABELS fast SOURCES tests/scan_test.cpp LINK qiven_scan)
