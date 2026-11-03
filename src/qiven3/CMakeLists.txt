# qiven3_base: the format's names and versions, and digest, each until it
# moves to its own module.
add_library(qiven3_base STATIC
  digest.cpp
  digest.hpp
  qiven3.cpp
  qiven3.hpp
)
# digest's keys are templates on the platform's contract, its header alone,
# reached by the contract's include directory: a binary that makes a key
# links its adapter.
target_include_directories(qiven3_base PUBLIC "${PROJECT_SOURCE_DIR}/src"
  "$<TARGET_PROPERTY:qiven_platform,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(qiven3_base PUBLIC qiven_memory PRIVATE qiven_build)

# qiven3_location: the human's choice of the store's directory, the record,
# the drive check and the commands, apart from qiven3_base so that B12 and
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
  PUBLIC qiven3_store qiven3_entry qiven_platform qiven3_wire qiven_text qiven_time qiven3_base qiven_memory
  PRIVATE qiven_build)
