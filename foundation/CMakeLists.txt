cmake_minimum_required(VERSION 3.28)
project(qiven LANGUAGES C CXX)

# The foundation alone, no product directory: its targets and the tests of
# them that need no product. By the root's parts (../CMakeLists.txt), with the
# trees narrowed to this one and the tests below left out of the listing.
set(qiven_kept_tree foundation)  # trees.cmake's qiven_kept; the table stays whole
# Left out by id, or by a package's name: no such test is defined here, and a
# name that matches no test fails the listing. Each reads what only a whole
# checkout holds.
set(qiven_left_out
  # Its row's test_depends are the product's; it judges every tree, the whole
  # build's records and git's whole checkout.
  foundation.devkit.structure.tests
  # Reads every .clang-tidy of the checkout.
  foundation.devkit.build.tests.test_trees_build.TreesBuild.test_header_filter
  # Walk every tree of the checkout.
  foundation.devkit.build.tests.test_trees_build.TreesBuild.test_one_reader
  foundation.devkit.build.tests.test_trees_build.TreesBuild.test_listing_reader
  # Its answer for products/qiven3/tests/ rests on that module's module.json.
  foundation.devkit.build.tests.test_trees_build.TreesBuild.test_analysis_choice
  # Configures the root's analysis preset, the product's directories with it.
  foundation.devkit.build.tests.test_trees_build.TreesBuild.test_steps_name_composed
  # Must read the links of the product's targets among the five that failed on macOS.
  foundation.devkit.build.tests.test_links.Links.test_build
  # Judges the root's test presets, the product's selections, against this
  # tree's tests.
  foundation.devkit.test.harness.tests.test_wiring.Registered.test_presets)
list(LENGTH qiven_left_out qiven_count)
message(STATUS "qiven: the foundation alone, ${qiven_count} names of Python tests left out (foundation/CMakeLists.txt)")

include("${PROJECT_SOURCE_DIR}/devkit/build/build.cmake" NO_POLICY_SCOPE)
include("${qiven_root}/foundation/devkit/build/top.cmake")
include("${qiven_root}/foundation/devkit/build/modules.cmake")
include("${qiven_root}/foundation/devkit/test/harness/wiring.cmake")
qiven_foundation_tests_end()
qiven_top_end()
