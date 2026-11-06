# The foundation's modules, the one list of them, each named here alone, as
# its path under foundation/; compute's under QIVEN_HAL alone (H6). The project
# on top includes this file at its own directory's scope, after build.cmake,
# not through a directory of the foundation's, so that the list and each
# module's variables reach what follows. Each module's directory is added here,
# its build directory foundation/<path> under the includer's; the runner's
# wiring walks the list for each module's tests. The vendored engine first,
# whose pin fails a changed amalgamation before anything builds
# (system/sqlite/CMakeLists.txt).
set(qiven_foundation_modules
  system/sqlite
  core/memory
  core/matcher
  core/text
  core/time
  core/digest
  system/diag
  services/log
  services/transport
  system/platform
  services/storage/sqlite
  services/storage/recordlog)
if(QIVEN_HAL)
  list(APPEND qiven_foundation_modules services/compute)
endif()
foreach(qiven_module IN LISTS qiven_foundation_modules)
  add_subdirectory("${qiven_root}/foundation/${qiven_module}" "foundation/${qiven_module}")
endforeach()
