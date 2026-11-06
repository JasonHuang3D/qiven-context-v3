# qiven3_location: the human's choice of the store's directory, the record,
# the drive check and the commands, a target of its own so that B12 and
# B16 hold by the build's edges; its commands are templates on the
# platform, declared in location.hpp and defined in location_impl.hpp, which
# instantiation files alone include: a binary's main's and its cases' on
# the fake.
add_library(qiven3_location STATIC
  location.cpp
  location.hpp
  location_impl.hpp
)
target_include_directories(qiven3_location PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_location
  PUBLIC qiven_recordlog qiven3_entry qiven_platform qiven_transport qiven_text qiven_time qiven_memory
  PRIVATE qiven_build)
