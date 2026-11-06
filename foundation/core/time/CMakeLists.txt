# qiven_time: the foundation's time, utc: a time as milliseconds since 1970
# UTC, read from and printed as its `at`, and a date; a header alone, the
# standard library its one include. Its tests (tests/) are added by
# harness' wiring.cmake.
add_library(qiven_time INTERFACE)
target_include_directories(qiven_time INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
