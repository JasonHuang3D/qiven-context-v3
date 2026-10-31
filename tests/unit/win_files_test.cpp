// win/files' cases on the Windows adapter, on the real file system in a
// scratch directory of each case's own under the job's temporary
// directory: create, read and remove_directory as the contract states them,
// each error by the system's call and its Win32 name; replace and lock over
// files' retries, a reader holding the target and a second lock driving
// them; read's one allocation counted on its arena; each Win32 call failed
// in turn, a typed error with nothing left open or behind.

#include <array>
#include <chrono>
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
#include "platform/win/windows.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace fs = std::filesystem;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using qiven3::platform::Code;
using qiven3::platform::PlatformError;
using qiven3::platform::Seconds;
using qiven3::platform::String;
using qiven3::platform::win::WindowsPlatform;
using namespace std::chrono_literals;

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

// Whether no handle holds path without sharing delete: it renames away and
// back. A handle the adapter leaked would fail the first rename.
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

// Whether a directory holds a replace's temporary file, ".<name>.<16>.tmp".
bool temporary_left(const fs::path& directory) {
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        const std::string name = text(entry.path().filename());
        if (name.starts_with(".") && name.ends_with(".tmp")) {
            return true;
        }
    }
    return false;
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven3-win-files-" + std::to_string(draw()) + std::to_string(draw()));
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

// The code, operation and detail of the PlatformError op throws; io, "" and
// "none thrown" when it returns.
struct Thrown {
    Code code = Code::io;
    std::string operation;
    std::string detail = "none thrown";
};

template <class Op>
Thrown thrown(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return {error.code, std::string(error.operation.view()), std::string(error.detail.view())};
    }
    return {};
}

// Pages for an arena, off the stack.
struct Pages {
    std::array<std::byte, 1U << 16U> bytes{};
};

constexpr Seconds five{5.0};
constexpr Seconds short_wait{0.2};

// The steady clock in whole milliseconds, as the adapter's elapsed() reads
// it: two readings hold between them every span it reads in their while.
std::chrono::milliseconds steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

#if defined(QIVEN_FAULTS)
using qiven3::platform::win::ClockTrace;

// A retry loop's clock by its trace, read between before and after: its
// reads in order within them, the first at its start, then one after each
// refused try; a pause of the text's 50 ms after each such read but a
// spent one. Spent: it gave up at its first read past the deadline, never
// before; else every read short of it.
void check_clock(qiven::runner::Context& qiven_ctx, const ClockTrace& t, std::chrono::milliseconds before,
                 std::chrono::milliseconds after, Seconds deadline, bool spent) {
    CHECK(t.reads >= (spent ? 2U : 1U));
    CHECK(before <= t.first && t.first <= t.previous && t.previous <= t.last && t.last <= after);
    CHECK_EQ(t.pauses, spent ? t.reads - 2 : t.reads - 1);
    CHECK(t.asked == 50ms * static_cast<std::chrono::milliseconds::rep>(t.pauses));
    CHECK(t.previous - t.first < deadline);
    CHECK(spent == (t.last - t.first >= deadline));
}
#endif

}  // namespace

QIVEN_CASE(win_files, create) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const fs::path made = scratch.root / "made";
    const fs::path folder = scratch.root / "folder";
    platform.create(text(made), false);
    CHECK(fs::is_regular_file(made));
    CHECK_EQ(fs::file_size(made), 0U);
    CHECK(unheld(made));
    platform.create(text(folder), true);
    CHECK(fs::is_directory(folder));

    write_bytes(made, "kept");
    write_bytes(folder / "inside", "inside");
    const Thrown file_again = thrown([&] { platform.create(text(made), false); });
    CHECK(file_again.code == Code::exists);
    CHECK_EQ(file_again.operation, std::string("create"));
    CHECK_EQ(file_again.detail, std::string("CreateFileW: ERROR_FILE_EXISTS (80)"));
    const Thrown folder_again = thrown([&] { platform.create(text(folder), true); });
    CHECK(folder_again.code == Code::exists);
    CHECK_EQ(folder_again.detail, std::string("CreateDirectoryW: ERROR_ALREADY_EXISTS (183)"));
    // A file's create on a present directory, and a directory's on a file.
    const Thrown file_on_folder = thrown([&] { platform.create(text(folder), false); });
    CHECK(file_on_folder.code == Code::exists);
    CHECK_EQ(file_on_folder.detail, std::string("CreateFileW: ERROR_ACCESS_DENIED (5)"));
    CHECK(thrown([&] { platform.create(text(made), true); }).code == Code::exists);
    CHECK_EQ(bytes_of(made), std::string("kept"));
    CHECK_EQ(bytes_of(folder / "inside"), std::string("inside"));

    const fs::path orphan = scratch.root / "missing" / "child";
    const Thrown file_orphan = thrown([&] { platform.create(text(orphan), false); });
    CHECK(file_orphan.code == Code::io);
    CHECK_EQ(file_orphan.detail, std::string("CreateFileW: ERROR_PATH_NOT_FOUND (3)"));
    const Thrown folder_orphan = thrown([&] { platform.create(text(orphan), true); });
    CHECK(folder_orphan.code == Code::io);
    CHECK_EQ(folder_orphan.detail, std::string("CreateDirectoryW: ERROR_PATH_NOT_FOUND (3)"));
    CHECK(!fs::exists(orphan.parent_path()));

    const Thrown relative = thrown([&] { platform.create("relative", false); });
    CHECK(relative.code == Code::path);
    CHECK_EQ(relative.detail, std::string("not absolute"));
    const Thrown bad = thrown([&] { platform.create(scratch.at("a") + "\xff", false); });
    CHECK(bad.code == Code::path);
    CHECK_EQ(bad.detail, std::string("not UTF-8"));
}

QIVEN_CASE(win_files, read) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    String out{Allocator<char>(arena)};
    const fs::path written = scratch.root / "written.bin";
    const std::string whole("a\0b\r\nc", 6);
    write_bytes(written, whole);
    out = "stale";
    platform.read(text(written), out);
    CHECK_EQ(std::string(out), whole);
    CHECK(unheld(written));

    const fs::path empty = scratch.root / "empty";
    write_bytes(empty, "");
    platform.read(text(empty), out);
    CHECK(out.empty());

    const Thrown missing = thrown([&] { platform.read(scratch.at("absent"), out); });
    CHECK(missing.code == Code::io);
    CHECK_EQ(missing.operation, std::string("read"));
    CHECK_EQ(missing.detail, std::string("CreateFileW: ERROR_FILE_NOT_FOUND (2)"));
    CHECK(out.empty());
    const Thrown folder = thrown([&] { platform.read(text(scratch.root), out); });
    CHECK(folder.code == Code::io);
    CHECK_EQ(folder.detail, std::string("CreateFileW: ERROR_ACCESS_DENIED (5)"));

    // Under another handle's byte-range lock: ReadFile's 33, never 0 bytes.
    const fs::path locked = scratch.root / "locked";
    write_bytes(locked, "held bytes");
    {
        const auto held = platform.lock(text(locked), five);
        const Thrown under = thrown([&] { platform.read(text(locked), out); });
        CHECK(under.code == Code::io);
        CHECK_EQ(under.detail, std::string("ReadFile: ERROR_LOCK_VIOLATION (33)"));
        CHECK(out.empty());
    }
    platform.read(text(locked), out);
    CHECK_EQ(std::string(out), std::string("held bytes"));
    CHECK(unheld(locked));

    CHECK(thrown([&] { platform.read("relative", out); }).code == Code::path);
    CHECK(thrown([&] { platform.read(scratch.at("a") + "\xed\xa0\x80", out); }).code == Code::path);
}

// read's memory: one allocation of the caller's arena past the string's
// own 15 bytes, none within them; failed, MemoryError with the file closed.
QIVEN_CASE(win_files, read_memory) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    qiven3::memory::Countdown allocations;
    arena.attach(&allocations);
    const fs::path big = scratch.root / "big.bin";
    const fs::path small = scratch.root / "small.bin";
    write_bytes(big, std::string(1000, 'x'));
    write_bytes(small, std::string(15, 'y'));
    {
        String out{Allocator<char>(arena)};
        allocations.disarm();
        platform.read(text(small), out);
        CHECK_EQ(allocations.calls(), 0U);
        CHECK_EQ(out.size(), 15U);
        platform.read(text(big), out);
        CHECK_EQ(allocations.calls(), 1U);
        CHECK_EQ(out.size(), 1000U);
        std::cout << "win_files.read_memory: 1000 bytes, allocations " << allocations.calls() << ", peak "
                  << arena.peak() << "\n";
#if defined(_MSC_VER)
        CHECK_EQ(arena.peak(), 1008U);  // MSVC's library: a capacity of 16n - 1, its null after
#endif
    }
    {
        String out{Allocator<char>(arena)};
        allocations.arm(1);
        bool refused = false;
        try {
            platform.read(text(big), out);
        } catch (const MemoryError&) {
            refused = true;
        }
        allocations.disarm();
        CHECK(refused);
        CHECK(out.empty());
        CHECK(unheld(big));
    }
    arena.attach(nullptr);
    CHECK_EQ(arena.used(), 0U);
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}

QIVEN_CASE(win_files, remove_directory) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const fs::path empty = scratch.root / "empty";
    fs::create_directory(empty);
    CHECK(platform.remove_directory(text(empty)));
    CHECK(!fs::exists(empty));
    CHECK(platform.remove_directory(text(empty)));
    CHECK(platform.remove_directory(text(scratch.root / "missing" / "child")));  // 3: none there

    const fs::path full = scratch.root / "full";
    const fs::path nested = scratch.root / "nested";
    fs::create_directory(full);
    write_bytes(full / "inside", "inside");
    fs::create_directories(nested / "inner");
    CHECK(!platform.remove_directory(text(full)));
    CHECK(!platform.remove_directory(text(nested)));
    CHECK_EQ(bytes_of(full / "inside"), std::string("inside"));
    CHECK(fs::is_directory(nested / "inner"));

    const fs::path plain = scratch.root / "plain";
    write_bytes(plain, "plain");
    const Thrown file = thrown([&] { platform.remove_directory(text(plain)); });
    CHECK(file.code == Code::io);
    CHECK_EQ(file.operation, std::string("remove_directory"));
    CHECK_EQ(file.detail, std::string("RemoveDirectoryW: ERROR_DIRECTORY (267)"));
    CHECK_EQ(bytes_of(plain), std::string("plain"));
    const Thrown relative = thrown([&] { platform.remove_directory("relative"); });
    CHECK(relative.code == Code::path);
    CHECK_EQ(relative.detail, std::string("not absolute"));
}

QIVEN_CASE(win_files, replace) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const fs::path target = scratch.root / "record.json";
    platform.replace(text(target), "first", five);
    CHECK_EQ(bytes_of(target), std::string("first"));
    platform.replace(text(target), std::string_view("sec\0nd", 6), five);
    CHECK_EQ(bytes_of(target), std::string("sec\0nd", 6));
    platform.replace(text(target), "", five);
    CHECK_EQ(bytes_of(target), std::string());
    CHECK(!temporary_left(scratch.root));
    CHECK(unheld(target));

    const Thrown missing = thrown([&] { platform.replace(text(scratch.root / "none" / "x.json"), "x", five); });
    CHECK(missing.code == Code::io);
    CHECK_EQ(missing.operation, std::string("replace"));
    CHECK_EQ(missing.detail, std::string("write: error 3 (ERROR_PATH_NOT_FOUND)"));
    CHECK(thrown([&] { platform.replace("relative.json", "x", five); }).code == Code::path);
    const Thrown no_name = thrown([&] { platform.replace(text(scratch.root) + "\\", "x", five); });
    CHECK(no_name.code == Code::path);
    CHECK_EQ(no_name.detail, std::string("no name"));
    CHECK(!temporary_left(scratch.root));
}

// P9's reader on the adapter: a reader holds the target past the deadline,
// busy with the old bytes and no temporary file, never before the deadline
// by the clock the adapter reads; a deadline of 0 one try. Under
// QIVEN_FAULTS the trace shows the retries as check_clock states them,
// and an unarmed countdown, which fails none, counts the adapter's Win32
// calls: the held replace of deadline 0 makes those of a replace that
// renames at its first try, and one more, its temporary file's deletion;
// the deadline's replace those and one rename more for each pause.
// Released, the new bytes.
QIVEN_CASE(win_files, replace_held) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const fs::path target = scratch.root / "map-marks.json";
    write_bytes(target, "old");
#if defined(QIVEN_FAULTS)
    qiven3::memory::Countdown countdown;
    platform.attach(&countdown);
    ClockTrace held_trace;
    ClockTrace zero_trace;
#endif
    // The Win32 calls op makes; 0 without QIVEN_FAULTS.
    const auto calls = [&](auto op) {
        std::size_t made = 0;
#if defined(QIVEN_FAULTS)
        countdown.disarm();
        op();
        made = countdown.calls();
#else
        op();
#endif
        return made;
    };
    std::size_t retried = 0;
    std::size_t once = 0;
    {
        const std::ifstream reader(target, std::ios::binary);  // shares read and write, not delete
        CHECK(reader.is_open());
#if defined(QIVEN_FAULTS)
        platform.trace(&held_trace);
#endif
        const auto before = steady_ms();
        Thrown held;
        retried = calls([&] { held = thrown([&] { platform.replace(text(target), "new", short_wait); }); });
        const auto after = steady_ms();
        CHECK(held.code == Code::busy);
        CHECK_EQ(held.detail, std::string("rename: error 5 (ERROR_ACCESS_DENIED)"));
        CHECK(after - before >= short_wait);
#if defined(QIVEN_FAULTS)
        check_clock(qiven_ctx, held_trace, before, after, short_wait, true);
        platform.trace(&zero_trace);
#endif
        Thrown zero;
        once = calls([&] { zero = thrown([&] { platform.replace(text(target), "new", Seconds{0.0}); }); });
#if defined(QIVEN_FAULTS)
        platform.trace(nullptr);
        CHECK_EQ(zero_trace.reads, 2U);  // its start, and its one try's
        CHECK_EQ(zero_trace.pauses, 0U);
#endif
        CHECK(zero.code == Code::busy);
        CHECK_EQ(zero.detail, std::string("rename: error 5 (ERROR_ACCESS_DENIED)"));
        CHECK(!temporary_left(scratch.root));
    }
    CHECK_EQ(bytes_of(target), std::string("old"));
    const std::size_t first_try = calls([&] { platform.replace(text(target), "new", five); });
    CHECK_EQ(bytes_of(target), std::string("new"));
    CHECK(!temporary_left(scratch.root));
#if defined(QIVEN_FAULTS)
    CHECK(first_try > 0U);
    CHECK_EQ(once, first_try + 1);
    CHECK_EQ(retried, once + held_trace.pauses);
    CHECK(!countdown.fired());
    platform.attach(nullptr);
#else
    static_cast<void>(retried + once + first_try);
#endif
}

// P20's lock half: held past the deadline, busy, never before it by the
// clock the adapter reads, and under QIVEN_FAULTS the retries as
// check_clock states them; at 0 busy; released, acquired.
QIVEN_CASE(win_files, lock) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const fs::path file = scratch.root / "location.lock";
    auto held = platform.lock(text(file), five);
    CHECK(fs::is_regular_file(file));
#if defined(QIVEN_FAULTS)
    ClockTrace trace;
    platform.trace(&trace);
#endif
    const auto before = steady_ms();
    const Thrown second = thrown([&] { platform.lock(text(file), short_wait); });
    const auto after = steady_ms();
    CHECK(second.code == Code::busy);
    CHECK_EQ(second.operation, std::string("lock"));
    CHECK_EQ(second.detail, std::string("lock: error 33 (ERROR_LOCK_VIOLATION)"));
    CHECK(after - before >= short_wait);
#if defined(QIVEN_FAULTS)
    platform.trace(nullptr);
    check_clock(qiven_ctx, trace, before, after, short_wait, true);
#endif
    CHECK(thrown([&] { platform.lock(text(file), Seconds{0.0}); }).code == Code::busy);
    auto moved = std::move(held);
    moved.release();
    moved.release();
    held.release();
    { const auto again = platform.lock(text(file), Seconds{0.0}); }
    {
        const auto last = platform.lock(text(file), five);
        CHECK(thrown([&] { platform.lock(text(file), Seconds{0.0}); }).code == Code::busy);
    }
    CHECK(fs::is_regular_file(file));  // kept, never written
    CHECK_EQ(fs::file_size(file), 0U);
    CHECK(unheld(file));
    const Thrown missing = thrown([&] { platform.lock(text(scratch.root / "none" / "x.lock"), five); });
    CHECK(missing.code == Code::io);
    CHECK_EQ(missing.detail, std::string("open: error 3 (ERROR_PATH_NOT_FOUND)"));
    CHECK(thrown([&] { platform.lock("relative.lock", five); }).code == Code::path);
}

// Each Win32 call failed in turn: a typed error each time, nothing held
// open after it, no temporary file, the target's bytes old or new.
QIVEN_CASE(win_files, faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    qiven3::memory::Countdown countdown;
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    const fs::path target = scratch.root / "record.json";
    const fs::path made = scratch.root / "made";
    const fs::path folder = scratch.root / "folder";
    const fs::path file = scratch.root / "store.lock";
    write_bytes(target, "old");
    const auto clean = [&](std::size_t) {
        const std::string bytes = bytes_of(target);
        return (bytes == "old" || bytes == "new") && unheld(target) && !temporary_left(scratch.root) &&
               (!fs::exists(made) || unheld(made)) && (!fs::exists(file) || unheld(file));
    };
    platform.attach(&countdown);
    const auto runs = [&](qiven3::log::Name site, auto op, std::size_t completed) {
        const qiven3::test::FaultRuns r =
            qiven3::test::run_faults<PlatformError>(qiven_ctx.log, site, countdown, op, clean);
        std::cout << "win_files.faults: completed at " << r.completed_at << ", typed " << r.typed << "\n";
        CHECK_EQ(r.completed_at, completed);
        CHECK_EQ(r.typed, completed - 1);
        CHECK_EQ(r.untyped, 0U);
        CHECK_EQ(r.unchecked, 0U);
    };
    // random_bytes, CreateFileW, WriteFile, FlushFileBuffers, CloseHandle, MoveFileExW.
    runs("win_files.replace", [&] { platform.replace(text(target), "new", five); }, 7);
    // CreateFileW, LockFileEx.
    runs("win_files.lock", [&] { platform.lock(text(file), five).release(); }, 3);
    // CreateFileW; CreateDirectoryW.
    const auto create_file = [&] {
        std::error_code error;
        fs::remove(made, error);
        platform.create(text(made), false);
    };
    runs("win_files.create", create_file, 2);
    const auto create_folder = [&] {
        std::error_code error;
        fs::remove(folder, error);
        platform.create(text(folder), true);
    };
    runs("win_files.create", create_folder, 2);
    // RemoveDirectoryW.
    runs("win_files.remove_directory", [&] {
        std::error_code error;
        fs::create_directory(folder, error);
        platform.remove_directory(text(folder));
    }, 2);
    // CreateFileW, GetFileSizeEx, ReadFile.
    runs("win_files.read", [&] {
        String out{Allocator<char>(arena)};
        platform.read(text(target), out);
    }, 4);

    // Each fault's detail names its call and error 31.
    countdown.arm(1);
    const Thrown named = thrown([&] { platform.remove_directory(text(folder)); });
    countdown.disarm();
    CHECK(named.code == Code::io);
    CHECK_EQ(named.detail, std::string("RemoveDirectoryW: ERROR_GEN_FAILURE (31)"));
    // A lock's other error: io at once, not retried as a holder's.
    countdown.arm(2);
    const Thrown other = thrown([&] { platform.lock(text(file), five); });
    countdown.disarm();
    CHECK(other.code == Code::io);
    CHECK_EQ(other.detail, std::string("lock: error 31 (ERROR_GEN_FAILURE)"));
    CHECK(clean(0));
    CHECK_EQ(arena.used(), 0U);
    platform.attach(nullptr);
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
