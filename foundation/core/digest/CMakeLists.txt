# qiven_digest: the foundation's digests, over memory alone: SHA-256,
# HMAC-SHA-256, the plain and keyed digests' text, and the comparison
# whose time does not depend on where two texts differ; the plain digest's
# instantiation over memory's string. Its tests are added by
# the runner's wiring.cmake, where the case functions and qiven_case_main are
# made.
add_library(qiven_digest STATIC
  src/digest.cpp
  include/qiven/digest/digest.hpp
)
target_include_directories(qiven_digest PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_digest PUBLIC qiven_memory PRIVATE qiven_build)
