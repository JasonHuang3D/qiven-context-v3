# qiven3-child: the processes the tests start by subprocess, one binary,
# each subcommand a row of main.cpp's table: probe, for the differential
# tests (drive/test_differential.py, drive/test_refs.py), over the matcher,
# the strict parser and refs' checks; init, P20's child half, which on
# Windows runs location's init in a child the Windows adapter starts, its
# wide entry by the adapter's run_utf8, location's main instantiated by
# instances.cpp: on Windows the child links location, and through the store
# the engine, as qiven3 does. The platform's own children are its program's
# (foundation/system/platform/tests/child/).
# Each library once (qiven_executable): location, on Windows, brings the
# entry, the matcher and text, and diag, linked last, brings memory.
if(WIN32)
  set(qiven3_child_links qiven3_location qiven_platform_windows)
else()
  set(qiven3_child_links qiven3_entry qiven_matcher qiven_text)
endif()
qiven_executable(qiven3-child SOURCES main.cpp child.hpp probe.cpp init.cpp instances.cpp
  LINK ${qiven3_child_links} WIDE)
target_include_directories(qiven3-child PRIVATE "${PROJECT_SOURCE_DIR}/tests")
if(WIN32)
  target_compile_definitions(qiven3-child PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
