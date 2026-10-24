# qiven3_hal: the compute contract, which feature targets link; no core
# target includes or links it. Built only when QIVEN3_HAL is on.
add_library(qiven3_hal STATIC
  hal.cpp
  hal.hpp
)
target_include_directories(qiven3_hal PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_hal PUBLIC qiven3_memory PRIVATE qiven3_flags)
