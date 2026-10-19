# qiven3_runner: the test runner and the main of every case binary.
add_library(qiven3_runner STATIC
  runner.cpp
  runner.hpp
)
target_include_directories(qiven3_runner PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_runner PRIVATE qiven3_flags)
if(WIN32)
  target_compile_definitions(qiven3_runner PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# The runner's own cases, each a CTest test.
qiven3_add_cases(qiven3_runner_test LABELS fast SOURCES runner_test.cpp)

# One case per route, run as processes by tests/drive/test_runner.py; never
# a CTest test itself.
add_executable(qiven3_runner_demo runner_demo.cpp)
target_link_libraries(qiven3_runner_demo PRIVATE qiven3_runner qiven3_flags)
