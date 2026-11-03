# qiven_compute: the compute contract (CPU threads, vector instructions,
# GPGPU), over memory alone, which a caller's targets link; and
# qiven_compute_fake, its fake beside it, on the C++ standard library alone,
# built on every system. Added by the root CMakeLists.txt when QIVEN_HAL is
# on alone, as its tests (tests/) are by tests/CMakeLists.txt, where the
# case functions and qiven_case_main are made.
add_library(qiven_compute STATIC
  src/hal.cpp
  include/qiven/compute/hal.hpp
)
target_include_directories(qiven_compute PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_compute PUBLIC qiven_memory PRIVATE qiven_build)

add_library(qiven_compute_fake STATIC
  src/fake_hal.cpp
  include/qiven/compute/fake_hal.hpp
)
target_link_libraries(qiven_compute_fake PUBLIC qiven_compute PRIVATE qiven_build)
