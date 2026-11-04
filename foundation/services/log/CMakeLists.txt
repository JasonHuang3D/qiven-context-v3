# qiven_log: levelled JSON lines into a crash ring, each event of the
# catalogue; json::encode from qiven_text, the arena from qiven_memory, the
# ring from qiven_diag_ring (qiven/diag/ring.hpp). Its tests (tests/) are added by
# tests/CMakeLists.txt, where the case functions and qiven_case_main are
# made. QIVEN_LOG_MIN, the build's minimum level, is every target's
# (foundation/devkit/build/build.cmake), so that each sees one catalogue.
#
# qiven_log_targets(<suffix> <memory target>) makes qiven_log with the
# suffix, over the memory target given and the ring and the text of that
# suffix: "" over qiven_memory, and the variant built on another memory
# (memory's tests' _poisoned).
function(qiven_log_targets suffix memory)
  set(dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  add_library(qiven_log${suffix} STATIC
    "${dir}/src/log.cpp"
    "${dir}/src/line.hpp"
    "${dir}/include/qiven/log/log.hpp"
    "${dir}/include/qiven/log/catalogue.hpp"
  )
  target_include_directories(qiven_log${suffix} PUBLIC "${dir}/include")
  target_link_libraries(qiven_log${suffix} PUBLIC qiven_diag_ring${suffix} qiven_text${suffix} ${memory}
    PRIVATE qiven_build)
endfunction()

qiven_log_targets("" qiven_memory)
