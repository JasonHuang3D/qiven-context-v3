#include "support/fixtures/fixtures.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace qiven3::test::fixtures {

// fixture_files.cpp's, which configure writes: how many files, and each
// one's name and bytes.
namespace generated {
std::size_t count() noexcept;
std::string_view name(std::size_t i) noexcept;
std::string_view data(std::size_t i) noexcept;
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

}  // namespace qiven3::test::fixtures
