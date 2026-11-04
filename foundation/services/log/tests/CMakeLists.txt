# Log's own tests: the log's cases, the static half's, and K13's binary
# half, whose binary test_log.py reads; and K13's planted field. Added by
# tests/CMakeLists.txt, where qiven_add_cases, qiven_faults and
# qiven_case_main are made.
qiven_add_cases(qiven_log_test LABELS fast fake
  SOURCES log_test.cpp log_static.cpp log_k13.cpp
  LINK qiven_faults)

# K13's planted field (log_plant.cpp): the control in every build, the plant
# built by its test alone, never by the build, the analysis or the records.
add_library(qiven_log_plant_control OBJECT log_plant.cpp)
add_library(qiven_log_plant OBJECT EXCLUDE_FROM_ALL log_plant.cpp)
target_compile_definitions(qiven_log_plant PRIVATE QIVEN_PLANT_FIELD)
foreach(target IN ITEMS qiven_log_plant_control qiven_log_plant)
  target_link_libraries(${target} PRIVATE qiven_build qiven_log)
endforeach()
set_target_properties(qiven_log_plant PROPERTIES QIVEN3_PLANT ON EXPORT_COMPILE_COMMANDS OFF)
