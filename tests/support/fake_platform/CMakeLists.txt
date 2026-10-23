# qiven3_platform_fake: the fake platform, on the C++ standard library alone,
# built on every system; its files' system calls under files' retries.
add_library(qiven3_platform_fake STATIC
  fake_files.cpp
  fake_files.hpp
  fake_platform.cpp
  fake_platform.hpp
)
target_include_directories(qiven3_platform_fake PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_platform_fake PUBLIC qiven3_platform PRIVATE qiven3_flags)
