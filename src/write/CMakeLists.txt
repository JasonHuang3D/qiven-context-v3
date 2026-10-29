# qiven3_write: the write route's modules, product: entries, the entries
# file's reader, its form, members, bounds, placeholders and pieces, each
# content checked by the entry's own content check; build, its entries built
# for the store against the fold: keys, holes, digests, targets and headers;
# refuse, the write command's refusals against the fold: the slot, the
# person scope, an owner's entry and words, the kinds, refs and cycles.
add_library(qiven3_write STATIC
  build.cpp
  build.hpp
  entries.cpp
  entries.hpp
  entries_reader.cpp
  entries_reader.hpp
  refuse.cpp
  refuse.hpp
)
target_include_directories(qiven3_write PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_write PUBLIC qiven3_entry qiven3_matcher qiven3_base qiven3_memory PRIVATE qiven3_flags)
