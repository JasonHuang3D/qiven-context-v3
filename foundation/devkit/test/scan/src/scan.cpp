// scan: the raw-byte scans (scan.hpp).

#include "qiven/scan/scan.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace qiven::test::scan {

namespace {

void put16(std::string& out, std::uint32_t unit) {
    out.push_back(static_cast<char>(unit & 0xFFU));
    out.push_back(static_cast<char>((unit >> 8U) & 0xFFU));
}

}  // namespace

std::string utf16le(std::string_view utf8) {
    std::string out;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        std::size_t n = 0;
        if (lead < 0x80U) {
            n = 1;
        } else if ((lead >> 5U) == 6U) {
            n = 2;
        } else if ((lead >> 4U) == 14U) {
            n = 3;
        } else if ((lead >> 3U) == 30U) {
            n = 4;
        }
        if (n == 0 || i + n > utf8.size()) {
            throw std::invalid_argument("scan: not UTF-8");
        }
        std::uint32_t cp = n == 1 ? lead : lead & (0x7FU >> n);
        for (std::size_t k = 1; k < n; ++k) {
            const auto c = static_cast<unsigned char>(utf8[i + k]);
            if ((c >> 6U) != 2U) {
                throw std::invalid_argument("scan: not UTF-8");
            }
            cp = (cp << 6U) | (c & 0x3FU);
        }
        if (cp < 0x10000U) {
            put16(out, cp);
        } else {
            put16(out, 0xD800U + ((cp - 0x10000U) >> 10U));
            put16(out, 0xDC00U + ((cp - 0x10000U) & 0x3FFU));
        }
        i += n;
    }
    return out;
}

std::vector<Pattern> marker(std::string_view name, std::string_view utf8) {
    return {Pattern{std::string(name) + " utf-8", std::string(utf8)},
            Pattern{std::string(name) + " utf-16le", utf16le(utf8)}};
}

bool Report::lists(std::string_view filename) const {
    return std::any_of(files.begin(), files.end(), [&](const File& f) { return f.path.filename() == filename; });
}

Report scan(const std::filesystem::path& directory, std::span<const Pattern> patterns) {
    Report report;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error)) {
        if (it->is_regular_file(error)) {
            report.files.push_back(File{it->path()});
        }
    }
    if (error) {
        report.hits.push_back(Hit{directory, "unread", 0});
    }
    std::sort(report.files.begin(), report.files.end(), [](const File& a, const File& b) { return a.path < b.path; });
    for (const File& f : report.files) {
        std::ifstream in(f.path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!in.is_open()) {
            report.hits.push_back(Hit{f.path, "unread", 0});
            continue;
        }
        for (const Pattern& p : patterns) {
            for (std::size_t at = bytes.find(p.bytes); !p.bytes.empty() && at != std::string::npos;
                 at = bytes.find(p.bytes, at + 1)) {
                report.hits.push_back(Hit{f.path, p.name, at});
            }
        }
    }
    return report;
}

}  // namespace qiven::test::scan
