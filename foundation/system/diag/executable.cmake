# qiven_executable(<name> SOURCES <source>... [LINK <target>...] [DIAG <suffix>] [WIDE])
# Every executable of the build, and the one add_executable: its sources,
# linked with LINK and the diagnostic entry's object library, whose object
# it takes and which links qiven_diag, the entry point qiven_diag_start on
# Windows, and the binary listed in the build's binaries.txt. DIAG names a
# variant of the entry's targets (qiven_diag_targets, CMakeLists.txt beside),
# for a binary on another runtime or memory.
# WIDE, for a binary whose entry is wmain on Windows (the product's), takes
# the entry's wide variant there.
# Each library is on the link line once (foundation/devkit/structure/tests/test_links.py):
# CMake keeps a binary's own list in its order, puts what they link after
# it, and names a library again when one after it links it. So LINK names
# a target before none that links it; qiven_diag, which links the ring the
# log links too, is none of the binary's own, so that it and the ring come
# after each that links them.
# Included by diag's CMakeLists.txt; the modules' list (build's modules.cmake)
# adds diag before every directory that makes a program.
function(qiven_executable name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "WIDE" "DIAG" "SOURCES;LINK")
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "qiven_executable(${name}): no SOURCES")
  endif()
  set(entry "qiven_diag_entry${arg_DIAG}")
  if(arg_WIDE AND WIN32)
    set(entry "qiven_diag_entry_wide")
  endif()
  add_executable(${name} ${arg_SOURCES})
  target_link_libraries(${name} PRIVATE qiven_build ${arg_LINK} ${entry})
  if(MSVC)
    target_link_options(${name} PRIVATE /ENTRY:qiven_diag_start)
  endif()
  set_property(GLOBAL APPEND PROPERTY QIVEN_BINARIES ${name})
endfunction()
