# qiven3_headers and qiven3_test_headers, the analysis presets' alone: a
# translation unit for each of the product's headers the analysis reads
# (under an analysed root of tools/trees.json or a product's tree, by the
# root CMakeLists.txt) with no source of its own, so that /analyze and
# clang-tidy read the header and its templates' bodies, which a source reads
# only where it includes the header and instantiates them. Each unit
# includes its header and instantiates its templates with the types the code
# gives them: the fake platform, v3's allocator and the standard's, the
# stores' default hooks. The foundation's units lie in their modules, in
# qiven_headers and qiven_test_headers (the root CMakeLists.txt); the product's
# stay here until its own move.
#
# The pair is qiven_header_targets', a unit's analysis by its header: one of
# test code compiles in qiven3_test_headers, any other in qiven3_headers.
# The units, their names and places and the errors are
# foundation/devkit/build/header_units.cmake's (qiven_header_units), which
# every configure with an interpreter runs (the root CMakeLists.txt): a
# unit's name carries its header's module, and configure fails on such a header
# with no unit, on two headers of one unit, on a unit no such header wants,
# and on a unit that does not include its header (an own source that does
# not include it, or that no target compiles, stands in for none).

qiven_header_targets(qiven3_headers qiven3_test_headers "tests/support/headers/")
foreach(target IN ITEMS qiven3_headers qiven3_test_headers)
  target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/tests")
  target_link_libraries(${target} PRIVATE qiven3_checked qiven_recordlog qiven_recordlog_stores qiven_platform_fake qiven3_fixtures qiven_faults
    qiven_log qiven_diag qiven_transport qiven_runner qiven_case_main qiven_build)
endforeach()
