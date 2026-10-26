# qiven3-child: the processes the tests start by subprocess, one binary,
# each subcommand a row of main.cpp's table: probe, for the differential
# tests (drive/test_differential.py, drive/test_refs.py), over the matcher,
# the strict parser and refs' checks; platform, the process cases'
# children, which on Windows start a grandchild by the Windows adapter,
# whose run_utf8 gives the wide entry its arguments as UTF-8, and run
# location's init on it, instantiated by instances.cpp: on Windows the
# child links location, and through the store the engine, as qiven3 does.
# Each library once (qiven3_executable): location, on Windows, brings the
# entry, the matcher and base, and diag, linked last, brings memory.
if(WIN32)
  set(qiven3_child_links qiven3_location qiven3_platform_win)
else()
  set(qiven3_child_links qiven3_entry qiven3_matcher qiven3_base)
endif()
qiven3_executable(qiven3-child SOURCES main.cpp child.hpp probe.cpp platform.cpp instances.cpp
  LINK ${qiven3_child_links} WIDE)
target_include_directories(qiven3-child PRIVATE "${PROJECT_SOURCE_DIR}/tests")
if(WIN32)
  target_compile_definitions(qiven3-child PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
