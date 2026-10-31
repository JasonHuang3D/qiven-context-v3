// win/files' stage on the Windows adapter, on the real file system in a
// scratch directory of each case's own under the job's temporary
// directory: stage, open, write, close, publish and discard as the contract
// states them, each error by its Win32 name; a file inside held by a
// reader, which shares read and write but not delete; the bound of 16 open
// files driven to its edge across staging directories, by each end that
// frees a place; each Win32 call failed in turn, a typed error with no
// staging directory, handle or place left (the heap's count is
// win_files_stage_memory_test.cpp's).

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
#include <thread>
#include <utility>
#include <vector>

#include "platform/win/windows.hpp"
#include "qiven/runner/runner.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace fs = std::filesystem;
using qiven3::platform::Code;
using qiven3::platform::PlatformError;
using qiven3::platform::Seconds;
using qiven3::platform::win::WindowsPlatform;
using qiven3::platform::win::WindowsSink;
using qiven3::platform::win::WindowsStaged;
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

// Whether no handle is open under path: it renames away and back, which a
// file held inside refuses.
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

// The staging directories in directory, ".<16 hex digits>.tmp".
std::vector<std::string> staging_left(const fs::path& directory) {
    std::vector<std::string> out;
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        const std::string name = text(entry.path().filename());
        if (name.size() == 21 && name.starts_with(".") && name.ends_with(".tmp") &&
            name.find_first_not_of("0123456789abcdef", 1) == 17) {
            out.push_back(name);
        }
    }
    return out;
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        const auto device = std::make_unique<std::random_device>();
        std::random_device& draw = *device;
        root = fs::temp_directory_path() / ("qiven3-win-stage-" + std::to_string(draw()) + std::to_string(draw()));
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

// Files opened in staged until the bound refuses one, at most 32: how many
// opened; each Sink kept in sinks.
std::size_t fill(WindowsStaged& staged, std::vector<WindowsSink>& sinks, std::string_view prefix) {
    for (std::size_t n = 0; n < 32; ++n) {
        const std::string path = std::string(prefix) + std::to_string(n);
        try {
            sinks.push_back(staged.open(path));
        } catch (const PlatformError&) {
            return n;
        }
    }
    return 32;
}

constexpr Seconds five{5.0};
constexpr Seconds short_wait{0.2};

// The steady clock in whole milliseconds, as the adapter's elapsed() reads
// it: two readings hold between them every span it reads in their while.
std::chrono::milliseconds steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
}

#if defined(QIVEN_FAULTS)
using qiven3::platform::win::ClockTrace;

// A retry loop's clock by its trace, read between before and after, as
// win_files_test.cpp's check_clock: its reads in order within them, the
// first at its start, then one after each refused try; a pause of the
// text's 50 ms after each such read but a spent one. Spent: it gave up at
// its first read past the deadline, never before; else every read short
// of it.
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
constexpr std::string_view bound_detail = "16 files already open across the staged directories";
constexpr std::string_view closed_handle = "write: error 6";  // 6 is off files' table of names
constexpr std::string_view ended = "the staging directory is published or discarded";

}  // namespace

// P19's first row on the adapter: files written through open, their
// parents made, then published under name, no staging directory left; a
// file publish closed answers a write io and a close none; the ended
// Staged io; each refused path, nothing made; open's errors.
QIVEN_CASE(win_files_stage, publish) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const fs::path bags = scratch.root / "bags";
    fs::create_directory(bags);
    {
        WindowsStaged staged = platform.stage(text(bags));
        CHECK_EQ(staging_left(bags).size(), 1U);
        WindowsSink bagit = staged.open("bagit.txt");
        WindowsSink headers = staged.open("data/deep/headers.jsonl");
        bagit.write("BagIt-Version: 1.0\n");
        headers.write("{\"id\":\"a\"}\n");
        headers.write(std::string_view("\0x", 2));
        bagit.write("");
        bagit.close();
        bagit.close();
        CHECK_EQ(thrown([&] { bagit.write("x"); }).detail, std::string("the file is closed"));
        staged.publish("one", five);
        CHECK_EQ(bytes_of(bags / "one" / "bagit.txt"), std::string("BagIt-Version: 1.0\n"));
        CHECK_EQ(bytes_of(bags / "one" / "data" / "deep" / "headers.jsonl"), std::string("{\"id\":\"a\"}\n\0x", 13));
        CHECK(staging_left(bags).empty());
        CHECK(unheld(bags / "one"));
        const Thrown late = thrown([&] { headers.write("late"); });
        CHECK(late.code == Code::io);
        CHECK_EQ(late.operation, std::string("write"));
        CHECK_EQ(late.detail, std::string(closed_handle));
        headers.close();
        CHECK_EQ(thrown([&] { static_cast<void>(staged.open("x")); }).detail, std::string(ended));
        CHECK_EQ(thrown([&] { staged.publish("two", five); }).detail, std::string(ended));
        staged.discard();
    }
    {
        WindowsStaged staged = platform.stage(text(bags) + "\\");
        const std::vector<std::string> made = staging_left(bags);
        CHECK_EQ(made.size(), 1U);
        for (const std::string_view bad : {"../x", "C:/x", "/x", "a//b", "a\\b", ".", "a/"}) {
            const Thrown path = thrown([&] { static_cast<void>(staged.open(bad)); });
            CHECK(path.code == Code::path);
            CHECK_EQ(path.operation, std::string("open"));
        }
        CHECK_EQ(thrown([&] { static_cast<void>(staged.open("../x")); }).detail,
                 std::string("an empty, . or .. component"));
        CHECK_EQ(thrown([&] { static_cast<void>(staged.open("C:/x")); }).detail,
                 std::string("a component holding \\ or :"));
        const Thrown two = thrown([&] { staged.publish("a/b", five); });
        CHECK(two.code == Code::path);
        CHECK_EQ(two.detail, std::string("more than one component"));
        CHECK(fs::is_empty(bags / made.front()));
        staged.open("present.txt").close();
        staged.open("folder/inner.txt").close();
        const Thrown present = thrown([&] { static_cast<void>(staged.open("present.txt")); });
        CHECK(present.code == Code::exists);
        CHECK_EQ(present.detail, std::string("open: error 80 (ERROR_FILE_EXISTS)"));
        const Thrown under_file = thrown([&] { static_cast<void>(staged.open("present.txt/inner.txt")); });
        CHECK(under_file.code == Code::io);
        CHECK_EQ(under_file.detail, std::string("open: error 3 (ERROR_PATH_NOT_FOUND)"));
        const Thrown folder = thrown([&] { static_cast<void>(staged.open("folder")); });
        CHECK(folder.code == Code::io);
        CHECK_EQ(folder.detail, std::string("open: error 5 (ERROR_ACCESS_DENIED)"));
        staged.discard();
        staged.discard();
    }
    CHECK(staging_left(bags).empty());
    const Thrown relative = thrown([&] { static_cast<void>(platform.stage("bags")); });
    CHECK(relative.code == Code::path);
    CHECK_EQ(relative.detail, std::string("not absolute"));
    const Thrown missing = thrown([&] { static_cast<void>(platform.stage(text(bags / "none"))); });
    CHECK(missing.code == Code::io);
    CHECK_EQ(missing.operation, std::string("stage"));
    CHECK_EQ(missing.detail, std::string("make: error 3 (ERROR_PATH_NOT_FOUND)"));
    CHECK_EQ(std::distance(fs::directory_iterator(bags), fs::directory_iterator()), 1);
    CHECK(!fs::exists(scratch.root / "x"));
}

// Onto a present name, empty or not: exists, its bytes kept, the staging
// directory discarded first.
QIVEN_CASE(win_files_stage, publish_exists) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    fs::create_directories(scratch.root / "full");
    fs::create_directories(scratch.root / "empty");
    write_bytes(scratch.root / "full" / "bagit.txt", "present");
    for (const std::string_view name : {"full", "empty"}) {
        WindowsStaged staged = platform.stage(text(scratch.root));
        WindowsSink open = staged.open("bagit.txt");
        open.write("new");
        const Thrown onto = thrown([&] { staged.publish(name, five); });
        CHECK(onto.code == Code::exists);
        CHECK_EQ(onto.operation, std::string("publish"));
        CHECK_EQ(onto.detail, std::string("rename: error 183 (ERROR_ALREADY_EXISTS)"));
        CHECK(staging_left(scratch.root).empty());
        CHECK_EQ(thrown([&] { open.write("x"); }).detail, std::string(closed_handle));
        staged.discard();
    }
    CHECK_EQ(bytes_of(scratch.root / "full" / "bagit.txt"), std::string("present"));
    CHECK(fs::is_empty(scratch.root / "empty"));
}

// P19's held rows: a file inside held, released before the deadline,
// published once released; then held past it: busy, never before the
// deadline by the clock the adapter reads, naming the staging directory
// left and the file that stopped its discard; released, a discard removes
// it. Under QIVEN_FAULTS the trace shows each publish's retries as
// check_clock states them.
QIVEN_CASE(win_files_stage, publish_held) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
#if defined(QIVEN_FAULTS)
    ClockTrace trace;
#endif
    const auto bag = [&](WindowsStaged& staged) {
        WindowsSink bagit = staged.open("bagit.txt");
        bagit.write("BagIt-Version: 1.0\n");
        bagit.close();
        WindowsSink headers = staged.open("data/headers.jsonl");
        headers.write("{}\n");
        return headers;  // left open for publish to close
    };
    {
        WindowsStaged staged = platform.stage(text(scratch.root));
        const WindowsSink open = bag(staged);
        const fs::path inside = scratch.root / staging_left(scratch.root).front();
        auto reader = std::make_unique<std::ifstream>(inside / "bagit.txt", std::ios::binary);
        CHECK(reader->is_open());
#if defined(QIVEN_FAULTS)
        platform.trace(&trace);
#endif
        const auto before = steady_ms();
        std::jthread release([&] {
            std::this_thread::sleep_for(150ms);
            reader->close();
        });
        staged.publish("one", five);
        const auto after = steady_ms();
        release.join();
        CHECK(after - before >= 100ms);  // not before the release
#if defined(QIVEN_FAULTS)
        platform.trace(nullptr);
        check_clock(qiven_ctx, trace, before, after, five, false);
        trace = ClockTrace{};
#endif
        CHECK_EQ(bytes_of(scratch.root / "one" / "data" / "headers.jsonl"), std::string("{}\n"));
        CHECK(staging_left(scratch.root).empty());
    }
    WindowsStaged staged = platform.stage(text(scratch.root));
    const WindowsSink open = bag(staged);
    const std::string name = staging_left(scratch.root).front();
    {
        const std::ifstream reader(scratch.root / name / "bagit.txt", std::ios::binary);
        CHECK(reader.is_open());
#if defined(QIVEN_FAULTS)
        platform.trace(&trace);
#endif
        const auto before = steady_ms();
        const Thrown held = thrown([&] { staged.publish("two", short_wait); });
        const auto after = steady_ms();
        CHECK(held.code == Code::busy);
        CHECK_EQ(held.operation, std::string("publish"));
        CHECK_EQ(held.detail, "rename: error 5 (ERROR_ACCESS_DENIED); left " + name +
                                  "/bagit.txt, delete: error 32 (ERROR_SHARING_VIOLATION)");
        CHECK(after - before >= short_wait);
#if defined(QIVEN_FAULTS)
        platform.trace(nullptr);
        check_clock(qiven_ctx, trace, before, after, short_wait, true);
#endif
        CHECK(!fs::exists(scratch.root / "two"));
        CHECK(fs::is_regular_file(scratch.root / name / "bagit.txt"));
    }
    staged.discard();
    CHECK(staging_left(scratch.root).empty());
    CHECK(!fs::exists(scratch.root / "two"));
}

// discard: the tree gone, its open files closed; a file inside held: busy,
// left naming it by its path inside, which a later discard removes; the
// Staged's end discards.
QIVEN_CASE(win_files_stage, discard) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    {
        WindowsStaged staged = platform.stage(text(scratch.root));
        WindowsSink open = staged.open("a/b/c.txt");
        open.write("c");
        staged.open("a/d.txt").close();
        staged.open("e.txt").close();
        WindowsSink deep = staged.open("f/g/h/i.txt");
        staged.discard();
        CHECK(staging_left(scratch.root).empty());
        CHECK_EQ(thrown([&] { open.write("x"); }).detail, std::string(closed_handle));
        open.close();
        staged.discard();
        CHECK_EQ(thrown([&] { static_cast<void>(staged.open("x")); }).detail, std::string(ended));
    }
    {
        WindowsStaged staged = platform.stage(text(scratch.root));
        const std::string name = staging_left(scratch.root).front();
        staged.open("sub/held.txt").close();
        const WindowsSink other = staged.open("sub/z.txt");
        {
            const std::ifstream reader(scratch.root / name / "sub" / "held.txt", std::ios::binary);
            const Thrown held = thrown([&] { staged.discard(); });
            CHECK(held.code == Code::busy);
            CHECK_EQ(held.operation, std::string("discard"));
            CHECK_EQ(held.detail, "left " + name + "/sub/held.txt, delete: error 32 (ERROR_SHARING_VIOLATION)");
            CHECK(fs::is_regular_file(scratch.root / name / "sub" / "held.txt"));
        }
        staged.discard();
        CHECK(staging_left(scratch.root).empty());
    }
    {
        WindowsStaged staged = platform.stage(text(scratch.root));
        WindowsSink sink = staged.open("x/y.txt");
        sink.write("y");
    }
    CHECK(staging_left(scratch.root).empty());
    CHECK(fs::is_empty(scratch.root));
}

// The file discard names is the code's, not the listing's: files made in
// an order, and named so, that the order of their making and a listing by
// names upper-cased would each give another first. All held, the first by
// its path inside, component by component in UTF-8's byte order, is named:
// B/y.txt, B ending before B-x.txt and B before a.txt. a.txt alone held,
// the walk deletes those before it and names it.
QIVEN_CASE(win_files_stage, discard_order) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    WindowsStaged staged = platform.stage(text(scratch.root));
    const std::string name = staging_left(scratch.root).front();
    const fs::path inside = scratch.root / name;
    constexpr std::array<std::string_view, 4> made{"a.txt", "B-x.txt", "B/y.txt", "c/d.txt"};
    for (const std::string_view path : made) {
        staged.open(path).close();
    }
    const std::string busy = ", delete: error 32 (ERROR_SHARING_VIOLATION)";
    {
        std::vector<std::ifstream> readers;
        for (const std::string_view path : made) {
            readers.emplace_back(inside / fs::path(path), std::ios::binary);
            CHECK(readers.back().is_open());
        }
        const Thrown held = thrown([&] { staged.discard(); });
        CHECK(held.code == Code::busy);
        CHECK_EQ(held.detail, "left " + name + "/B/y.txt" + busy);
    }
    {
        const std::ifstream reader(inside / "a.txt", std::ios::binary);
        CHECK_EQ(thrown([&] { staged.discard(); }).detail, "left " + name + "/a.txt" + busy);
        CHECK(!fs::exists(inside / "B"));
        CHECK(!fs::exists(inside / "B-x.txt"));
        CHECK(fs::is_regular_file(inside / "c" / "d.txt"));
    }
    staged.discard();
    CHECK(staging_left(scratch.root).empty());
}

// The bound to its edge: 16 open across two staging directories, the 17th
// io naming the bound with nothing made; then each end that frees a place,
// the places it frees opened again, one more refused: a Sink's close, its
// end, publish, discard and the Staged's end.
QIVEN_CASE(win_files_stage, bound) {
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    const std::string root = text(scratch.root);
    std::vector<WindowsSink> a;
    std::vector<WindowsSink> b;
    std::optional<WindowsStaged> first(platform.stage(root));
    std::optional<WindowsStaged> second(platform.stage(root));
    for (std::size_t n = 0; n < 8; ++n) {
        a.push_back(first->open("a" + std::to_string(n)));
        b.push_back(second->open("b/" + std::to_string(n)));
    }
    const Thrown over = thrown([&] { static_cast<void>(second->open("over")); });
    CHECK(over.code == Code::io);
    CHECK_EQ(over.operation, std::string("open"));
    CHECK_EQ(over.detail, std::string(bound_detail));
    for (const std::string& name : staging_left(scratch.root)) {
        CHECK(!fs::exists(scratch.root / name / "over"));
    }
    a[0].close();
    CHECK_EQ(fill(*second, b, "close-"), 1U);
    a[1] = WindowsSink();
    CHECK_EQ(fill(*second, b, "end-"), 1U);
    first->publish("published", five);
    CHECK_EQ(std::distance(fs::directory_iterator(scratch.root / "published"), fs::directory_iterator()), 8);
    CHECK_EQ(fill(*second, b, "publish-"), 6U);
    CHECK_EQ(thrown([&] { a[2].write("x"); }).detail, std::string(closed_handle));
    std::optional<WindowsStaged> third(platform.stage(root));
    std::vector<WindowsSink> c;
    CHECK_EQ(fill(*third, c, "none-"), 0U);
    second->discard();
    CHECK_EQ(thrown([&] { b[0].write("x"); }).detail, std::string(closed_handle));
    CHECK_EQ(fill(*third, c, "discard-"), 16U);
    third.reset();
    std::optional<WindowsStaged> fourth(platform.stage(root));
    std::vector<WindowsSink> d;
    CHECK_EQ(fill(*fourth, d, "staged-end-"), 16U);
    CHECK_EQ(thrown([&] { static_cast<void>(fourth->open("over")); }).detail, std::string(bound_detail));
    fourth.reset();
    first.reset();
    second.reset();
    a.clear();
    b.clear();
    c.clear();
    d.clear();
    CHECK_EQ(staging_left(scratch.root).size(), 0U);
}

// Each Win32 call of stage, open, write, close, publish and discard failed
// in turn: a typed error each time, no staging directory left and no
// handle open under the runs' directory, the run completing past the last;
// then every place free, 16 open and the 17th refused. The pages' fault is
// memory, as their refusal is; the random draw's and the directory's io.
QIVEN_CASE(win_files_stage, faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch;
    WindowsPlatform platform(std::nullopt);
    qiven3::memory::Countdown countdown;
    const fs::path runs = scratch.root / "runs";
    fs::create_directory(runs);
    const std::string dir = text(runs);
    const auto clean = [&](std::size_t) {
        std::error_code error;
        fs::remove_all(runs / "out", error);
        return !error && staging_left(runs).empty() && unheld(runs);
    };
    platform.attach(&countdown);
    const auto check = [&](qiven3::log::Name site, auto op, std::size_t completed) {
        const qiven3::test::FaultRuns r =
            qiven3::test::run_faults<PlatformError>(qiven_ctx.log, site, countdown, op, clean);
        std::cout << "win_files_stage.faults: completed at " << r.completed_at << ", typed "
                  << r.typed << "\n";
        CHECK_EQ(r.completed_at, completed);
        CHECK_EQ(r.typed, completed - 1);
        CHECK_EQ(r.untyped, 0U);
        CHECK_EQ(r.unchecked, 0U);
    };
    // stage's 3 (the draw, the pages, CreateDirectoryW); an empty
    // directory's discard 4 (FindFirstFileW, FindNextFileW for . and ..,
    // RemoveDirectoryW). discard searches a directory whole once a step: k
    // entries, . and .. among them, k + 1 calls; then 1 for each file
    // deleted and each directory removed.
    check("win_files_stage.stage", [&] { platform.stage(dir).discard(); }, 8);
    // open's 1 a parent and 1 CreateFileW, 3 here; the discard of three
    // levels, each searched with its one entry, then the file deleted, then
    // each searched empty and removed: 4 + 4 + 4 + 1 + 3 * (3 + 1).
    check("win_files_stage.open", [&] {
        WindowsStaged staged = platform.stage(dir);
        const WindowsSink sink = staged.open("a/b/c.txt");
        staged.discard();
    }, 32);
    // write's 1 a chunk, close's 2 (FlushFileBuffers, CloseHandle); the
    // discard of one file 4 + 1 + 3 + 1.
    check("win_files_stage.write", [&] {
        WindowsStaged staged = platform.stage(dir);
        WindowsSink sink = staged.open("w.txt");
        sink.write("bytes");
        sink.write("more");
        sink.close();
        staged.discard();
    }, 18);
    // publish's close of each file still open 2, its MoveFileExW 1.
    check("win_files_stage.publish", [&] {
        WindowsStaged staged = platform.stage(dir);
        WindowsSink first = staged.open("bagit.txt");
        first.write("BagIt");
        first.close();
        WindowsSink second = staged.open("data/headers.jsonl");
        second.write("{}");
        staged.publish("out", five);
    }, 14);
    // A tree's discard: two files and a directory, one.txt first, then
    // sub's: 5 + 1, 4, 4 + 1, 3 + 1, 3 + 1.
    check("win_files_stage.discard", [&] {
        WindowsStaged staged = platform.stage(dir);
        staged.open("one.txt").close();
        staged.open("sub/two.txt").close();
        staged.discard();
    }, 34);

    const auto fault_at = [&](std::size_t k) {
        countdown.arm(k);
        const Thrown out = thrown([&] { static_cast<void>(platform.stage(dir)); });
        countdown.disarm();
        return out;
    };
    const Thrown draw = fault_at(1);
    CHECK(draw.code == Code::io);
    CHECK_EQ(draw.detail, std::string("BCryptGenRandom: ERROR_GEN_FAILURE (31)"));
    const Thrown pages = fault_at(2);
    CHECK(pages.code == Code::memory);
    CHECK_EQ(pages.operation, std::string("pages"));
    CHECK_EQ(pages.detail, std::string("VirtualAlloc: ERROR_GEN_FAILURE (31)"));
    const Thrown made = fault_at(3);
    CHECK(made.code == Code::io);
    CHECK_EQ(made.operation, std::string("stage"));
    CHECK_EQ(made.detail, std::string("make: error 31 (ERROR_GEN_FAILURE)"));
    CHECK(clean(0));
    CHECK(fs::is_empty(runs));
    platform.attach(nullptr);

    WindowsStaged staged = platform.stage(dir);
    std::vector<WindowsSink> sinks;
    CHECK_EQ(fill(staged, sinks, "f"), 16U);
    staged.discard();
#else
    QIVEN_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
