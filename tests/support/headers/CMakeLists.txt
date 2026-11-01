# qiven3_headers, the analysis preset's alone: a translation unit for each
# header the analysis reads (under an analysed root of tools/trees.json or a
# layer's tree, by the root CMakeLists.txt) with no source of its own, so
# that /analyze and clang-tidy read the header and its templates' bodies,
# which a source reads only where it includes the header and instantiates
# them. Each unit includes its header and instantiates its templates with the
# types the code gives them: the fake platform, v3's allocator and the
# standard's, the stores' default hooks. An object library: compiled, and
# taken by no binary.
#
# The units, their names and the errors are foundation/devkit/build/trees.cmake's
# (qiven3_header_units), which every configure with an interpreter runs
# (tests/CMakeLists.txt): a unit's name carries its header's module, and
# configure fails on such a header with no unit, on two headers of one unit,
# on a unit no such header wants, and on a unit that does not include its
# header (an own source that does not include it, or that no target
# compiles, stands in for none).

add_library(qiven3_headers OBJECT ${qiven3_header_units})
# faults.hpp holds its loop under QIVEN_FAULTS alone; its unit defines it,
# where the build does not.
set_source_files_properties(support_faults_faults.cpp PROPERTIES COMPILE_DEFINITIONS QIVEN_FAULTS)
target_include_directories(qiven3_headers PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_headers PRIVATE qiven3_checked qiven3_store qiven3_platform_fake qiven3_fixtures qiven3_faults
  qiven3_log qiven3_diag qiven_runner qiven_build)
list(LENGTH qiven3_header_units qiven3_count)
message(STATUS "qiven3: ${qiven3_count} header-only headers, each its unit in qiven3_headers")
