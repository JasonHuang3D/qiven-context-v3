# qiven_text: the foundation's text, over memory alone: strict UTF-8 and
# the parser's typed error (utf), the JSON value, scan and encode (json,
# json_bytes behind it), and the strict parser and canonical bytes over a
# key rule (canon); their instantiations over memory's allocator. Its tests
# (tests/) are added by the runner's wiring.cmake.
#
# qiven_text_targets(<suffix> <memory target>) makes qiven_text with the
# suffix, over the memory target given: "" over qiven_memory, and the
# variant built on another memory (memory's tests' _poisoned).
function(qiven_text_targets suffix memory)
  set(dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
  add_library(qiven_text${suffix} STATIC
    "${dir}/src/utf.cpp"
    "${dir}/src/json.cpp"
    "${dir}/src/canon.cpp"
    "${dir}/include/qiven/text/utf.hpp"
    "${dir}/include/qiven/text/json.hpp"
    "${dir}/include/qiven/text/json_bytes.hpp"
    "${dir}/include/qiven/text/canon.hpp"
  )
  target_include_directories(qiven_text${suffix} PUBLIC "${dir}/include")
  target_link_libraries(qiven_text${suffix} PUBLIC ${memory} PRIVATE qiven_build)
endfunction()

qiven_text_targets("" qiven_memory)
