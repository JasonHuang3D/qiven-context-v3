# qiven3_base: the format's names and versions, then canon, json, utf, utc
# and digest as their packages arrive.
add_library(qiven3_base STATIC
  canon.cpp
  canon.hpp
  digest.cpp
  digest.hpp
  json.cpp
  json.hpp
  json_bytes.hpp
  qiven3.cpp
  qiven3.hpp
  utc.hpp
  utf.cpp
  utf.hpp
)
target_include_directories(qiven3_base PUBLIC "${PROJECT_SOURCE_DIR}/src")
# digest's keys are templates on the platform's contract, its header alone:
# a binary that makes a key links its adapter.
target_link_libraries(qiven3_base PUBLIC qiven3_memory PRIVATE qiven3_flags)

# qiven3_location: the human's choice of the store's directory, the record,
# the drive check and the commands, apart from qiven3_base so that B12 and
# B16 hold by the build's edges; its commands are templates on the
# platform, instantiated by a binary's main and by its cases on the fake.
add_library(qiven3_location STATIC
  location.cpp
  location.hpp
)
target_include_directories(qiven3_location PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_location
  PUBLIC qiven3_store qiven3_entry qiven3_platform qiven3_base qiven3_memory
  PRIVATE qiven3_flags)
