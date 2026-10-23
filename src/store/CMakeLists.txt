# qiven3_store: the store, the one target that links qiven3_sqlite: its
# contract, its typed errors, the engine's result codes mapped to them,
# stage, the shared validator's two parts as each store calls them, the
# in-memory reference store, and the SQLite store: sqlite.cpp alone
# includes the engine's header and makes its every call; sqlite_open.cpp
# runs the open steps through its wrapper, sqlite_write.cpp the writes.
add_library(qiven3_store STATIC
  codes.hpp
  contract.hpp
  errors.cpp
  errors.hpp
  memory.hpp
  memory_exchange.cpp
  memory_exchange.hpp
  memory_state.cpp
  memory_state.hpp
  sqlite.cpp
  sqlite.hpp
  sqlite_open.cpp
  sqlite_store.hpp
  sqlite_write.cpp
  stage.cpp
  stage.hpp
)
target_include_directories(qiven3_store PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_store PUBLIC qiven3_entry qiven3_secret qiven3_memory PRIVATE qiven3_flags qiven3_sqlite)
