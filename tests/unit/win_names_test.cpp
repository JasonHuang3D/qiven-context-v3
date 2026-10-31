// win/files' list, remove and rename on the Windows adapter, on the real
// file system in a scratch directory of each case's own: list's order and
// its errors, remove's none there, its held file and its directory,
// rename's target there, file or directory, its held file, its missing
// file and a file's own name, each system error measured and printed; each
// Win32 call failed in turn, a typed error with nothing left open or
// behind.

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "memory/arena.hpp"
#include "platform/files.hpp"
#include "platform/win/files.hpp"
#include "platform/win/windows.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace fs = std::filesystem;
namespace files = qiven3::platform::files;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::String;
using qiven3::memory::Vector;
using qiven3::platform::Code;
using qiven3::platform::PlatformError;
using qiven3::platform::Seconds;
using qiven3::platform::win::WindowsFiles;
using qiven3::platform::win::WindowsPlatform;

std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

fs::path utf8(std::string_view name) {
    std::u8string out;
    for (const char c : name) {
        out.push_back(static_cast<char8_t>(c));
    }
    return {out};
}

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Whether no handle holds path without sharing delete: it renames away and
// back.
bool unheld(const fs::path& path) {
    fs::path moved = path;
    moved += ".moved";
    std::error_code error;
    fs::rename(path, moved, error);
    if (error) {
        return false;
    }
    fs::rename(moved, path, error);
    return !error;
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven3-win-names-" + std::to_string(draw()) + std::to_string(draw()));
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
    [[nodiscard]] std::string at(std::string_view name) const { return text(root / name); }
    fs::path root;
};

// The code and detail of the PlatformError op throws; io and "none thrown"
// when it returns.
struct Thrown {
    Code code = Code::io;
    std::string detail = "none thrown";
};

template <class Op>
Thrown thrown(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return {error.code, std::string(error.detail.view())};
    }
    return {};
}

// Pages for an arena, off the stack.
struct Pages {
    std::array<std::byte, 1U << 16U> bytes{};
};

std::string joined(const Vector<String>& names) {
    std::string out;
    for (const String& name : names) {
        out += out.empty() ? "" : "|";
        out += std::string_view(name);
    }
    return out;
}

// The system's error, printed beside what it measures.
files::Error measured(std::string_view what, files::Error error) {
    std::cout << "win_names: " << what << ": error " << error << " (" << files::error_name(error) << ")\n";
    return error;
}

constexpr Seconds five{5.0};
constexpr Seconds once{0.0};

}  // namespace

QIVEN_CASE(win_names, list) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    WindowsFiles system(platform);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    Vector<String> names{Allocator<String>(arena)};
    for (const std::string_view name : {"Zeta", "alpha", "Beta", "a.b", "\xc3\xa4", "\xe4\xb8\xad"}) {
        write_bytes(scratch.root / utf8(name), "x");
    }
    fs::create_directory(scratch.root / "dir");
    write_bytes(scratch.root / "dir" / "inner", "x");
    platform.list(text(scratch.root), names);
    CHECK_EQ(joined(names), std::string("Beta|Zeta|a.b|alpha|dir|\xc3\xa4|\xe4\xb8\xad"));
    fs::create_directory(scratch.root / "empty");
    platform.list(scratch.at("empty"), names);
    CHECK(names.empty());

    CHECK_EQ(measured("list, a missing directory", system.list_names(scratch.at("missing"), names)),
             files::path_not_found);
    CHECK_EQ(measured("list, a missing parent", system.list_names(scratch.at("missing/child"), names)),
             files::path_not_found);
    CHECK_EQ(measured("list, a file", system.list_names(scratch.at("Zeta"), names)), files::not_a_directory);
    names.clear();
    for (const std::string_view given : {"missing", "missing/child", "Zeta"}) {
        CHECK(thrown([&] { platform.list(scratch.at(given), names); }).code == Code::io);
        CHECK(names.empty());
    }
    CHECK(thrown([&] { platform.list("relative", names); }).code == Code::path);
}

QIVEN_CASE(win_names, remove) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    WindowsFiles system(platform);
    const fs::path file = scratch.root / "file";
    write_bytes(file, "x");
    platform.remove(text(file));
    CHECK(!fs::exists(file));
    CHECK_EQ(measured("remove, none there", system.remove_file(text(file))), files::file_not_found);
    platform.remove(text(file));
    CHECK_EQ(measured("remove, a missing parent", system.remove_file(scratch.at("missing/child"))),
             files::path_not_found);
    platform.remove(scratch.at("missing/child"));

    fs::create_directory(scratch.root / "dir");
    CHECK_EQ(measured("remove, a directory", system.remove_file(scratch.at("dir"))), files::access_denied);
    const Thrown directory = thrown([&] { platform.remove(scratch.at("dir")); });
    CHECK(directory.code == Code::io);
    CHECK_EQ(directory.detail, std::string("delete: error 5 (ERROR_ACCESS_DENIED)"));
    CHECK(fs::is_directory(scratch.root / "dir"));

    write_bytes(file, "held");
    {
        const std::ifstream reader(file, std::ios::binary);  // shares read and write, not delete
        CHECK_EQ(measured("remove, held", system.remove_file(text(file))), files::sharing_violation);
        CHECK(thrown([&] { platform.remove(text(file)); }).code == Code::busy);
    }
    CHECK_EQ(bytes_of(file), std::string("held"));
    CHECK(thrown([&] { platform.remove("relative"); }).code == Code::path);
}

QIVEN_CASE(win_names, rename) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    WindowsFiles system(platform);
    const fs::path file = scratch.root / "file";
    write_bytes(file, "x");
    platform.rename(text(file), "renamed", five);
    CHECK(!fs::exists(file));
    CHECK_EQ(bytes_of(scratch.root / "renamed"), std::string("x"));
    CHECK_EQ(measured("rename, a missing file", system.rename_file(text(file), "other")), files::file_not_found);
    CHECK(thrown([&] { platform.rename(text(file), "other", five); }).code == Code::io);

    write_bytes(file, "file");
    CHECK_EQ(measured("rename, a file there", system.rename_file(text(file), "renamed")), files::already_exists);
    CHECK(thrown([&] { platform.rename(text(file), "renamed", five); }).code == Code::exists);
    fs::create_directory(scratch.root / "dir");
    CHECK_EQ(measured("rename, a directory there", system.rename_file(text(file), "dir")), files::already_exists);
    CHECK(thrown([&] { platform.rename(text(file), "dir", five); }).code == Code::exists);
    CHECK_EQ(bytes_of(file), std::string("file"));
    CHECK_EQ(bytes_of(scratch.root / "renamed"), std::string("x"));
    CHECK(fs::is_directory(scratch.root / "dir"));

    CHECK_EQ(measured("rename, its own name", system.rename_file(text(file), "file")), files::none);
    CHECK_EQ(bytes_of(file), std::string("file"));
    {
        const std::ifstream reader(file, std::ios::binary);  // shares read and write, not delete
        CHECK_EQ(measured("rename, held", system.rename_file(text(file), "moved")), files::sharing_violation);
        CHECK(thrown([&] { platform.rename(text(file), "moved", once); }).code == Code::busy);
    }
    CHECK_EQ(bytes_of(file), std::string("file"));
    CHECK(!fs::exists(scratch.root / "moved"));
    CHECK(thrown([&] { platform.rename(text(file), "a/b", five); }).code == Code::path);
    CHECK(thrown([&] { platform.rename("relative", "moved", five); }).code == Code::path);
}

QIVEN_CASE(win_names, faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    qiven3::memory::Countdown countdown;
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    Vector<String> names{Allocator<String>(arena)};
    const fs::path listed = scratch.root / "listed";
    fs::create_directory(listed);
    write_bytes(listed / "a", "x");
    const fs::path file = scratch.root / "file";
    const auto clean = [&](std::size_t) {
        return (names.empty() || joined(names) == "a") && unheld(listed / "a") && (!fs::exists(file) || unheld(file));
    };
    platform.attach(&countdown);
    const auto runs = [&](qiven3::log::Name site, auto op, std::size_t completed) {
        const qiven3::test::FaultRuns r =
            qiven3::test::run_faults<PlatformError>(qiven_ctx.log, site, countdown, op, clean);
        std::cout << "win_names.faults: completed at " << r.completed_at << ", typed " << r.typed << "\n";
        CHECK_EQ(r.completed_at, completed);
        CHECK_EQ(r.typed, completed - 1);
        CHECK_EQ(r.untyped, 0U);
        CHECK_EQ(r.unchecked, 0U);
    };
    // FindFirstFileExW, then each of ., .. and a converted and the next found.
    runs("win_names.list", [&] { platform.list(text(listed), names); }, 8);
    // DeleteFileW.
    runs("win_names.remove", [&] {
        write_bytes(file, "x");
        platform.remove(text(file));
    }, 2);
    // MoveFileExW.
    runs("win_names.rename", [&] {
        std::error_code error;
        fs::remove(scratch.root / "moved", error);
        write_bytes(file, "x");
        platform.rename(text(file), "moved", five);
    }, 2);

    // Each fault's detail names its call and error 31.
    countdown.arm(1);
    const Thrown named = thrown([&] { platform.remove(text(file)); });
    countdown.disarm();
    CHECK(named.code == Code::io);
    CHECK_EQ(named.detail, std::string("delete: error 31 (ERROR_GEN_FAILURE)"));
    CHECK(clean(0));
    platform.attach(nullptr);
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
