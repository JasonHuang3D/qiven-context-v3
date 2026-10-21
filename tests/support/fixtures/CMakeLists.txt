# qiven3_fixtures: tests/fixtures/ loaded through canon::parse. Configure
# writes each fixture file, tests/fixtures/<name>.json, into
# fixture_files.cpp as its bytes, so that a binary reads no path; an edit
# to a file, or a file added or removed, configures again.
set(fixtures_dir "${PROJECT_SOURCE_DIR}/tests/fixtures")
file(GLOB fixture_files CONFIGURE_DEPENDS "${fixtures_dir}/*.json")
list(SORT fixture_files)
set(arrays "")
set(names "")
set(views "")
set(count 0)
foreach(file IN LISTS fixture_files)
  get_filename_component(name "${file}" NAME)
  if(NOT name MATCHES "^[a-z][a-z0-9_]*\\.json$")
    message(FATAL_ERROR "qiven3: a fixture's name is not a stem of small letters, digits and _: ${name}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
  file(READ "${file}" hex HEX)
  string(LENGTH "${hex}" digits)
  math(EXPR size "${digits} / 2")
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," bytes "${hex}")
  string(REGEX REPLACE "(('\\\\x[0-9a-f][0-9a-f]',){16})" "\\1\n    " bytes "${bytes}")
  string(APPEND arrays "constexpr std::array<char, ${size}> file_${count} = {\n    ${bytes}};\n")
  string(APPEND names "    \"${name}\",\n")
  string(APPEND views "    std::string_view(file_${count}.data(), file_${count}.size()),\n")
  math(EXPR count "${count} + 1")
endforeach()
set(fixtures_source "${CMAKE_CURRENT_BINARY_DIR}/fixture_files.cpp")
file(GENERATE OUTPUT "${fixtures_source}" CONTENT "// Written by tests/support/fixtures/CMakeLists.txt at configure: the files of
// tests/fixtures/, each its bytes.
#include <array>
#include <cstddef>
#include <string_view>

namespace {
${arrays}
constexpr std::array<std::string_view, ${count}> names = {
${names}};
constexpr std::array<std::string_view, ${count}> views = {
${views}};
}  // namespace

namespace qiven3::test::fixtures::generated {
std::size_t count() noexcept { return ${count}; }
std::string_view name(std::size_t i) noexcept { return names[i]; }
std::string_view data(std::size_t i) noexcept { return views[i]; }
}  // namespace qiven3::test::fixtures::generated
")

add_library(qiven3_fixtures STATIC
  fixtures.cpp
  fixtures.hpp
  "${fixtures_source}"
)
target_include_directories(qiven3_fixtures PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_fixtures PUBLIC qiven3_base PRIVATE qiven3_flags)
