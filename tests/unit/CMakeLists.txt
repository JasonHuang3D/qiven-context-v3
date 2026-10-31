# The unit cases, each module's registered by its own file beside this one
# (the module.json files' table names each file's module); what stays here is the
# support's.
include("${CMAKE_CURRENT_SOURCE_DIR}/memory.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/base.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/entry.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/log.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/secret.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/write.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/platform.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/wire.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/child.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/store.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/checked.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/location.cmake")
