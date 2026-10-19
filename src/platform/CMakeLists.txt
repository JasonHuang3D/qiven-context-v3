# qiven3_platform: the contract, which core targets link (files joins it
# with its package).
add_library(qiven3_platform STATIC
  platform.cpp
  platform.hpp
)
target_include_directories(qiven3_platform PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_platform PRIVATE qiven3_flags)

# qiven3_platform_win: the Windows adapter, built on Windows alone; the one
# product target that includes a Windows header or links a Windows library.
if(WIN32)
  add_library(qiven3_platform_win STATIC
    win/windows.cpp
    win/windows.hpp
  )
  target_compile_definitions(qiven3_platform_win PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(qiven3_platform_win
    PUBLIC qiven3_platform
    PRIVATE qiven3_flags kernel32 shell32 ole32 bcrypt)
endif()

# qiven3_platform_current: current() and adapter_for, which a binary's main
# alone links.
add_library(qiven3_platform_current STATIC
  current.cpp
  current.hpp
)
target_link_libraries(qiven3_platform_current PUBLIC qiven3_platform PRIVATE qiven3_flags)
if(WIN32)
  target_link_libraries(qiven3_platform_current PRIVATE qiven3_platform_win)
endif()
