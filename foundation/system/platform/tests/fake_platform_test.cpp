// The fake's own members beyond files and processes: config_dir's location
// as its Config gives it, made only by create, and its errors, in a scratch
// directory of each case's own; normcase folding ASCII letters alone; wipe
// zeroing, each call recorded.

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "qiven/memory/arena.hpp"
#include "qiven/memory/memory.hpp"
#include "qiven/platform/platform.hpp"
#include "qiven/runner/runner.hpp"
#include "qiven/platform/fake/fake_platform.hpp"

namespace {

namespace fs = std::filesystem;
using qiven::memory::Allocator;
using qiven::memory::Arena;
using qiven::memory::String;
using qiven::platform::Code;
using qiven::platform::PlatformError;
using qiven::platform::fake::Config;
using qiven::platform::fake::FakePlatform;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven-fake-platform-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root, error);
    }
    // The fake's Config: its root here, its configuration location given.
    [[nodiscard]] Config config(std::string_view config_dir) const {
        Config config;
        config.root = text(root);
        config.config_dir = std::string(config_dir);
        return config;
    }
    fs::path root;
};

// The working directory moved for the guard's life, then put back.
struct Here {
    explicit Here(const fs::path& to) : was(fs::current_path()) { fs::current_path(to); }
    Here(const Here&) = delete;
    Here& operator=(const Here&) = delete;
    Here(Here&&) = delete;
    Here& operator=(Here&&) = delete;
    ~Here() {
        std::error_code error;
        fs::current_path(was, error);
    }
    fs::path was;
};

// The code and operation of the PlatformError op throws; unsupported and
// "none thrown" when it returns.
template <class Op>
std::pair<Code, std::string> thrown(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return {error.code, std::string(error.operation.view())};
    }
    return {Code::unsupported, "none thrown"};
}

// Pages for an arena, off the stack.
struct Pages {
    std::array<std::byte, 1U << 16U> bytes{};
};

// normcase's paths, as UTF-8: the Windows adapter's vectors, non-ASCII
// letters among them, which the fake keeps as they are.
constexpr std::array<std::string_view, 9> normcase_paths{
    "C:/Users/ABC", "D:\\\xc3\x84\\\xc3\x96x", "C:\\\xc4\xb0", "C:\\\xc7\x85", "C:\\\xc3\x9f",
    "C:\\\xce\xa3\xce\xa3", "C:\\\xef\xbc\xa1", "C:\\\xf0\x90\x90\x80", "",
};

// The fake's form: ASCII letters alone folded.
std::string ascii_folded(std::string_view path) {
    std::string out(path);
    for (char& c : out) {
        c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

}  // namespace

// The Config's location, as given: config_dir gives it and makes nothing;
// with create, a missing one made, a present one kept with what it holds, a
// file in its place exists and kept, a missing parent io and not made; a
// relative one path, with or without create, nothing made where the
// process runs.
QIVEN_CASE(fake_platform, config_dir) {
    const Scratch scratch;
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String out{Allocator<char>(arena)};
        const fs::path over = scratch.root / "over" / "qiven-config";
        fs::create_directory(over.parent_path());
        FakePlatform fake(scratch.config(text(over)));
        fake.config_dir(false, out);
        CHECK_EQ(std::string(out), text(over));
        CHECK(!fs::exists(over));

        fake.config_dir(true, out);
        CHECK_EQ(std::string(out), text(over));
        CHECK(fs::is_directory(over));
        write_bytes(over / "kept", "kept");
        fake.config_dir(true, out);
        CHECK_EQ(std::string(out), text(over));
        CHECK_EQ(bytes_of(over / "kept"), std::string("kept"));
        fs::remove_all(over);
        write_bytes(over, "a file");
        const auto file = thrown([&] { fake.config_dir(true, out); });
        CHECK_EQ(file.first, Code::exists);
        CHECK_EQ(file.second, std::string("config_dir"));
        CHECK_EQ(bytes_of(over), std::string("a file"));

        const fs::path orphan = scratch.root / "missing" / "qiven-config";
        FakePlatform orphaned(scratch.config(text(orphan)));
        const auto missing = thrown([&] { orphaned.config_dir(true, out); });
        CHECK_EQ(missing.first, Code::io);
        CHECK_EQ(missing.second, std::string("config_dir"));
        CHECK(!fs::exists(orphan.parent_path()));

        const Here here(scratch.root);
        FakePlatform relative(scratch.config("qiven-relative"));
        for (const bool create : {false, true}) {
            const auto refused = thrown([&] { relative.config_dir(create, out); });
            CHECK_EQ(refused.first, Code::path);
            CHECK_EQ(refused.second, std::string("config_dir"));
        }
        CHECK(!fs::exists(scratch.root / "qiven-relative"));
    }
    CHECK_EQ(arena.used(), 0U);
}

// normcase of each path: ASCII letters alone folded, '/' kept; of the
// empty path, empty.
QIVEN_CASE(fake_platform, normcase) {
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        FakePlatform fake{Config{}};
        String out{Allocator<char>(arena)};
        for (const std::string_view path : normcase_paths) {
            fake.normcase(path, out);
            CHECK_EQ(std::string(out), ascii_folded(path));
        }
    }
    CHECK_EQ(arena.used(), 0U);
}

// wipe of 32 bytes filled with 0xa5: each zero after; of an empty span,
// nothing changed; each call recorded.
QIVEN_CASE(fake_platform, wipe) {
    FakePlatform fake{Config{}};
    std::array<std::byte, 32> bytes{};
    bytes.fill(std::byte{0xa5});
    fake.wipe(bytes);
    CHECK(std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0}; }));
    bytes.fill(std::byte{0xa5});
    fake.wipe(std::span<std::byte>(bytes).first(0));
    CHECK(std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0xa5}; }));
    CHECK_EQ(std::ranges::count(fake.calls(), std::string("wipe")), 2);
}
