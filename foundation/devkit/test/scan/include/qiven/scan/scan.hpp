#pragma once

// scan: the raw-byte scans of a directory's files (a store's directory, in
// recordlog's scan_test.cpp). The patterns are bytes with a name for the
// report: a marker in UTF-8 and in UTF-16LE. A scan reads every regular
// file under a directory, its subdirectories included, in path order, and
// lists each and each hit with its file, its pattern's name and its offset;
// a file it cannot read is a hit named "unread", so that nothing passes
// unscanned.
//
// Allocations: each file's bytes in a string, the report's; test side.
// Platform calls: none; the files by the standard library. thread: any.

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qiven::test::scan {

struct Pattern {
    std::string name;
    std::string bytes;
};

// UTF-8's code points as UTF-16LE's bytes; invalid UTF-8 is a
// std::invalid_argument.
std::string utf16le(std::string_view utf8);

// "<name> utf-8" and "<name> utf-16le".
std::vector<Pattern> marker(std::string_view name, std::string_view utf8);

struct File {
    std::filesystem::path path;
};
struct Hit {
    std::filesystem::path path;
    std::string pattern;
    std::size_t offset = 0;
};
struct Report {
    std::vector<File> files;
    std::vector<Hit> hits;
    // Whether a file of this name (its last part) was scanned.
    [[nodiscard]] bool lists(std::string_view filename) const;
};

Report scan(const std::filesystem::path& directory, std::span<const Pattern> patterns);

}  // namespace qiven::test::scan
