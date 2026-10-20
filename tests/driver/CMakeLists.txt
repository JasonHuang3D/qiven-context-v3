# qiven3-drive: the binary the contract tests drive (driver/drive.hpp). It
# links the platform's current and adapter_for, the platform's fake and,
# with QIVEN3_HAL on, the HAL's fake; off Windows, current builds no
# adapter, so the fake alone answers.
add_executable(qiven3-drive
  drive.hpp
  main.cpp
  platform_ops.cpp
  wire.cpp
  wire.hpp
)
target_include_directories(qiven3-drive PRIVATE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3-drive PRIVATE qiven3_flags qiven3_platform_current qiven3_platform_fake)
if(QIVEN3_HAL)
  target_sources(qiven3-drive PRIVATE hal_ops.cpp)
  target_compile_definitions(qiven3-drive PRIVATE QIVEN3_DRIVE_HAL)
  target_link_libraries(qiven3-drive PRIVATE qiven3_hal_fake)
endif()
