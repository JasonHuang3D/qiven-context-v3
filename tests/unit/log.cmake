# The log's cases: log, ring, the report's events, the static half's, and
# K13's binary half, whose binary tests/drive/test_log.py reads.
qiven3_add_cases(qiven3_unit_log LABELS fast fake
  SOURCES log_test.cpp log_static.cpp log_k13.cpp ring_test.cpp events_test.cpp
  LINK qiven3_faults)

# K13's planted field (log_plant.cpp): the control in every build, the plant
# built by its test alone, never by the build, the analysis or the records.
add_library(qiven3_log_plant_control OBJECT log_plant.cpp)
add_library(qiven3_log_plant OBJECT EXCLUDE_FROM_ALL log_plant.cpp)
target_compile_definitions(qiven3_log_plant PRIVATE QIVEN3_PLANT_FIELD)
foreach(target IN ITEMS qiven3_log_plant_control qiven3_log_plant)
  target_link_libraries(${target} PRIVATE qiven3_flags qiven3_log)
endforeach()
set_target_properties(qiven3_log_plant PROPERTIES QIVEN3_PLANT ON EXPORT_COMPILE_COMMANDS OFF)
