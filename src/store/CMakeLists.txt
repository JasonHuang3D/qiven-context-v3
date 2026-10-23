# qiven3_store: the store, the one target that links qiven3_sqlite, which it
# takes with the first source that needs SQLite; so far its typed errors.
add_library(qiven3_store STATIC
  errors.cpp
  errors.hpp
)
target_include_directories(qiven3_store PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_store PUBLIC qiven3_memory PRIVATE qiven3_flags)
