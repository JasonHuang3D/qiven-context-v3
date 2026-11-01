# cmake -DROOT=<tree> -DPATHFILE=<file> -P tools/loop_listing.cmake: for each
# header of PATHFILE the build analyses, header=unit, the source it compiles
# through by ROOT's own trees.cmake: its own source beside it, else its unit
# under tests/support/headers/. First, ROOT=, ROOT as the build's filters
# match it.
include("${ROOT}/foundation/devkit/build/trees.cmake")
qiven3_trees("${ROOT}/tools/trees.json")
qiven3_path_regex("${ROOT}" regex)
message("ROOT=${regex}")
file(STRINGS "${PATHFILE}" paths)
foreach(path IN LISTS paths)
  qiven3_analysed("${path}" read)
  if(NOT read OR NOT path MATCHES "${qiven3_header_suffix}")
    continue()
  endif()
  string(REGEX REPLACE "${qiven3_header_suffix}" "" stem "${path}")
  qiven3_own_source("${ROOT}" "${stem}" unit)
  if(NOT unit)
    qiven3_header_unit("${ROOT}" "${path}" name unit)
  endif()
  if(NOT unit)
    set(unit "tests/support/headers/${name}")
  endif()
  message("${path}=${unit}")
endforeach()
