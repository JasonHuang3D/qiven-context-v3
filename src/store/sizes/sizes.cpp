// sqlite: a file's size, read by the standard library's file system
// reads, for open's presence check (a store of 0 bytes) and for size's
// figures (the store's file, its -wal and -shm). It sits in a directory of
// its own for its analysis configuration: MSVC's file_size combines its
// stats flags, an enum declared without their combinations, which the
// analyzer reads as a cast out of the enum's range.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "store/engine/sqlite.hpp"

namespace qiven3::store::sqlite {

std::optional<std::uint64_t> file_bytes(std::string_view path, std::string_view suffix) {
    namespace fs = std::filesystem;
    std::u8string text;
    for (const char c : path) {
        text.push_back(static_cast<char8_t>(c));
    }
    for (const char c : suffix) {
        text.push_back(static_cast<char8_t>(c));
    }
    std::error_code error;
    const fs::path p(text);
    if (!fs::is_regular_file(fs::status(p, error))) {
        return std::nullopt;
    }
    const std::uintmax_t size = fs::file_size(p, error);
    return error ? std::nullopt : std::optional<std::uint64_t>(size);
}

}  // namespace qiven3::store::sqlite
