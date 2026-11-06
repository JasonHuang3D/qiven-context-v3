# The diagnostic entry: qiven_diag_ring, the crash ring the log writes and
# the report reads, core logic, a core target linking qiven_memory alone;
# qiven_diag, the install, the report and Out, linking the ring; and
# qiven_diag_entry, the entry point and the first initializer, an object
# that qiven_executable gives every binary. qiven_diag and the entry are not
# core targets: they include the system's headers, link dbghelp on Windows,
# and link the ring and qiven_memory alone of v3's. Its tests (tests/) are
# added by the runner's wiring.cmake.
#
# qiven_diag_targets(<suffix> <memory target> [RING_INSIDE]) makes the three
# targets, named with the suffix, over the memory target given: "" over
# qiven_memory, and the variants of the binaries built on another runtime or
# another memory (memory's tests' _poisoned, the fault program's _crtdbg,
# tests/faults/). RING_INSIDE, for a variant no log is built on (_crtdbg),
# compiles the ring into qiven_diag<suffix> and makes no ring target.
function(qiven_diag_targets suffix memory)
  cmake_parse_arguments(PARSE_ARGV 2 arg "RING_INSIDE" "" "")
  set(dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  set(ring "${dir}/src/ring.cpp" "${dir}/include/qiven/diag/ring.hpp")
  set(sources "${dir}/src/diag.cpp" "${dir}/include/qiven/diag/diag.hpp" "${dir}/src/events.hpp"
    "${dir}/src/out.cpp" "${dir}/include/qiven/diag/out.hpp")
  if(arg_RING_INSIDE)
    list(APPEND sources ${ring})
    set(ring_target "")
  else()
    set(ring_target "qiven_diag_ring${suffix}")
    add_library(${ring_target} STATIC ${ring})
    target_include_directories(${ring_target} PUBLIC "${dir}/include")
    target_link_libraries(${ring_target} PUBLIC ${memory} PRIVATE qiven_build)
  endif()
  if(WIN32)
    list(APPEND sources "${dir}/src/diag_windows.cpp")
  else()
    list(APPEND sources "${dir}/src/diag_linux.cpp" "${dir}/src/signal.cpp" "${dir}/src/signal.hpp")
  endif()
  add_library(qiven_diag${suffix} STATIC ${sources})
  # Linux's handlers throw nothing and call nothing that throws: built
  # without exceptions, so that their object names no unwinder's routine,
  # which K18 would read as a call off the async-signal-safe list.
  if(NOT WIN32)
    set_source_files_properties("${dir}/src/signal.cpp" TARGET_DIRECTORY qiven_diag${suffix}
      PROPERTIES COMPILE_OPTIONS -fno-exceptions)
  endif()
  target_include_directories(qiven_diag${suffix} PUBLIC "${dir}/include")
  target_link_libraries(qiven_diag${suffix} PUBLIC ${ring_target} ${memory} PRIVATE qiven_build)
  # A binary links the entry, which takes its object and qiven_diag
  # (qiven_executable).
  add_library(qiven_diag_entry${suffix} OBJECT "${dir}/src/entry.cpp" "${dir}/include/qiven/diag/diag.hpp")
  target_link_libraries(qiven_diag_entry${suffix} PUBLIC qiven_diag${suffix} PRIVATE qiven_build)
  # Under a sanitizer its own report stands for the fault handlers'.
  if(QIVEN_SANITIZE)
    target_compile_definitions(qiven_diag${suffix} PRIVATE QIVEN_SANITIZED)
  endif()
  if(WIN32)
    target_compile_definitions(qiven_diag${suffix} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(qiven_diag_entry${suffix} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(qiven_diag${suffix} PRIVATE kernel32 dbghelp)
    # C4073 says that init_seg(lib) puts the initializer in the library's
    # segment, which is the entry's purpose; it is the one warning off.
    target_compile_options(qiven_diag_entry${suffix} PRIVATE /wd4073)
  endif()
endfunction()

qiven_diag_targets("" qiven_memory)

# qiven_diag_entry_wide: the entry again for a binary whose entry is
# wmain, the product's on Windows (qiven_executable's WIDE), starting the
# C runtime's wmain in place of main.
if(WIN32)
  add_library(qiven_diag_entry_wide OBJECT src/entry.cpp include/qiven/diag/diag.hpp)
  target_link_libraries(qiven_diag_entry_wide PUBLIC qiven_diag PRIVATE qiven_build)
  target_compile_definitions(qiven_diag_entry_wide
    PRIVATE QIVEN_WIDE_MAIN UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(qiven_diag_entry_wide PRIVATE /wd4073)
endif()
