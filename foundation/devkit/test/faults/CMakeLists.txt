# qiven_faults: fault injection's loop, a header over memory's countdown,
# writing each failure it injects to the case's log. Its parts.hpp registers
# a fault run's cases in the runner's registry, so it links the runner.
add_library(qiven_faults INTERFACE)
target_include_directories(qiven_faults INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_faults INTERFACE qiven_memory qiven_log qiven_runner)
