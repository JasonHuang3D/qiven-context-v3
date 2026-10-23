#include "support/fixtures/fixtures.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace qiven3::test::fixtures {

// fixture_files.cpp's, which configure writes: how many files, and each
// one's name and bytes, of tests/fixtures/ and of tests/vectors/.
namespace generated {
std::size_t count() noexcept;
std::string_view name(std::size_t i) noexcept;
std::string_view data(std::size_t i) noexcept;
std::size_t vector_count() noexcept;
std::string_view vector_name(std::size_t i) noexcept;
std::string_view vector_data(std::size_t i) noexcept;
}  // namespace generated

std::optional<std::string_view> bytes(std::string_view file) noexcept {
    for (std::size_t i = 0; i < generated::count(); ++i) {
        if (generated::name(i) == file) {
            return generated::data(i);
        }
    }
    return std::nullopt;
}

std::size_t file_count() noexcept { return generated::count(); }

std::string_view file_name(std::size_t i) noexcept {
    if (i >= generated::count()) {
        memory::fail_fast("fixtures: a file past the count");
    }
    return generated::name(i);
}

std::optional<std::string_view> vector_bytes(std::string_view file) noexcept {
    for (std::size_t i = 0; i < generated::vector_count(); ++i) {
        if (generated::vector_name(i) == file) {
            return generated::vector_data(i);
        }
    }
    return std::nullopt;
}

std::size_t vector_file_count() noexcept { return generated::vector_count(); }

std::string_view vector_file_name(std::size_t i) noexcept {
    if (i >= generated::vector_count()) {
        memory::fail_fast("fixtures: a vectors file past the count");
    }
    return generated::vector_name(i);
}

}  // namespace qiven3::test::fixtures
