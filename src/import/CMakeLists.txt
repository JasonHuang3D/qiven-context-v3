# qiven3_import: the import's readers of the files of the system v3
# imports from, product: yaml, its flat YAML read strictly; git, the git
# route through the platform's run, its output's parsers pure. utf, which
# checks each line's bytes and each name git gives, is qiven3_base's.
add_library(qiven3_import STATIC
  git.cpp
  git.hpp
  yaml.cpp
  yaml.hpp
)
target_include_directories(qiven3_import PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(qiven3_import PUBLIC qiven3_platform PRIVATE qiven3_memory qiven3_base qiven3_flags)
