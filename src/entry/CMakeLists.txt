# qiven3_entry: the entry's modules, header and body first; its syntax
# checks link the matcher. Cognition's record log values (cognition.hpp)
# are in the entry's own directory, reached by its include directory.
add_library(qiven3_entry STATIC
  "${PROJECT_SOURCE_DIR}/products/qiven3/entry/include/qiven3/entry/cognition.hpp"
  body.cpp
  body.hpp
  check.cpp
  check.hpp
  content.cpp
  content.hpp
  content_fields.cpp
  content_fields.hpp
  fold.cpp
  fold.hpp
  header.cpp
  header.hpp
  kinds.cpp
  kinds.hpp
  kinds_identity.hpp
  labels.cpp
  labels.hpp
  projectmap.cpp
  projectmap.hpp
  projectmap_files.hpp
  refs.cpp
  refs.hpp
  served.cpp
  served.hpp
  served_text.hpp
  validator.cpp
  validator.hpp
)
target_include_directories(qiven3_entry PUBLIC "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/products/qiven3/entry/include")
target_link_libraries(qiven3_entry PUBLIC qiven_recordlog_keys qiven_matcher qiven_text qiven_time qiven_digest qiven3_format
  qiven_memory PRIVATE qiven_build)

# qiven3_secret: the secret check, apart from the entry's modules so that
# the reader links none of it (B13); it links the matcher and the entry.
add_library(qiven3_secret STATIC
  secret.cpp
  secret.hpp
)
target_include_directories(qiven3_secret PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_secret PUBLIC qiven3_entry qiven_matcher qiven_text qiven_memory
  PRIVATE qiven_build)
