# qiven3_store: the store, the one target that links qiven3_sqlite: its
# contract, its typed errors, the engine's result codes mapped to them,
# stage, the admit hook at its places, the store's own guards over the
# keys, its order and the maximum, the in-memory reference store, and the SQLite store: exchange its export,
# import and verify, through the wrapper; engine/, a directory
# of its own with its own analysis configuration, holds the engine's
# calls, engine/sqlite.cpp alone including the engine's header and making
# its every call; sqlite_open.cpp runs the open steps through its wrapper,
# sqlite_create.hpp create's making and undoing on the platform,
# sqlite_write.cpp the writes, sqlite_read.cpp the reads; sizes/, a
# directory of its own for its analysis configuration too, reads a file's
# size.
add_library(qiven3_store STATIC
  codes.hpp
  contract.hpp
  engine/sqlite.cpp
  engine/sqlite.hpp
  errors.cpp
  errors.hpp
  exchange.cpp
  exchange.hpp
  memory.hpp
  memory_exchange.cpp
  memory_exchange.hpp
  memory_state.cpp
  memory_state.hpp
  sizes/sizes.cpp
  sqlite_create.hpp
  sqlite_open.cpp
  sqlite_read.cpp
  sqlite_store.hpp
  sqlite_store_impl.hpp
  sqlite_write.cpp
  stage.cpp
  stage.hpp
  values.hpp
)
target_include_directories(qiven3_store PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_store PUBLIC qiven3_keys qiven3_base qiven3_memory PRIVATE qiven3_flags qiven3_sqlite)
