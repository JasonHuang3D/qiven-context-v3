# qiven3_faults: fault injection's loop, a header over memory's countdown.
add_library(qiven3_faults INTERFACE)
target_include_directories(qiven3_faults INTERFACE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_faults INTERFACE qiven3_memory)
