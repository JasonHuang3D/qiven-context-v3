# The diagnostic entry: qiven3_diag, the install, the report and Out, and
# qiven3_diag_entry, the entry point and the first initializer, an object
# that qiven3_executable gives every binary. Not core logic: it includes the
# system's headers, links dbghelp on Windows, and links qiven3_memory alone
# of v3's targets.
#
# qiven3_diag_targets(<suffix> <memory target>) makes both targets, named
# with the suffix, over the memory target given: "" over qiven3_memory, and
# the variants of the binaries built on another runtime or another memory.
function(qiven3_diag_targets suffix memory)
  set(dir "${PROJECT_SOURCE_DIR}/src/diag")
  set(sources "${dir}/diag.cpp" "${dir}/diag.hpp" "${dir}/events.hpp" "${dir}/out.cpp" "${dir}/out.hpp")
  if(WIN32)
    list(APPEND sources "${dir}/diag_windows.cpp")
  else()
    list(APPEND sources "${dir}/diag_linux.cpp" "${dir}/signal.cpp" "${dir}/signal.hpp")
  endif()
  add_library(qiven3_diag${suffix} STATIC ${sources})
  # Linux's handlers throw nothing and call nothing that throws: built
  # without exceptions, so that their object names no unwinder's routine,
  # which K18 would read as a call off the async-signal-safe list.
  if(NOT WIN32)
    set_source_files_properties("${dir}/signal.cpp" TARGET_DIRECTORY qiven3_diag${suffix}
      PROPERTIES COMPILE_OPTIONS -fno-exceptions)
  endif()
  target_include_directories(qiven3_diag${suffix} PUBLIC "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(qiven3_diag${suffix} PUBLIC ${memory} PRIVATE qiven3_flags)
  add_library(qiven3_diag_entry${suffix} OBJECT "${dir}/entry.cpp" "${dir}/diag.hpp")
  target_include_directories(qiven3_diag_entry${suffix} PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(qiven3_diag_entry${suffix} PRIVATE qiven3_flags)
  # Under a sanitizer its own report stands for the fault handlers'.
  if(QIVEN3_SANITIZE)
    target_compile_definitions(qiven3_diag${suffix} PRIVATE QIVEN3_SANITIZED)
  endif()
  if(WIN32)
    target_compile_definitions(qiven3_diag${suffix} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(qiven3_diag_entry${suffix} PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(qiven3_diag${suffix} PRIVATE kernel32 dbghelp)
    # C4073 says that init_seg(lib) puts the initializer in the library's
    # segment, which is the entry's purpose; it is the one warning off.
    target_compile_options(qiven3_diag_entry${suffix} PRIVATE /wd4073)
  endif()
endfunction()

qiven3_diag_targets("" qiven3_memory)

# qiven3_diag_entry_wide: the entry again for a binary whose entry is
# wmain, the product's on Windows (qiven3_executable's WIDE), starting the
# C runtime's wmain in place of main.
if(WIN32)
  add_library(qiven3_diag_entry_wide OBJECT entry.cpp diag.hpp)
  target_include_directories(qiven3_diag_entry_wide PRIVATE "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(qiven3_diag_entry_wide PRIVATE qiven3_flags)
  target_compile_definitions(qiven3_diag_entry_wide
    PRIVATE QIVEN3_WIDE_MAIN UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_options(qiven3_diag_entry_wide PRIVATE /wd4073)
endif()
