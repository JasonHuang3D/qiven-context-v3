# qiven3_checked: cognition's rules over a store's write, above the store:
# the rules as the store's admit hook (the secret check and its clearing,
# part 2, an import's rules) and the ruled stores, each store with the
# rules as its one hook.
add_library(qiven3_checked STATIC
  rules.cpp
  rules.hpp
  stores.hpp
)
target_include_directories(qiven3_checked PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_checked PUBLIC qiven3_store qiven3_entry qiven3_secret qiven3_memory PRIVATE qiven3_flags)
