# qiven3_format: the product's format: its names and versions (qiven3.hpp),
# an entry's framing and id over digest's SHA-256 (entry_id.hpp), and Key
# with its functions (key.hpp), templates on the platform's contract, its
# header alone, reached by the contract's include directory: a binary that
# makes a key links its adapter.
add_library(qiven3_format STATIC
  src/qiven3.cpp
  include/qiven3/format/qiven3.hpp
  include/qiven3/format/entry_id.hpp
  include/qiven3/format/key.hpp
)
target_include_directories(qiven3_format PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "$<TARGET_PROPERTY:qiven_platform,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(qiven3_format PUBLIC qiven_digest qiven_memory PRIVATE qiven_build)
