# qiven3_sqlite: SQLite 3.53.4's amalgamation, sqlite3.c and sqlite3.h as
# extracted, unchanged (README.md here). The top CMakeLists.txt checks both
# files' SHA-256 before this directory is read, so a changed amalgamation
# never builds. qiven3_store alone links it (B1).
#
# Compiled as C at its own level: MSVC /W3 without /WX, at which the pinned
# amalgamation builds with no warning; gcc and clang their defaults without
# -Werror. The core's warnings, its analysis and its frame bound never cover
# it: it links no qiven3_flags. The build's sanitizers, path maps and floors
# reach it as they reach every target.
add_library(qiven3_sqlite STATIC
  sqlite3.c
  sqlite3.h
)
target_include_directories(qiven3_sqlite PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(MSVC)
  target_compile_options(qiven3_sqlite PRIVATE /W3)
endif()
# The options, each with its reason in README.md. None changes sqlite3.h,
# so the store's own code needs none of them.
target_compile_definitions(qiven3_sqlite PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0
  SQLITE_DEFAULT_SYNCHRONOUS=2
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=2
  SQLITE_DEFAULT_PAGE_SIZE=4096
  SQLITE_DEFAULT_CACHE_SIZE=-2000
  SQLITE_DEFAULT_WAL_AUTOCHECKPOINT=1000
  SQLITE_DEFAULT_AUTOVACUUM=0
  SQLITE_SECURE_DELETE
  SQLITE_TEMP_STORE=2
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_TRUSTED_SCHEMA=0
  SQLITE_ENABLE_FTS5
  SQLITE_ENABLE_API_ARMOR
  SQLITE_LIKE_DOESNT_MATCH_BLOBS
  SQLITE_STRICT_SUBTYPE=1
  SQLITE_OMIT_AUTOINIT
  SQLITE_OMIT_SHARED_CACHE
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_OMIT_DECLTYPE
  SQLITE_OMIT_PROGRESS_CALLBACK
)
