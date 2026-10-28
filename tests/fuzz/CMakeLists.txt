# The fuzz targets, each fuzz/<target>/target.cpp, whose
# qiven3::fuzz::<target>_input takes one input, and whose
# LLVMFuzzerTestOneInput calls it in the fuzz binary alone
# (QIVEN3_FUZZ_BINARY), so that one regression binary holds every target.
# In every build the regression cases run each over its kept inputs, fuzz/<target>/regress/<sha1>, which
# configure writes into regress_inputs.cpp as bytes, so that the binary
# reads no path. Under libFuzzer (QIVEN3_SANITIZE with fuzzer) each target
# is a binary of its own on libFuzzer's main, its corpus,
# fuzz/<target>/corpus/ in the build, seeded at the build by
# tools/fuzz_seeds.py and grown by libFuzzer, its findings under
# fuzz/<target>/artifacts/; the listing step (runner/ctest_tests.cmake)
# makes each a CTest test labelled fuzz from fuzz_targets.txt.
set(qiven3_fuzz_targets digest entry wire)

# The kept inputs: an array of bytes each, and the tables the case reads.
set(arrays "")
set(targets "")
set(names "")
set(spans "")
set(count 0)
set(regress_files "")
foreach(target IN LISTS qiven3_fuzz_targets)
  file(GLOB kept CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${target}/regress/*")
  list(SORT kept)
  foreach(file IN LISTS kept)
    get_filename_component(name "${file}" NAME)
    if(NOT name MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "qiven3: a kept input of fuzz/${target}/regress/ is not named by its digest: ${name}")
    endif()
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" digits)
    math(EXPR size "${digits} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REGEX REPLACE "((0x..,){16})" "\\1\n    " bytes "${bytes}")
    string(APPEND arrays "constexpr std::array<std::uint8_t, ${size}> kept_${count} = {\n    ${bytes}};\n")
    string(APPEND targets "    \"${target}\",\n")
    string(APPEND names "    \"${name}\",\n")
    string(APPEND spans "    kept_${count},\n")
    list(APPEND regress_files "${file}")
    math(EXPR count "${count} + 1")
  endforeach()
endforeach()
set(regress_source "${CMAKE_CURRENT_BINARY_DIR}/regress_inputs.cpp")
file(GENERATE OUTPUT "${regress_source}" CONTENT "// Written by tests/fuzz/CMakeLists.txt at configure: the kept inputs.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace {
${arrays}
constexpr std::array<std::string_view, ${count}> targets = {
${targets}};
constexpr std::array<std::string_view, ${count}> names = {
${names}};
constexpr std::array<std::span<const std::uint8_t>, ${count}> spans = {
${spans}};
}  // namespace

namespace qiven3::fuzz {
std::size_t kept_count() noexcept { return ${count}; }
std::string_view kept_target(std::size_t i) noexcept { return targets[i]; }
std::string_view kept_name(std::size_t i) noexcept { return names[i]; }
std::span<const std::uint8_t> kept_bytes(std::size_t i) noexcept { return spans[i]; }
}  // namespace qiven3::fuzz
")

qiven3_add_cases(qiven3_fuzz_regress LABELS fast fake
  SOURCES regress_test.cpp "${regress_source}" digest/target.cpp entry/target.cpp wire/target.cpp
  LINK qiven3_platform_fake qiven3_entry qiven3_wire)

if(QIVEN3_SANITIZE MATCHES "fuzzer")
  set(fuzz_lines "")
  foreach(target IN LISTS qiven3_fuzz_targets)
    set(dir "${PROJECT_BINARY_DIR}/fuzz/${target}")
    file(MAKE_DIRECTORY "${dir}/corpus" "${dir}/artifacts")
    qiven3_executable(qiven3_fuzz_${target} SOURCES ${target}/target.cpp
      LINK qiven3_entry qiven3_base qiven3_platform_fake qiven3_wire)
    target_compile_definitions(qiven3_fuzz_${target} PRIVATE QIVEN3_FUZZ_BINARY)
    # libFuzzer's main, here alone (cmake/sanitize.cmake): MSVC's
    # /fsanitize=fuzzer on the target's own object names it to the link.
    if(MSVC)
      target_compile_options(qiven3_fuzz_${target} PRIVATE /fsanitize=fuzzer)
    else()
      target_link_options(qiven3_fuzz_${target} PRIVATE -fsanitize=fuzzer)
    endif()
    string(APPEND fuzz_lines "${target}\t$<TARGET_FILE:qiven3_fuzz_${target}>\t${dir}/corpus\t${dir}/artifacts\n")
    set_property(GLOBAL APPEND PROPERTY QIVEN3_FUZZ_BINARIES qiven3_fuzz_${target})
    if(Python3_EXECUTABLE)
      file(GLOB target_kept CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${target}/regress/*")
      add_custom_command(OUTPUT "${dir}/seeded"
        COMMAND "${Python3_EXECUTABLE}" -I "${PROJECT_SOURCE_DIR}/tools/fuzz_seeds.py" "${PROJECT_SOURCE_DIR}"
          "${target}" "${dir}/corpus"
        COMMAND "${CMAKE_COMMAND}" -E touch "${dir}/seeded"
        DEPENDS "${PROJECT_SOURCE_DIR}/tools/fuzz_seeds.py" ${target_kept}
        COMMENT "Seeding fuzz/${target}'s corpus"
        VERBATIM)
      add_custom_target(qiven3_fuzz_${target}_seeds ALL DEPENDS "${dir}/seeded")
    endif()
  endforeach()
  file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/tests/fuzz_targets.txt" CONTENT "${fuzz_lines}")
endif()
