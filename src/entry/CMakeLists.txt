# qiven3_entry: the entry's modules, header and body first; its syntax
# checks link the matcher.
add_library(qiven3_entry STATIC
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
target_include_directories(qiven3_entry PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_entry PUBLIC qiven3_keys qiven_matcher qiven_text qiven3_base qiven_memory
  PRIVATE qiven_build)

# qiven3_secret: the secret check, apart from the entry's modules so that
# the reader links none of it (B13); it links the matcher and the entry.
add_library(qiven3_secret STATIC
  secret.cpp
  secret.hpp
)
target_include_directories(qiven3_secret PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_secret PUBLIC qiven3_entry qiven_matcher qiven_text qiven3_base qiven_memory
  PRIVATE qiven_build)
