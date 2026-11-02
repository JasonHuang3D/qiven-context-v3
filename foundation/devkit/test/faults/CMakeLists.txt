# qiven_faults: fault injection's loop, a header over memory's countdown,
# writing each failure it injects to the case's log.
add_library(qiven_faults INTERFACE)
target_include_directories(qiven_faults INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_faults INTERFACE qiven3_memory qiven3_log)
