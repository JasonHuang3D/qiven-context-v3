// files' list, remove and rename on the fake's system calls, on real files
// in each case's scratch directory: list's order by UTF-8 bytes whatever
// the system gives, never . or .., not recursing, its errors and its
// names on the caller's arena counted; remove's none there, busy with no
// retry and its errors; rename's exists with both kept, its retries until
// the deadline then busy, one try for a deadline of 0 or not a number,
// and its errors; each system call and each allocation failed in turn.

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "platform/files.hpp"
#include "platform/names.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace fs = std::filesystem;
namespace files = qiven3::platform::files;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::platform::Code;
using qiven3::platform::PlatformError;
using qiven3::platform::Seconds;
using qiven3::platform::fake::Busy;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakeFiles;
using qiven3::platform::fake::FakePlatform;
using qiven3::platform::fake::never;
using namespace std::chrono_literals;

static_assert(files::error_name(267) == "ERROR_DIRECTORY");
static_assert(files::error_name(1113) == "ERROR_NO_UNICODE_TRANSLATION");

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

fs::path utf8(std::string_view name) {
    std::u8string units;
    for (const char c : name) {
        units.push_back(static_cast<char8_t>(c));
    }
    return fs::path(units);
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// The names, '|'-joined.
std::string joined(const Vector<String>& names) {
    std::string out;
    for (const String& name : names) {
        out += (out.empty() ? "" : "|") + std::string(name);
    }
    return out;
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven3-names-" + std::to_string(draw()) + std::to_string(draw()));
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
    [[nodiscard]] Config config() const {
        Config config;
        config.root = text(root);
        config.seed = 7;
        return config;
    }
    [[nodiscard]] std::string at(std::string_view name) const { return text(root / utf8(name)); }
    fs::path root;
};

// An arena's pages, on the heap: past the frame bound.
struct Pages {
    alignas(64) std::array<std::byte, 64 * 1024> bytes{};
};

template <class Op>
std::pair<Code, std::string> thrown(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return {error.code, std::string(error.detail.view())};
    }
    return {Code::io, "none thrown"};
}

constexpr Seconds five{5.0};

}  // namespace

// Sorted by UTF-8 bytes, which neither the fake's order (the reverse) nor
// NTFS's (case folded) is; . and .. never; a directory's own names not.
QIVEN3_CASE(files_names, list) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    FakeFiles& system = fake.files();
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    Vector<String> names{Allocator<String>(arena)};
    for (const std::string_view name : {"Zeta", "alpha", "Beta", "a.b", "a-b", "\xc3\xa4", "\xe4\xb8\xad"}) {
        write_bytes(scratch.root / utf8(name), "x");
    }
    fs::create_directory(scratch.root / "dir");
    write_bytes(scratch.root / "dir" / "inner", "x");
    names.emplace_back("stale", Allocator<char>(arena));

    CHECK_EQ(system.list_names(text(scratch.root), names), files::none);
    CHECK_EQ(names.size(), 11U);
    CHECK_EQ(joined(names).substr(0, 17), std::string("stale|.|..|\xe4\xb8\xad|\xc3\xa4"));
    files::list(system, text(scratch.root), names);
    CHECK_EQ(joined(names), std::string("Beta|Zeta|a-b|a.b|alpha|dir|\xc3\xa4|\xe4\xb8\xad"));
    files::list(system, scratch.at("dir"), names);
    CHECK_EQ(joined(names), std::string("inner"));
    fs::create_directory(scratch.root / "empty");
    files::list(system, scratch.at("empty"), names);
    CHECK(names.empty());
    CHECK_EQ(system.kept(), 0U);
}

QIVEN3_CASE(files_names, list_errors) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    FakeFiles& system = fake.files();
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    Vector<String> names{Allocator<String>(arena)};
    write_bytes(scratch.root / "plain", "x");
    const auto listed = [&](const std::string& directory) {
        names.emplace_back("stale", Allocator<char>(arena));
        auto result = thrown([&] { files::list(system, directory, names); });
        CHECK(names.empty());
        return result;
    };
    CHECK(listed(scratch.at("missing")) == std::pair(Code::io, std::string("list: error 3 (ERROR_PATH_NOT_FOUND)")));
    CHECK(listed(scratch.at("plain")) == std::pair(Code::io, std::string("list: error 267 (ERROR_DIRECTORY)")));
    CHECK(listed("relative") == std::pair(Code::path, std::string("not absolute")));
    CHECK(listed(text(scratch.root) + "/a\xff") == std::pair(Code::path, std::string("not UTF-8")));
    CHECK(listed(text(scratch.root) + "/") == std::pair(Code::path, std::string("no name")));

    // A name the system cannot give as UTF-8: path, naming the directory.
    const fs::path odd = scratch.root / "odd";
    fs::create_directory(odd);
#if defined(_WIN32)
    write_bytes(odd / fs::path(std::wstring(L"a") + static_cast<wchar_t>(0xD800)), "x");
#else
    write_bytes(odd / fs::path(std::string("a\xff")), "x");
#endif
    CHECK(listed(text(odd)) ==
          std::pair(Code::path, "list: error 1113 (ERROR_NO_UNICODE_TRANSLATION), a name in " + text(odd)));
}

// The names on the caller's arena, counted exactly: each name of at most
// 15 bytes is held in its String, so the arena holds the vector's array
// alone; past the budget MemoryError, names cleared.
QIVEN3_CASE(files_names, list_memory) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    for (int i = 0; i < 40; ++i) {
        write_bytes(scratch.root / ("n" + std::to_string(100 + i)), "x");
    }
    const auto pages = std::make_unique<Pages>();
    {
        Arena arena(pages->bytes);
        Vector<String> names{Allocator<String>(arena)};
        files::list(fake.files(), text(scratch.root), names);
        CHECK_EQ(names.size(), 40U);
        CHECK_EQ(arena.used(), names.capacity() * sizeof(String));
        CHECK(arena.peak() <= arena.budget());
        std::cout << "files_names.list_memory: 40 names, used " << arena.used() << ", peak " << arena.peak() << "\n";
    }
    Arena small(std::span(pages->bytes).first(512));
    Vector<String> names{Allocator<String>(small)};
    bool refused = false;
    try {
        files::list(fake.files(), text(scratch.root), names);
    } catch (const MemoryError&) {
        refused = true;
    }
    CHECK(refused);
    CHECK(names.empty());
    CHECK_EQ(fake.files().kept(), 0U);
}

// Removed; none there, twice and in a missing directory, no fault; a
// directory io, kept; held without sharing delete busy at once, kept.
QIVEN3_CASE(files_names, remove) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    FakeFiles& system = fake.files();
    const fs::path kept = scratch.root / "3.json";
    write_bytes(kept, "x");
    files::remove(system, text(kept));
    CHECK(!fs::exists(kept));
    files::remove(system, text(kept));
    files::remove(system, scratch.at("missing/3.json"));
    fs::create_directory(scratch.root / "dir");
    CHECK(thrown([&] { files::remove(system, scratch.at("dir")); }) ==
          std::pair(Code::io, std::string("delete: error 5 (ERROR_ACCESS_DENIED)")));
    CHECK(fs::is_directory(scratch.root / "dir"));

    write_bytes(kept, "x");
    fake.set_busy(Busy{.remove = 2});
    CHECK(thrown([&] { files::remove(system, text(kept)); }) ==
          std::pair(Code::busy, std::string("delete: error 32 (ERROR_SHARING_VIOLATION)")));
    CHECK_EQ(bytes_of(kept), std::string("x"));
    CHECK_EQ(system.busy().remove, 1U);  // one try: no retry
    CHECK_EQ(system.elapsed(), 0ms);
    CHECK(thrown([&] { files::remove(system, text(kept)); }).first == Code::busy);
    files::remove(system, text(kept));
    CHECK(!fs::exists(kept));

    CHECK(thrown([&] { files::remove(system, "relative"); }) == std::pair(Code::path, std::string("not absolute")));
    CHECK(thrown([&] { files::remove(system, scratch.at("a\x01")); }).first == Code::path);
    CHECK(thrown([&] { files::remove(system, text(scratch.root) + "/"); }) ==
          std::pair(Code::path, std::string("no name")));
}

// Renamed in its directory; a file or directory under the name exists,
// both kept; held, retried until the deadline then busy, both kept; one
// try for a deadline of 0 or not a number; a missing file io; a name of
// more than one component, . or .., path.
QIVEN3_CASE(files_names, rename) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    FakeFiles& system = fake.files();
    const fs::path from = scratch.root / "3.tmp";
    const fs::path to = scratch.root / "3.pending";
    write_bytes(from, "new");
    files::rename(system, text(from), "3.pending", five);
    CHECK(!fs::exists(from));
    CHECK_EQ(bytes_of(to), std::string("new"));

    write_bytes(from, "other");
    CHECK(thrown([&] { files::rename(system, text(from), "3.pending", five); }) ==
          std::pair(Code::exists, std::string("rename: error 183 (ERROR_ALREADY_EXISTS)")));
    CHECK_EQ(bytes_of(from), std::string("other"));
    CHECK_EQ(bytes_of(to), std::string("new"));
    fs::create_directory(scratch.root / "dir");
    CHECK(thrown([&] { files::rename(system, text(from), "dir", five); }).first == Code::exists);
    CHECK_EQ(bytes_of(from), std::string("other"));
    CHECK_EQ(system.elapsed(), 0ms);

    // Two refusals, 32 then 5, then renamed: 100 ms.
    fake.set_busy(Busy{.rename = 2});
    files::rename(system, text(from), "4.pending", five);
    CHECK_EQ(bytes_of(scratch.root / "4.pending"), std::string("other"));
    CHECK_EQ(system.elapsed(), 100ms);

    write_bytes(from, "held");
    fake.set_busy(Busy{.rename = never});
    CHECK(thrown([&] { files::rename(system, text(from), "5.pending", five); }) ==
          std::pair(Code::busy, std::string("rename: error 5 (ERROR_ACCESS_DENIED)")));
    CHECK_EQ(system.elapsed(), 100ms + 5000ms);
    CHECK_EQ(bytes_of(from), std::string("held"));
    CHECK(!fs::exists(scratch.root / "5.pending"));
    const std::chrono::milliseconds before = system.elapsed();
    CHECK(thrown([&] { files::rename(system, text(from), "5.pending", Seconds{0.0}); }).first == Code::busy);
    CHECK(thrown([&] { files::rename(system, text(from), "5.pending", Seconds{std::nan("")}); }).first == Code::busy);
    CHECK_EQ(system.elapsed(), before);
    fake.set_busy(Busy{});

    CHECK(thrown([&] { files::rename(system, scratch.at("missing"), "6.pending", five); }) ==
          std::pair(Code::io, std::string("rename: error 2 (ERROR_FILE_NOT_FOUND)")));
    CHECK(thrown([&] { files::rename(system, text(from), "a/b", five); }) ==
          std::pair(Code::path, std::string("more than one component")));
    CHECK(thrown([&] { files::rename(system, text(from), "..", five); }).first == Code::path);
    CHECK(thrown([&] { files::rename(system, text(from), "", five); }).first == Code::path);
    CHECK(thrown([&] { files::rename(system, text(from), "a\\b", five); }).first == Code::path);
    CHECK(thrown([&] { files::rename(system, "relative", "6.pending", five); }).first == Code::path);
    CHECK_EQ(bytes_of(from), std::string("held"));
    CHECK_EQ(system.kept(), 0U);
}

// Each system call failed in turn, then each allocation of list's names:
// a typed error, names cleared, the files as before or as done.
QIVEN3_CASE(files_names, faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    FakeFiles& system = fake.files();
    qiven3::memory::Countdown countdown;
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    Vector<String> names{Allocator<String>(arena)};
    for (const std::string_view name : {"1.pending", "2.pending", "a-long-name-past-the-small-string.json"}) {
        write_bytes(scratch.root / name, "x");
    }
    const auto clean = [&](std::size_t) {
        return (names.empty() || names.size() == 3) && fs::exists(scratch.root / "1.pending") && system.kept() == 0;
    };
    const auto runs = [&](qiven3::log::Name site, auto op, std::size_t completed, auto typed) {
        const qiven3::test::FaultRuns r =
            qiven3::test::run_faults<decltype(typed)>(qiven3_ctx.log, site, countdown, op, clean);
        std::cout << "files_names.faults: completed at " << r.completed_at << ", typed " << r.typed << "\n";
        if (completed != 0) {
            CHECK_EQ(r.completed_at, completed);
        }
        CHECK(r.completed_at > 1);
        CHECK_EQ(r.typed, r.completed_at - 1);
        CHECK_EQ(r.untyped, 0U);
        CHECK_EQ(r.unchecked, 0U);
    };
    const std::string root = text(scratch.root);
    const PlatformError platform(Code::io, "", "");
    system.attach(&countdown);
    runs("files_names.list", [&] { files::list(system, root, names); }, 2, platform);
    runs("files_names.remove", [&] {
        write_bytes(scratch.root / "3.pending", "x");
        files::remove(system, root + "/3.pending");
    }, 2, platform);
    runs("files_names.rename", [&] {
        write_bytes(scratch.root / "4.tmp", "x");
        std::error_code error;
        fs::remove(scratch.root / "4.pending", error);
        files::rename(system, root + "/4.tmp", "4.pending", five);
    }, 2, platform);
    system.attach(nullptr);
    fs::remove(scratch.root / "4.pending");
    // The allocations, into a new vector each run: its growth and the long
    // name's; how many is the standard library's.
    arena.attach(&countdown);
    runs("files_names.list_memory", [&] {
        names = Vector<String>{Allocator<String>(arena)};
        files::list(system, root, names);
    }, 0, MemoryError(qiven3::memory::MemoryCode::budget, "", ""));
    arena.attach(nullptr);
    CHECK_EQ(joined(names), std::string("1.pending|2.pending|a-long-name-past-the-small-string.json"));
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
