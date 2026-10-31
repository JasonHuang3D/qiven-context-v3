# qiven3_wire: the per-call wire, a process's request read from its
# arguments within bounds given and its answer written to standard output,
# foundation: it names no command. Its answer is a template on the
# platform's standard output, in wire.hpp; utf, which reads the arguments
# strictly, is qiven3_base's.
add_library(qiven3_wire STATIC
  wire.cpp
  wire.hpp
)
target_include_directories(qiven3_wire PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_wire PRIVATE qiven3_memory qiven3_base qiven_build)
