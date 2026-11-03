# qiven_sqlite: SQLite 3.53.4's amalgamation, sqlite3.c and sqlite3.h as
# extracted, unchanged (src/README.md). qiven_storage_sqlite alone links it
# (B1), and includes the header as "qiven/sqlite/sqlite3.h" from the PUBLIC
# include/ alone.

# The pin: each file's SHA-256 checked on every configure, pinned or not,
# before the target below is defined; the root adds this directory first, so
# a changed amalgamation fails configure before anything builds.
set(QIVEN_SQLITE_SHA256
  "src/sqlite3.c=b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189"
  "include/qiven/sqlite/sqlite3.h=919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d")
foreach(pin IN LISTS QIVEN_SQLITE_SHA256)
  string(REPLACE "=" ";" pin "${pin}")
  list(GET pin 0 qiven_file)
  list(GET pin 1 qiven_pinned)
  set(qiven_path "${CMAKE_CURRENT_SOURCE_DIR}/${qiven_file}")
  if(NOT EXISTS "${qiven_path}")
    message(FATAL_ERROR "qiven3: foundation/system/sqlite/${qiven_file} is missing")
  endif()
  file(SHA256 "${qiven_path}" qiven_digest)
  if(NOT qiven_digest STREQUAL qiven_pinned)
    message(FATAL_ERROR "qiven3: foundation/system/sqlite/${qiven_file} has SHA-256 ${qiven_digest}; "
      "the pin is ${qiven_pinned}")
  endif()
endforeach()
message(STATUS "qiven3: SQLite's amalgamation holds its pinned digests")

#
# Compiled as C at its own level: MSVC /W3 without /WX, at which the pinned
# amalgamation builds with no warning; gcc and clang their defaults without
# -Werror. The core's warnings, its analysis and its frame bound never cover
# it: it links no qiven_build. The build's sanitizers, path maps and floors
# reach it as they reach every target.
add_library(qiven_sqlite STATIC
  src/sqlite3.c
  include/qiven/sqlite/sqlite3.h
)
target_include_directories(qiven_sqlite PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(MSVC)
  target_compile_options(qiven_sqlite PRIVATE /W3)
endif()
# The foundation's one build of the engine: its options, each with its
# reason in src/README.md. None sets a default a caller sets by a pragma on
# its connection, and none changes sqlite3.h, so a caller's code needs none
# of them.
target_compile_definitions(qiven_sqlite PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_DQS=0
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
