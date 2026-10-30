# qiven3_stores: the factory the store cases take and their tags, a header
# binding the in-memory store and the SQLite store on the fake platform,
# each with no admit hook, and the plain records the cases write.
add_library(qiven3_stores INTERFACE)
target_include_directories(qiven3_stores INTERFACE "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_stores INTERFACE qiven3_store qiven3_platform_fake qiven3_runner)
