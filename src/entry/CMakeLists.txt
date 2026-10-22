# qiven3_matcher: the patterns' and syntaxes' matcher alone, which the
# entry's syntax checks and the secret check link.
add_library(qiven3_matcher STATIC
  matcher.cpp
  matcher.hpp
  matcher_basic.hpp
  matcher_read.hpp
)
target_include_directories(qiven3_matcher PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_matcher PUBLIC qiven3_memory PRIVATE qiven3_flags)

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
  labels.cpp
  labels.hpp
  refs.cpp
  refs.hpp
  validator.cpp
  validator.hpp
)
target_include_directories(qiven3_entry PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_entry PUBLIC qiven3_matcher qiven3_base qiven3_memory PRIVATE qiven3_flags)

# qiven3_secret: the secret check, apart from the entry's modules so that
# the reader links none of it (B13); it links the matcher and the entry.
add_library(qiven3_secret STATIC
  secret.cpp
  secret.hpp
)
target_include_directories(qiven3_secret PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_secret PUBLIC qiven3_entry qiven3_matcher qiven3_base qiven3_memory PRIVATE qiven3_flags)
