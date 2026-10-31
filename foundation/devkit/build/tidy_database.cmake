# Run by the analysis preset's build (cmake -P): writes clang-tidy's copy of
# BUILD's compile_commands.json, BUILD/tidy/compile_commands.json, without the
# options clang-cl reads as files and fails on (/analyze's, /pathmap's) or
# warns on (/experimental:, /external:). Configure rewrites the build's
# database each time; the copy is written only when its content changes, so
# that Ninja, restating it, reruns no clang-tidy step for an unchanged one.
# Input: BUILD.
cmake_minimum_required(VERSION 3.28)
file(READ "${BUILD}/compile_commands.json" db)
string(JSON count LENGTH "${db}")
math(EXPR last "${count} - 1")
foreach(i RANGE ${last})
  string(JSON command GET "${db}" ${i} command)
  string(REGEX REPLACE " (\"/(analyze|pathmap|experimental|external)[^\"]*\"|/(analyze|pathmap|experimental|external)[^ ]*)" "" command "${command}")
  string(REPLACE "\\" "\\\\" command "${command}")
  string(REPLACE "\"" "\\\"" command "${command}")
  string(JSON db SET "${db}" ${i} command "\"${command}\"")
endforeach()
set(copy "${BUILD}/tidy/compile_commands.json")
set(old "")
if(EXISTS "${copy}")
  file(READ "${copy}" old)
endif()
if(NOT "${old}" STREQUAL "${db}")
  file(WRITE "${copy}" "${db}")
endif()
