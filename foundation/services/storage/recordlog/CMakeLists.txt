# The record log, added by the root CMakeLists.txt. Its public headers are
# include/qiven/recordlog/, its sources src/; its tests and their support
# are tests/, added by tests/CMakeLists.txt.

# qiven_recordlog_keys: a record's keys, Id and Head, the most entries one
# call takes and a scope set's match, below the store, which names them; a
# header alone (keys.hpp), the standard library its one include, so that
# the keys are named without the store.
add_library(qiven_recordlog_keys INTERFACE)
target_include_directories(qiven_recordlog_keys INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

# qiven_recordlog: the store, the one target that links qiven_sqlite: its
# contract, its typed errors, the engine's result codes mapped to them,
# stage, the admit hook at its places, the store's own guards over the
# keys, its order and the maximum, the in-memory reference store, and the
# SQLite store: exchange its export, import and verify, through the
# wrapper; src/engine/, a directory of its own with its own analysis
# configuration, holds the engine's calls, src/engine/sqlite.cpp alone
# including the engine's header and making its every call, its wrapper's
# header include/qiven/recordlog/engine/connection.hpp; sqlite_steps.hpp
# declares the store's settings, open steps, writes and reads through the
# wrapper, sqlite_open.cpp runs the open steps, sqlite_create.hpp create's
# making and undoing on the platform, sqlite_write.cpp the writes,
# sqlite_read.cpp the reads;
# src/sizes/, a directory of its own for its analysis configuration too,
# reads a file's size.
add_library(qiven_recordlog STATIC
  include/qiven/recordlog/codes.hpp
  include/qiven/recordlog/contract.hpp
  include/qiven/recordlog/engine/connection.hpp
  include/qiven/recordlog/errors.hpp
  include/qiven/recordlog/exchange.hpp
  include/qiven/recordlog/memory.hpp
  include/qiven/recordlog/memory_exchange.hpp
  include/qiven/recordlog/memory_state.hpp
  include/qiven/recordlog/sqlite_create.hpp
  include/qiven/recordlog/sqlite_steps.hpp
  include/qiven/recordlog/sqlite_store.hpp
  include/qiven/recordlog/sqlite_store_impl.hpp
  include/qiven/recordlog/stage.hpp
  include/qiven/recordlog/values.hpp
  src/engine/sqlite.cpp
  src/errors.cpp
  src/exchange.cpp
  src/memory_exchange.cpp
  src/memory_state.cpp
  src/sizes/sizes.cpp
  src/sqlite_open.cpp
  src/sqlite_read.cpp
  src/sqlite_write.cpp
  src/stage.cpp
)
target_include_directories(qiven_recordlog PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_recordlog PUBLIC qiven_recordlog_keys qiven_text qiven_platform qiven_memory
  PRIVATE qiven_build qiven_sqlite)
