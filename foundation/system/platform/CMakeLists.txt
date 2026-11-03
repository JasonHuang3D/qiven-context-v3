# The platform, added by the root CMakeLists.txt, as its tests (tests/) are
# by tests/CMakeLists.txt, where the case functions and qiven_case_main are
# made.
#
# qiven_platform_targets(<suffix> <memory target>) makes qiven_platform with
# the suffix, over the memory target given: "" over qiven_memory, and the
# variants built on another memory (memory's tests' _poisoned). It is the
# contract, which a caller's targets link, and files, the retries of
# replace, stage and lock over an adapter's system calls, and names, list,
# remove and rename over them. Its value types hold their text on the
# caller's arenas.
function(qiven_platform_targets suffix memory)
  set(dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  add_library(qiven_platform${suffix} STATIC
    "${dir}/include/qiven/platform/files.hpp"
    "${dir}/include/qiven/platform/names.hpp"
    "${dir}/include/qiven/platform/platform.hpp"
    "${dir}/src/platform.cpp"
  )
  target_include_directories(qiven_platform${suffix} PUBLIC "${dir}/include")
  target_link_libraries(qiven_platform${suffix} PUBLIC ${memory} PRIVATE qiven_build)
endfunction()

qiven_platform_targets("" qiven_memory)

# qiven_platform_windows: the Windows adapter, built on Windows alone; the
# one target of the platform that includes a Windows header or links a
# Windows library. Its private headers sit beside its sources in
# src/windows/.
if(WIN32)
  add_library(qiven_platform_windows STATIC
    include/qiven/platform/windows/files.hpp
    include/qiven/platform/windows/windows.hpp
    src/windows/arguments.cpp
    src/windows/child.cpp
    src/windows/facts.cpp
    src/windows/facts.hpp
    src/windows/facts_calls.cpp
    src/windows/facts_calls.hpp
    src/windows/files.cpp
    src/windows/handles.cpp
    src/windows/names.cpp
    src/windows/paths.cpp
    src/windows/paths.hpp
    src/windows/process.cpp
    src/windows/stage.cpp
    src/windows/synced.cpp
    src/windows/windows.cpp
  )
  target_compile_definitions(qiven_platform_windows PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(qiven_platform_windows
    PUBLIC qiven_platform qiven_memory
    PRIVATE qiven_build kernel32 advapi32 shell32 ole32 bcrypt)
endif()

# qiven_platform_current: current() and adapter_for, which a binary's main
# alone links; its header names the build's adapter type.
add_library(qiven_platform_current STATIC
  include/qiven/platform/current.hpp
  src/current.cpp
)
target_link_libraries(qiven_platform_current PUBLIC qiven_platform qiven_memory PRIVATE qiven_build)
if(WIN32)
  target_link_libraries(qiven_platform_current PUBLIC qiven_platform_windows)
endif()

# qiven_platform_fake: the fake platform, on the C++ standard library alone,
# built on every system; its files' system calls under files' retries;
# its seed blocks.
add_library(qiven_platform_fake STATIC
  include/qiven/platform/fake/fake_files.hpp
  include/qiven/platform/fake/fake_platform.hpp
  src/fake/fake_echo.cpp
  src/fake/fake_files.cpp
  src/fake/fake_platform.cpp
  src/fake/fake_seed.cpp
)
target_link_libraries(qiven_platform_fake PUBLIC qiven_platform PRIVATE qiven_build)
