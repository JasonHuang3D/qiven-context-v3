# The SQLite wrapper, added by the root CMakeLists.txt. Its public header is
# include/qiven/storage_sqlite/connection.hpp, its source src/sqlite.cpp;
# its tests, under tests/, are added by tests/CMakeLists.txt.
#
# qiven_storage_sqlite: the engine heap, Connection and Statement and their
# typed error; the one target that links the vendored engine, qiven_sqlite,
# and whose source alone includes the engine's header (B1). src/, with its
# own analysis configuration, lets the engine's variadic configuration
# call pass (B22).
add_library(qiven_storage_sqlite STATIC
  include/qiven/storage_sqlite/connection.hpp
  src/sqlite.cpp
)
target_include_directories(qiven_storage_sqlite PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_storage_sqlite PUBLIC qiven_memory PRIVATE qiven_build qiven_sqlite)
