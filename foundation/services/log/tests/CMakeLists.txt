# Log's own tests: the log's cases, the static half's, and K13's binary
# half, whose binary test_log.py reads; and K13's planted field. Added by
# harness' wiring.cmake; qiven_add_cases is made in harness' cases.cmake,
# qiven_faults and qiven_case_main in the kit's faults and case_main.
qiven_add_cases(qiven_log_test LABELS fast fake
  SOURCES log_test.cpp log_static.cpp log_k13.cpp
  LINK qiven_faults)
# The line's encoder, log's private header, by its name from log's own src/.
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH qiven_log_dir)
target_include_directories(qiven_log_test PRIVATE "${qiven_log_dir}/src")

# K13's planted field (log_plant.cpp): the control in every build, the plant
# built by its test alone, never by the build, the analysis or the records.
add_library(qiven_log_plant_control OBJECT log_plant.cpp)
add_library(qiven_log_plant OBJECT EXCLUDE_FROM_ALL log_plant.cpp)
target_compile_definitions(qiven_log_plant PRIVATE QIVEN_PLANT_FIELD)
foreach(target IN ITEMS qiven_log_plant_control qiven_log_plant)
  target_link_libraries(${target} PRIVATE qiven_build qiven_log)
endforeach()
set_target_properties(qiven_log_plant PROPERTIES QIVEN_PLANT ON EXPORT_COMPILE_COMMANDS OFF)
