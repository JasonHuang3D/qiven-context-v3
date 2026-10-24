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
)
target_include_directories(qiven3_drive_common PUBLIC "${PROJECT_SOURCE_DIR}/tests")
# The request loop holds the process's log, kept for the entry's report.
target_link_libraries(qiven3_drive_common PUBLIC qiven3_platform qiven3_memory qiven3_base qiven3_log qiven3_diag
  PRIVATE qiven3_flags qiven3_platform_current)
if(QIVEN3_HAL)
  target_sources(qiven3_drive_common PRIVATE hal_ops.cpp hal_ops.hpp)
  target_compile_definitions(qiven3_drive_common PUBLIC QIVEN3_DRIVE_HAL)
  target_link_libraries(qiven3_drive_common PUBLIC qiven3_hal_fake)
endif()

# The fake's drive alone has the store's requests, over support/stores'
# factory (driver/store_ops.hpp).
qiven3_executable(qiven3-drive-fake SOURCES instances_fake.cpp store_ops.cpp store_ops.hpp store_fill.cpp store_fixtures.cpp
  store_held.hpp store_plans.cpp store_threads.hpp store_wire.cpp store_wire.hpp
  LINK qiven3_drive_common qiven3_platform_fake qiven3_platform_current qiven3_stores)

if(WIN32)
  qiven3_executable(qiven3-drive-windows SOURCES instances_windows.cpp
    LINK qiven3_drive_common qiven3_platform_current)
endif()
