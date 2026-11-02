# cmake -DROOT=<tree> -P tools/loop_listing.cmake: for each header the build
# analyses, header=unit, the source it compiles through, as ROOT's own
# trees.cmake reads it
# (qiven3_header_units): its own source where its list names it and it
# includes the header, else its unit under tests/support/headers/; an error
# where ROOT's trees.cmake names none (<out>_of). First, ROOT=, ROOT as the
# build's filters match it.
include("${ROOT}/foundation/devkit/build/trees.cmake")
qiven3_trees("${ROOT}/tools/trees.json")
qiven3_path_regex("${ROOT}" regex)
message("ROOT=${regex}")
qiven3_header_units("${ROOT}" units)
if(NOT DEFINED units_of)
  message(FATAL_ERROR "loop_listing: ${ROOT}'s qiven3_header_units names no header's unit (units_of)")
endif()
foreach(pair IN LISTS units_of)
  message("${pair}")
endforeach()
