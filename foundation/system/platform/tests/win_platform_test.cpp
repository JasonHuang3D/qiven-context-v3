// The Windows adapter's own members, in this process, on Windows alone:
// config_dir's override as given, made only by create, and its errors,
// the scratch directory of each case's own under the job's temporary
// directory; its default location, the known folder LocalAppData's leaf,
// which LOCALAPPDATA moved, unset or relative does not move; normcase's
// forms by LCMapStringEx's invariant lowercase; wipe's zeros; random_bytes'
// draws differing; now beside the system's clock; pages zeroed in whole
// pages, a count the system refuses memory; resolve, replace and a Staged's
// open refusing a path not UTF-8. No Windows header here (B5): the
// environment by the C runtime's _putenv_s.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "qiven/memory/arena.hpp"
#include "qiven/platform/windows/windows.hpp"
#include "qiven/runner/runner.hpp"

namespace {

namespace fs = std::filesystem;
using qiven::memory::Allocator;
using qiven::memory::Arena;
using qiven::platform::Code;
using qiven::platform::PlatformError;
using qiven::platform::Seconds;
using qiven::platform::String;
using qiven::platform::win::WindowsPlatform;

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
        root = fs::temp_directory_path() / ("qiven-win-platform-" + std::to_string(draw()) + std::to_string(draw()));
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

// An environment variable as the process holds it: its value, or none.
std::optional<std::string> variable(const char* name) {
    char* value = nullptr;
    std::size_t n = 0;
    std::optional<std::string> out;
    if (_dupenv_s(&value, &n, name) == 0 && value != nullptr) {
        out = std::string(value);
    }
    std::free(value);
    return out;
}

// An environment variable set, or removed for none, for the guard's life,
// then put back as it was.
struct Variable {
    Variable(const char* name_, const std::optional<std::string>& value) : name(name_), was(variable(name_)) {
        set(value);
    }
    Variable(const Variable&) = delete;
    Variable& operator=(const Variable&) = delete;
    Variable(Variable&&) = delete;
    Variable& operator=(Variable&&) = delete;
    ~Variable() { set(was); }
    void set(const std::optional<std::string>& value) const {
        static_cast<void>(_putenv_s(name, value ? value->c_str() : ""));
    }
    const char* name;
    std::optional<std::string> was;
};

// The code and operation of the PlatformError op throws; unsupported and
// "none thrown" when it returns.
struct Thrown {
    Code code = Code::unsupported;
    std::string operation = "none thrown";
};

template <class Op>
Thrown thrown(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return {error.code, std::string(error.operation.view())};
    }
    return {};
}

// Pages for an arena, off the stack.
struct Pages {
    std::array<std::byte, 1U << 16U> bytes{};
};

// normcase's paths, as UTF-8, and their forms: '/' as '\', each letter by
// the invariant lowercase, which leaves U+0130, U+01C5 and U+00DF and folds
// U+03A3, U+FF21 and U+10400, beyond the BMP.
constexpr std::array<std::pair<std::string_view, std::string_view>, 9> normcase_forms{{
    {"C:/Users/ABC", "c:\\users\\abc"},
    {"D:\\\xc3\x84\\\xc3\x96x", "d:\\\xc3\xa4\\\xc3\xb6x"},
    {"C:\\\xc4\xb0", "c:\\\xc4\xb0"},
    {"C:\\\xc7\x85", "c:\\\xc7\x85"},
    {"C:\\\xc3\x9f", "c:\\\xc3\x9f"},
    {"C:\\\xce\xa3\xce\xa3", "c:\\\xcf\x83\xcf\x83"},
    {"C:\\\xef\xbc\xa1", "c:\\\xef\xbd\x81"},
    {"C:\\\xf0\x90\x90\x80", "c:\\\xf0\x90\x90\xa8"},
    {"", ""},
}};

// replace's deadline.
constexpr Seconds five{5.0};

// The milliseconds since the epoch of a time.
std::int64_t ms_of(std::chrono::sys_time<std::chrono::milliseconds> at) { return at.time_since_epoch().count(); }

// The bytes all zero.
bool zeroed(std::span<const std::byte> bytes) {
    return std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0}; });
}

// The names a directory holds, sorted.
std::vector<std::string> names_in(const fs::path& directory) {
    std::vector<std::string> out;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        out.push_back(text(entry.path().filename()));
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace

// The override, as given: config_dir gives it and makes nothing; with
// create, a missing one made, a present one kept with what it holds, a file
// in its place exists and kept, a missing parent io and not made; a
// relative override path, with or without create, nothing made where the
// process runs.
QIVEN_CASE(win_platform, config_override) {
    const Scratch scratch;
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String out{Allocator<char>(arena)};
        const fs::path over = scratch.root / "over" / "qiven-config";
        fs::create_directory(over.parent_path());
        WindowsPlatform platform(text(over));
        platform.config_dir(false, out);
        CHECK_EQ(std::string(out), text(over));
        CHECK(!fs::exists(over));

        platform.config_dir(true, out);
        CHECK_EQ(std::string(out), text(over));
        CHECK(fs::is_directory(over));
        write_bytes(over / "kept", "kept");
        platform.config_dir(true, out);
        CHECK_EQ(std::string(out), text(over));
        CHECK_EQ(bytes_of(over / "kept"), std::string("kept"));
        fs::remove_all(over);
        write_bytes(over, "a file");
        const Thrown file = thrown([&] { platform.config_dir(true, out); });
        CHECK_EQ(file.code, Code::exists);
        CHECK_EQ(file.operation, std::string("config_dir"));
        CHECK_EQ(bytes_of(over), std::string("a file"));

        const fs::path orphan = scratch.root / "missing" / "qiven-config";
        WindowsPlatform orphaned(text(orphan));
        const Thrown missing = thrown([&] { orphaned.config_dir(true, out); });
        CHECK_EQ(missing.code, Code::io);
        CHECK_EQ(missing.operation, std::string("config_dir"));
        CHECK(!fs::exists(orphan.parent_path()));

        const Here here(scratch.root);
        WindowsPlatform relative("qiven-relative");
        for (const bool create : {false, true}) {
            const Thrown refused = thrown([&] { relative.config_dir(create, out); });
            CHECK_EQ(refused.code, Code::path);
            CHECK_EQ(refused.operation, std::string("config_dir"));
        }
        CHECK(!fs::exists(scratch.root / "qiven-relative"));
    }
    CHECK_EQ(arena.used(), 0U);
}

// No override: the known folder LocalAppData's qiven3, absolute, which the
// process's LOCALAPPDATA names where the system set it; LOCALAPPDATA moved,
// unset and relative each give the same, and nothing is made under the
// moved one.
QIVEN_CASE(win_platform, config_default) {
    const Scratch scratch;
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String out{Allocator<char>(arena)};
        WindowsPlatform first(std::nullopt);
        first.config_dir(false, out);
        const std::string expected(out);
        const fs::path found = fs::path(std::u8string(expected.begin(), expected.end()));
        CHECK(found.is_absolute());
        CHECK_EQ(text(found.filename()), std::string("qiven3"));
        if (const std::optional<std::string> set = variable("LOCALAPPDATA")) {
            CHECK_EQ(expected, *set + "\\qiven3");
        }
        const fs::path elsewhere = scratch.root / "elsewhere";
        for (const std::optional<std::string>& value :
             {std::optional<std::string>(text(elsewhere)), std::optional<std::string>(),
              std::optional<std::string>("relative")}) {
            const Variable moved("LOCALAPPDATA", value);
            WindowsPlatform platform(std::nullopt);
            platform.config_dir(false, out);
            CHECK_EQ(std::string(out), expected);
        }
        CHECK(!fs::exists(elsewhere / "qiven3"));
    }
    CHECK_EQ(arena.used(), 0U);
}

// normcase of each path: its form; of the empty path, empty.
QIVEN_CASE(win_platform, normcase) {
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        WindowsPlatform platform(std::nullopt);
        String out{Allocator<char>(arena)};
        for (const auto& [path, form] : normcase_forms) {
            platform.normcase(path, out);
            CHECK_EQ(std::string(out), std::string(form));
        }
    }
    CHECK_EQ(arena.used(), 0U);
}

// wipe of 32 bytes filled with 0xa5: each zero after; of an empty span, of
// none and of the filled bytes' first 0, nothing changed.
QIVEN_CASE(win_platform, wipe) {
    WindowsPlatform platform(std::nullopt);
    std::array<std::byte, 32> bytes{};
    bytes.fill(std::byte{0xa5});
    platform.wipe(bytes);
    CHECK(std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0}; }));
    bytes.fill(std::byte{0xa5});
    platform.wipe(std::span<std::byte>());
    platform.wipe(std::span<std::byte>(bytes).first(0));
    CHECK(std::ranges::all_of(bytes, [](std::byte b) { return b == std::byte{0xa5}; }));
}

// random_bytes(32) twice: the two differing.
QIVEN_CASE(win_platform, random_bytes) {
    WindowsPlatform platform(std::nullopt);
    std::array<std::byte, 32> first{};
    std::array<std::byte, 32> second{};
    platform.random_bytes(first);
    platform.random_bytes(second);
    CHECK(first != second);
}

// now(): the system's clock in milliseconds, within 1 s of the case's own
// reads before and after it.
QIVEN_CASE(win_platform, now) {
    using std::chrono::floor;
    using std::chrono::milliseconds;
    using std::chrono::system_clock;
    const WindowsPlatform platform(std::nullopt);
    const std::int64_t before = ms_of(floor<milliseconds>(system_clock::now()));
    const std::int64_t now = ms_of(platform.now());
    const std::int64_t after = ms_of(floor<milliseconds>(system_clock::now()));
    CHECK(now >= before - 1000);
    CHECK(now <= after + 1000);
}

// pages(1), pages(5000) and pages(4096): 4,096, 8,192 and 4,096 bytes,
// zeroed, each given back; (1 << 53) - 1 bytes, which the system refuses:
// memory.
QIVEN_CASE(win_platform, pages) {
    WindowsPlatform platform(std::nullopt);
    {
        const auto first = platform.pages(1);
        const auto second = platform.pages(5000);
        const auto third = platform.pages(4096);
        CHECK_EQ(first.bytes().size(), 4096U);
        CHECK_EQ(second.bytes().size(), 8192U);
        CHECK_EQ(third.bytes().size(), 4096U);
        CHECK(zeroed(first.bytes()) && zeroed(second.bytes()) && zeroed(third.bytes()));
    }
    const Thrown refused = thrown([&] { static_cast<void>(platform.pages((std::size_t{1} << 53U) - 1)); });
    CHECK(refused.code == Code::memory);
    CHECK_EQ(refused.operation, std::string("pages"));
}

// A path holding 0xff, then a lone surrogate's three bytes, to resolve,
// replace and a Staged's open: path each time; nothing written in the
// scratch directory, the staging directory left empty.
QIVEN_CASE(win_platform, not_utf8) {
    const Scratch scratch;
    const fs::path inside = scratch.root / "staged";
    fs::create_directory(inside);
    const std::vector<std::string> before = names_in(scratch.root);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        WindowsPlatform platform(std::nullopt);
        String out{Allocator<char>(arena)};
        for (const std::string_view bad : {std::string_view("\xff"), std::string_view("\xed\xa0\x80")}) {
            const std::string path = text(scratch.root) + "\\a" + std::string(bad) + ".txt";
            const Thrown resolved = thrown([&] { platform.resolve(path, out); });
            CHECK(resolved.code == Code::path);
            CHECK_EQ(resolved.operation, std::string("resolve"));
            const Thrown replaced = thrown([&] { platform.replace(path, "00", five); });
            CHECK(replaced.code == Code::path);
            CHECK_EQ(replaced.operation, std::string("replace"));
            auto staged = platform.stage(text(inside));
            const Thrown opened = thrown([&] { static_cast<void>(staged.open("a" + std::string(bad))); });
            CHECK(opened.code == Code::path);
            CHECK_EQ(opened.operation, std::string("open"));
            staged.discard();
        }
    }
    CHECK_EQ(arena.used(), 0U);
    CHECK(names_in(scratch.root) == before);
    CHECK(fs::is_empty(inside));
}
