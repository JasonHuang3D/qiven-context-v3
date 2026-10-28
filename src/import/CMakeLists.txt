# qiven3_import: the import's readers of the files of the system v3
# imports from, product: yaml, its flat YAML read strictly. utf, which
# checks each line's bytes, is qiven3_base's.
add_library(qiven3_import STATIC
  yaml.cpp
  yaml.hpp
)
target_include_directories(qiven3_import PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_import PRIVATE qiven3_memory qiven3_base qiven3_flags)
