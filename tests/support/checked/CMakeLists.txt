# qiven3_checked_stores: the ruled factory checked's cases and the driver
# take, the store's factories (support/stores, header-only, under the same
# directory) bound to the ruled stores (checked) and a Site holding the
# fixtures' book. Its links are the store factory's and checked's, flat, in
# the order the link lines keep with no library twice.
add_library(qiven3_checked_stores INTERFACE)
target_include_directories(qiven3_checked_stores INTERFACE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_checked_stores INTERFACE qiven3_checked qiven3_store qiven3_platform_fake qiven3_fixtures
  qiven_runner)
