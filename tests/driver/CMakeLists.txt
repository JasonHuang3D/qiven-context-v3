# The drives the contract tests drive (driver/drive.hpp): qiven3-drive-fake
# and, on Windows alone, qiven3-drive-windows, each compiling its
# instantiation file over the common half. Each links current and
# adapter_for, which a main alone links, and, with QIVEN3_HAL on, the HAL's
# fake.
add_library(qiven3_drive_common STATIC
  common.cpp
  drive.hpp
  platform_ops.hpp
  platform_ops_impl.hpp
  wire.cpp
  wire.hpp
)
target_include_directories(qiven3_drive_common PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_drive_common PUBLIC qiven3_platform qiven3_memory PRIVATE qiven3_flags qiven3_platform_current)
if(QIVEN3_HAL)
  target_sources(qiven3_drive_common PRIVATE hal_ops.cpp)
  target_compile_definitions(qiven3_drive_common PUBLIC QIVEN3_DRIVE_HAL)
  target_link_libraries(qiven3_drive_common PRIVATE qiven3_hal_fake)
endif()

add_executable(qiven3-drive-fake instances_fake.cpp)
target_link_libraries(qiven3-drive-fake PRIVATE qiven3_flags qiven3_drive_common qiven3_platform_fake
  qiven3_platform_current)

if(WIN32)
  add_executable(qiven3-drive-windows instances_windows.cpp)
  target_link_libraries(qiven3-drive-windows PRIVATE qiven3_flags qiven3_drive_common qiven3_platform_current)
endif()
