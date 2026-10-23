# qiven3_fixtures: tests/fixtures/ and tests/vectors/ loaded through
# canon::parse. Configure writes each file, tests/fixtures/<name>.json and
# tests/vectors/<name>.json, into fixture_files.cpp as its bytes, so that a
# binary reads no path; an edit to a file, or a file added or removed,
# configures again.

# The arrays, names and views of the JSON files of dir, each array named
# <tag>_<n>, into <tag>_arrays, <tag>_names, <tag>_views and <tag>_count.
function(qiven3_embed_files dir tag)
  file(GLOB files CONFIGURE_DEPENDS "${dir}/*.json")
  list(SORT files)
  set(arrays "")
  set(names "")
  set(views "")
  set(count 0)
  foreach(file IN LISTS files)
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
    string(APPEND arrays "constexpr std::array<char, ${size}> ${tag}_${count} = {\n    ${bytes}};\n")
    string(APPEND names "    \"${name}\",\n")
    string(APPEND views "    std::string_view(${tag}_${count}.data(), ${tag}_${count}.size()),\n")
    math(EXPR count "${count} + 1")
  endforeach()
  set(${tag}_arrays "${arrays}" PARENT_SCOPE)
  set(${tag}_names "${names}" PARENT_SCOPE)
  set(${tag}_views "${views}" PARENT_SCOPE)
  set(${tag}_count "${count}" PARENT_SCOPE)
endfunction()

qiven3_embed_files("${PROJECT_SOURCE_DIR}/tests/fixtures" file)
qiven3_embed_files("${PROJECT_SOURCE_DIR}/tests/vectors" vector)
set(fixtures_source "${CMAKE_CURRENT_BINARY_DIR}/fixture_files.cpp")
file(GENERATE OUTPUT "${fixtures_source}" CONTENT "// Written by tests/support/fixtures/CMakeLists.txt at configure: the files of
// tests/fixtures/ and of tests/vectors/, each its bytes.
#include <array>
#include <cstddef>
#include <string_view>

namespace {
${file_arrays}
constexpr std::array<std::string_view, ${file_count}> names = {
${file_names}};
constexpr std::array<std::string_view, ${file_count}> views = {
${file_views}};
${vector_arrays}
constexpr std::array<std::string_view, ${vector_count}> vector_names = {
${vector_names}};
constexpr std::array<std::string_view, ${vector_count}> vector_views = {
${vector_views}};
}  // namespace

namespace qiven3::test::fixtures::generated {
std::size_t count() noexcept { return ${file_count}; }
std::string_view name(std::size_t i) noexcept { return names[i]; }
std::string_view data(std::size_t i) noexcept { return views[i]; }
std::size_t vector_count() noexcept { return ${vector_count}; }
std::string_view vector_name(std::size_t i) noexcept { return vector_names[i]; }
std::string_view vector_data(std::size_t i) noexcept { return vector_views[i]; }
}  // namespace qiven3::test::fixtures::generated
")

add_library(qiven3_fixtures STATIC
  fixtures.cpp
  fixtures.hpp
  patterns.hpp
  vectors.hpp
  view.hpp
  "${fixtures_source}"
)
target_include_directories(qiven3_fixtures PUBLIC "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(qiven3_fixtures PUBLIC qiven3_base PRIVATE qiven3_flags)
