// scan's own cases: the patterns' bytes, UTF-16LE's pairs and windows
// among them; and a scan over a directory with a subdirectory finding a
// marker in UTF-8 and in UTF-16LE and a key's hex, each file listed with its
// size, in a scratch directory of the case's own.

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "qiven/runner/runner.hpp"
#include "qiven/scan/scan.hpp"

namespace {

namespace scan = qiven3::test::scan;
namespace fs = std::filesystem;

// A fresh directory under the system's temporary one, removed at its end.
class Scratch {
public:
    Scratch() : root_(fresh()) {}
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        try {
            std::error_code error;
            fs::remove_all(root_, error);
        } catch (const std::exception&) {
            static_cast<void>(std::fputs("scan: the scratch directory was not removed\n", stderr));
        }
    }

    [[nodiscard]] const fs::path& root() const noexcept { return root_; }

private:
    static fs::path fresh() {
        // On the heap: libstdc++'s random_device is some 5,000 bytes.
        std::vector<std::random_device> device(1);
        std::random_device& draw = device.front();
        fs::path out = fs::temp_directory_path() / ("qiven3-scan-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(out);
        return out;
    }

    fs::path root_;
};

void put(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

std::vector<scan::Pattern> joined(std::vector<scan::Pattern> a, const std::vector<scan::Pattern>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace

QIVEN_CASE(scan, patterns) {
    // U+00E9, U+20AC and U+1F600: two, three and four bytes, the last a pair.
    const std::string text = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80";
    CHECK_EQ(scan::utf16le(text), std::string("a\0\xE9\0\xAC\x20\x3D\xD8\x00\xDE", 10));
    CHECK_EQ(scan::hex("\x01\xAB"), std::string("01ab"));
    CHECK_EQ(scan::hex("\x01\xAB", true), std::string("01AB"));
    bool refused = false;
    try {
        (void)scan::utf16le("\xC3");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    CHECK(refused);
    const std::vector<scan::Pattern> m = scan::marker("m", "MARK");
    REQUIRE(m.size() == 2U);
    CHECK_EQ(m[0].name, std::string("m utf-8"));
    CHECK_EQ(m[1].bytes, std::string("M\0A\0R\0K\0", 8));
    const std::vector<scan::Pattern> w = scan::windows(m[0], 2);
    REQUIRE(w.size() == 3U);
    CHECK_EQ(w[2].name + "=" + w[2].bytes, std::string("m utf-8 @2=RK"));
    CHECK_EQ(scan::windows(m[0], 4).size(), 1U);
}

QIVEN_CASE(scan, directory) {
    const Scratch scratch;
    const fs::path dir = scratch.root() / "scanned";
    const std::string key = "\x10\x20\xFE";
    put(dir / "a.bin", "xxMARKxx");
    put(dir / "sub" / "b.bin", std::string("..M\0A\0R\0K\0..", 12));
    put(dir / "c.txt", "key 1020fe here");
    put(dir / "d.txt", "nothing");
    const std::vector<scan::Pattern> patterns = joined(scan::marker("marker", "MARK"), scan::key("key", key));
    const scan::Report r = scan::scan(dir, patterns);
    REQUIRE(r.files.size() == 4U);
    CHECK(r.lists("d.txt"));
    CHECK(!r.lists("e.txt"));
    CHECK_EQ(r.files[0].size, 8U);
    CHECK_EQ(r.describe(), (dir / "a.bin").generic_string() + " marker utf-8 2\n" + (dir / "c.txt").generic_string() +
                               " key hex 4\n" + (dir / "sub" / "b.bin").generic_string() + " marker utf-16le 2\n");
    CHECK(scan::scan(scratch.root() / "absent", patterns).hits.size() == 1U);
}
