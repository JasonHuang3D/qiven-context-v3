#pragma once

// scan: the raw-byte scans (S11; the forgetting fixture's). The patterns
// are bytes with a name for the report: a marker in UTF-8 and in UTF-16LE,
// a key's hex in lower and upper case, and every window of a given width
// of a pattern's bytes, for one split across pages. A scan reads every
// regular file under a directory, its subdirectories included, in path
// order, and lists each with its size as read and each hit with its file,
// its pattern's name and its offset; a file it cannot read is a hit named
// "unread", so that nothing passes unscanned.
//
// Allocations: each file's bytes in a string, the report's; test side.
// Platform calls: none; the files by the standard library. thread: any.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qiven3::test::scan {

struct Pattern {
    std::string name;
    std::string bytes;
};

// UTF-8's code points as UTF-16LE's bytes; invalid UTF-8 is a
// std::invalid_argument.
std::string utf16le(std::string_view utf8);
// bytes as hex digits, lower case or upper.
std::string hex(std::string_view bytes, bool upper = false);

// "<name> utf-8" and "<name> utf-16le".
std::vector<Pattern> marker(std::string_view name, std::string_view utf8);
// "<name> hex" and "<name> HEX", the key's bytes as hex.
std::vector<Pattern> key(std::string_view name, std::string_view bytes);
// Every width-byte window of the pattern's bytes, "<its name> @<start>";
// the pattern itself where it is not longer than width.
std::vector<Pattern> windows(const Pattern& pattern, std::size_t width);

struct File {
    std::filesystem::path path;
    std::uintmax_t size = 0;
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
    // Each hit, one a line: path, pattern, offset.
    [[nodiscard]] std::string describe() const;
};

Report scan(const std::filesystem::path& directory, std::span<const Pattern> patterns);

}  // namespace qiven3::test::scan
