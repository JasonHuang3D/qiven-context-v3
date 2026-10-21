# qiven3_log: levelled JSON lines into a crash ring, each event of the
# catalogue; json::encode from qiven3_base, the arena from qiven3_memory.
# QIVEN3_LOG_MIN, the build's minimum level, is every target's (the top
# CMakeLists.txt), so that each sees one catalogue.
add_library(qiven3_log STATIC
  catalogue.hpp
  line.hpp
  log.cpp
  log.hpp
  ring.cpp
  ring.hpp
)
target_include_directories(qiven3_log PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_log PUBLIC qiven3_base qiven3_memory PRIVATE qiven3_flags)
