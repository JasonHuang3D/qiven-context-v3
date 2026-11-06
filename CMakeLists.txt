cmake_minimum_required(VERSION 3.28)
project(qiven LANGUAGES C CXX)

# The root's build: the foundation's parts, then the product's directories.
include("${PROJECT_SOURCE_DIR}/foundation/devkit/build/build.cmake" NO_POLICY_SCOPE)
include("${qiven_root}/foundation/devkit/build/top.cmake")
include("${qiven_root}/foundation/devkit/build/modules.cmake")

add_subdirectory(src/qiven3)
add_subdirectory(src/entry)
add_subdirectory(products/qiven3/format)
add_subdirectory(src/checked)
add_subdirectory(src/write)
add_subdirectory(src/app)
add_subdirectory(tests)

qiven_top_end()
