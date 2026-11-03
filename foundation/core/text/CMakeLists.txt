# qiven_text: the foundation's text, over memory alone: strict UTF-8 and
# the parser's typed error (utf), the JSON value, scan and encode (json,
# json_bytes behind it), and the strict parser and canonical bytes over a
# key rule (canon); their instantiations over memory's allocator. Its tests
# (tests/) are added by tests/CMakeLists.txt, where the case functions and
# qiven_case_main are made.
add_library(qiven_text STATIC
  src/utf.cpp
  src/json.cpp
  src/canon.cpp
  include/qiven/text/utf.hpp
  include/qiven/text/json.hpp
  include/qiven/text/json_bytes.hpp
  include/qiven/text/canon.hpp
)
target_include_directories(qiven_text PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_text PUBLIC qiven_memory PRIVATE qiven_build)
