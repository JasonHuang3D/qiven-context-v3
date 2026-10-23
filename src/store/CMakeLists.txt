# qiven3_store: the store, the one target that links qiven3_sqlite, which it
# takes with the first source that needs SQLite; so far its contract, its
# typed errors, the engine's result codes mapped to them, stage, the
# shared validator's two parts as each store calls them, and the in-memory
# reference store.
add_library(qiven3_store STATIC
  codes.hpp
  contract.hpp
  errors.cpp
  errors.hpp
  memory.hpp
  memory_exchange.hpp
  memory_state.cpp
  memory_state.hpp
  stage.cpp
  stage.hpp
)
target_include_directories(qiven3_store PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_store PUBLIC qiven3_entry qiven3_secret qiven3_memory PRIVATE qiven3_flags)
