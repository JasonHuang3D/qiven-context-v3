# qiven3_log: levelled JSON lines into a crash ring, each event of the
# catalogue; json::encode from qiven_text, the arena from qiven_memory, the
# ring from qiven_diag (qiven/diag/ring.hpp).
# QIVEN_LOG_MIN, the build's minimum level, is every target's
# (foundation/devkit/build/build.cmake), so that each sees one catalogue.
add_library(qiven3_log STATIC
  catalogue.hpp
  line.hpp
  log.cpp
  log.hpp
)
target_include_directories(qiven3_log PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_log PUBLIC qiven_diag qiven_text qiven_memory PRIVATE qiven_build)
