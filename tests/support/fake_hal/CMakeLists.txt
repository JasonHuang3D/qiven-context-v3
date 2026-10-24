# qiven3_hal_fake: the HAL's fake, on the C++ standard library alone, built
# on every system when QIVEN3_HAL is on.
add_library(qiven3_hal_fake STATIC
  fake_hal.cpp
  fake_hal.hpp
)
target_include_directories(qiven3_hal_fake PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_hal_fake PUBLIC qiven3_hal PRIVATE qiven3_flags)
