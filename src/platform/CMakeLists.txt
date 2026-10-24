# qiven3_platform: the contract, which core targets link, and files, the
# retries of replace, stage and lock over an adapter's system calls. Its
# value types hold their text on v3's arenas.
add_library(qiven3_platform STATIC
  files.hpp
  platform.cpp
  platform.hpp
)
target_include_directories(qiven3_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_platform PUBLIC qiven3_memory PRIVATE qiven3_flags)

# qiven3_platform_win: the Windows adapter, built on Windows alone; the one
# product target that includes a Windows header or links a Windows library.
if(WIN32)
  add_library(qiven3_platform_win STATIC
    win/handles.cpp
    win/paths.cpp
    win/paths.hpp
    win/windows.cpp
    win/windows.hpp
  )
  target_compile_definitions(qiven3_platform_win PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(qiven3_platform_win
    PUBLIC qiven3_platform qiven3_memory
    PRIVATE qiven3_flags kernel32 shell32 ole32 bcrypt)
endif()

# qiven3_platform_current: current() and adapter_for, which a binary's main
# alone links; its header names the build's adapter type.
add_library(qiven3_platform_current STATIC
  current.cpp
  current.hpp
)
target_link_libraries(qiven3_platform_current PUBLIC qiven3_platform qiven3_memory PRIVATE qiven3_flags)
if(WIN32)
  target_link_libraries(qiven3_platform_current PUBLIC qiven3_platform_win)
endif()
