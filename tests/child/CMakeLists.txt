# qiven3-child: the processes the tests start by subprocess, one binary,
# each subcommand a row of main.cpp's table: probe, for the differential
# tests (drive/test_differential.py), over the matcher and the strict
# parser.
qiven3_executable(qiven3-child SOURCES main.cpp child.hpp probe.cpp
  LINK qiven3_matcher qiven3_base qiven3_memory)
target_include_directories(qiven3-child PRIVATE "${PROJECT_SOURCE_DIR}/tests")
