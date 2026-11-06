# qiven_transport: the per-call wire, a process's request read from its
# arguments within bounds given and its answer written to standard output,
# foundation: it names no command. Its answer is a template on the
# platform's standard output, in qiven/transport/wire.hpp; utf, which reads
# the arguments strictly, is qiven_text's. Its tests (tests/) are added by
# harness' wiring.cmake.
add_library(qiven_transport STATIC
  src/wire.cpp
  include/qiven/transport/wire.hpp
)
target_include_directories(qiven_transport PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(qiven_transport PRIVATE qiven_memory qiven_text qiven_build)
