// files' cases on the fake: its pure checks evaluated by the compiler;
// replace, stage and lock over the fake's system calls on real files in a
// scratch directory of each case's own, the busy counts driving the
// retries, the fake's clock counting the waits (P9, P19, P20's lock and
// P25's replace and open, as files defines them); each system call and
// each platform call failed in turn, a typed error with nothing left.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "platform/files.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"
#include "support/faults/faults.hpp"

namespace {

namespace fs = std::filesystem;
namespace files = qiven3::platform::files;
using qiven3::platform::Code;
using qiven3::platform::PlatformError;
using qiven3::platform::Seconds;
using qiven3::platform::fake::Busy;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;
using qiven3::platform::fake::never;
using namespace std::chrono_literals;

// The pure checks, evaluated by the compiler.
constexpr std::array<std::byte, 8> eight{std::byte{0x01}, std::byte{0x23}, std::byte{0x45}, std::byte{0x67},
                                         std::byte{0x89}, std::byte{0xab}, std::byte{0xcd}, std::byte{0xef}};
static_assert(std::string_view(files::digits(eight).data(), 16) == "0123456789abcdef");
static_assert(files::text_fault("C:/a/\xc3\xa4\xe4\xb8\xad\xf0\x9f\x98\x80").empty());
static_assert(files::text_fault("a\xff") == "not UTF-8");
static_assert(files::text_fault("a\xed\xa0\x80") == "not UTF-8");  // a lone surrogate's three bytes
static_assert(files::text_fault("\xc0\xaf") == "not UTF-8");       // overlong
static_assert(files::text_fault("\xe4\xb8") == "not UTF-8");       // cut short
static_assert(files::text_fault("a\x01") == "holds a control character");
static_assert(files::text_fault("a\xc2\x85") == "holds a control character");  // U+0085
static_assert(files::relative_fault("bagit.txt").empty() && files::relative_fault("data/headers.jsonl").empty());
static_assert(files::relative_fault("../x") == "an empty, . or .. component");
static_assert(files::relative_fault("a/./b") == "an empty, . or .. component");
static_assert(files::relative_fault("/a") == "an empty, . or .. component");
static_assert(files::relative_fault("a//b") == "an empty, . or .. component");
static_assert(files::relative_fault("a/") == "an empty, . or .. component");
static_assert(files::relative_fault("") == "an empty, . or .. component");
static_assert(files::relative_fault("a\\..\\b") == "a component holding \\ or :");
static_assert(files::relative_fault("C:x") == "a component holding \\ or :");
static_assert(files::relative_fault("a\xff") == "not UTF-8");
static_assert(files::name_fault("bag").empty() && files::name_fault("a/b") == "more than one component");
static_assert(files::name_fault("..") == "an empty, . or .. component");
static_assert(files::last_component("C:\\a\\b.json") == "b.json" && files::last_component("/a/b") == "b");
static_assert(files::last_component("/a/").empty() && files::last_component("b") == "b");
static_assert(files::error_name(5) == "ERROR_ACCESS_DENIED" && files::error_name(32) == "ERROR_SHARING_VIOLATION");
static_assert(files::error_name(33) == "ERROR_LOCK_VIOLATION" && files::error_name(183) == "ERROR_ALREADY_EXISTS");
static_assert(files::error_name(7).empty());
static_assert(files::retried(5) && files::retried(32) && !files::retried(33) && !files::retried(3));

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

// The names in a directory, sorted, '|'-joined.
std::string listing(const fs::path& directory) {
    std::string out;
    std::error_code error;
    std::vector<std::string> names;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(directory, error)) {
        names.push_back(fs::relative(entry.path(), directory).generic_string());
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        out += (out.empty() ? "" : "|") + name;
    }
    return out;
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    Scratch() {
        std::random_device device;
        root = fs::temp_directory_path() / ("qiven3-files-" + std::to_string(device()) + std::to_string(device()));
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
    fs::path root;
};

// The code and detail of the PlatformError op throws; io and "none thrown"
// when it returns.
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

QIVEN3_CASE(files, replace) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    const fs::path target = scratch.root / "record.json";
    fake.replace(text(target), "first", five);
    CHECK_EQ(bytes_of(target), std::string("first"));
    fake.replace(text(target), std::string_view("sec\0nd", 6), five);
    CHECK_EQ(bytes_of(target), std::string("sec\0nd", 6));
    CHECK_EQ(listing(scratch.root), std::string("record.json"));
    CHECK_EQ(fake.files().elapsed(), 0ms);
    CHECK_EQ(fake.files().kept(), 0U);
}

// P9 on the fake: a reader holds the target, released before the deadline,
// then held past it; a handle on the temporary file, the same.
QIVEN3_CASE(files, replace_held) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    const fs::path target = scratch.root / "map-marks.json";
    write_bytes(target, "old");
    const std::string path = text(target);

    fake.set_busy(Busy{.target = 2});
    fake.replace(path, "new", five);
    CHECK_EQ(bytes_of(target), std::string("new"));
    CHECK_EQ(fake.files().elapsed(), 100ms);

    write_bytes(target, "old");
    fake.set_busy(Busy{.target = never});
    const auto [held, held_detail] = thrown([&] { fake.replace(path, "new", five); });
    CHECK(held == Code::busy);
    CHECK_EQ(held_detail, std::string("rename: error 5 (ERROR_ACCESS_DENIED)"));
    CHECK_EQ(bytes_of(target), std::string("old"));
    CHECK_EQ(listing(scratch.root), std::string("map-marks.json"));
    CHECK_EQ(fake.files().elapsed(), 100ms + 5000ms);

    fake.set_busy(Busy{.temporary = 2});
    fake.replace(path, "new", five);
    CHECK_EQ(bytes_of(target), std::string("new"));

    write_bytes(target, "old");
    fake.set_busy(Busy{.temporary = never});
    const auto [temporary, temporary_detail] = thrown([&] { fake.replace(path, "new", five); });
    CHECK(temporary == Code::busy);
    CHECK(temporary_detail.starts_with("rename: error 32 (ERROR_SHARING_VIOLATION); left .map-marks.json."));
    CHECK(temporary_detail.ends_with(".tmp, delete: error 32 (ERROR_SHARING_VIOLATION)"));
    CHECK_EQ(bytes_of(target), std::string("old"));
    // The temporary file, left as the holder keeps it, is the one named.
    const std::string left = listing(scratch.root);
    CHECK(left.starts_with("map-marks.json|.map-marks.json.") || left.starts_with(".map-marks.json."));
    CHECK(temporary_detail.find(left.substr(left.find(".map-marks.json."), 33)) != std::string::npos);

    // A deadline of 0 and one not a number: one try each.
    fake.set_busy(Busy{.target = never});
    const std::chrono::milliseconds before = fake.files().elapsed();
    CHECK(thrown([&] { fake.replace(path, "x", Seconds{0.0}); }).first == Code::busy);
    CHECK(thrown([&] { fake.replace(path, "x", Seconds{std::nan("")}); }).first == Code::busy);
    CHECK_EQ(fake.files().elapsed(), before);
    CHECK_EQ(fake.files().busy().target, never);  // held for good: never counted down
}

// replace refused: P25's paths, a relative one, one outside the root, one
// with no name, a missing directory; nothing written.
QIVEN3_CASE(files, replace_refused) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    const std::string root = text(scratch.root);
    CHECK(thrown([&] { fake.replace(root + "/a\xff", "x", five); }) == std::pair(Code::path, std::string("not UTF-8")));
    CHECK(thrown([&] { fake.replace(root + "/a\xed\xa0\x80", "x", five); }).first == Code::path);
    CHECK(thrown([&] { fake.replace(root + "/a\x07", "x", five); }).first == Code::path);
    CHECK(thrown([&] { fake.replace("relative.json", "x", five); }).first == Code::path);
    CHECK(thrown([&] { fake.replace(text(scratch.root.parent_path() / "outside.json"), "x", five); }).first ==
          Code::path);
    CHECK(thrown([&] { fake.replace(root + "/", "x", five); }) == std::pair(Code::path, std::string("no name")));
    const auto [code, detail] = thrown([&] { fake.replace(root + "/missing/a.json", "x", five); });
    CHECK(code == Code::io);
    CHECK_EQ(detail, std::string("write: error 3 (ERROR_PATH_NOT_FOUND)"));
    CHECK_EQ(listing(scratch.root), std::string());
}

// P19 on the fake: two files staged and published; again with a file held
// inside, released before the deadline, then held past it; then onto a
// present name; open of ../x and publish of a/b.
QIVEN3_CASE(files, stage) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    const std::string root = text(scratch.root);
    const auto staged = [&](std::string_view bag) {
        auto stage = fake.stage(root);
        auto bagit = stage.open("bagit.txt");
        bagit.write("BagIt-Version: 1.0\n");
        auto headers = stage.open("data/headers.jsonl");
        headers.write(bag);
        bagit.close();  // headers stays open: publish closes it
        return stage;
    };
    auto first = staged("one");
    first.publish("bag-1", five);
    CHECK_EQ(listing(scratch.root), std::string("bag-1|bag-1/bagit.txt|bag-1/data|bag-1/data/headers.jsonl"));
    CHECK_EQ(bytes_of(scratch.root / "bag-1/data/headers.jsonl"), std::string("one"));
    CHECK(thrown([&] { first.open("late"); }).first == Code::io);
    first.discard();  // published: none

    fake.set_busy(Busy{.publish = 2});
    staged("two").publish("bag-2", five);
    CHECK(fs::is_regular_file(scratch.root / "bag-2/data/headers.jsonl"));
    CHECK_EQ(fake.files().elapsed(), 100ms);

    fake.set_busy(Busy{.publish = never});
    auto held = staged("three");
    const auto [code, detail] = thrown([&] { held.publish("bag-3", five); });
    CHECK(code == Code::busy);
    CHECK_EQ(detail, std::string("rename: error 5 (ERROR_ACCESS_DENIED); left bagit.txt, "
                                 "delete: error 32 (ERROR_SHARING_VIOLATION)"));
    CHECK(!fs::exists(scratch.root / "bag-3"));
    CHECK(thrown([&] { held.discard(); }).first == Code::busy);
    fake.set_busy(Busy{});
    held.discard();
    CHECK_EQ(listing(scratch.root).find(".tmp"), std::string::npos);
    CHECK(thrown([&] { held.publish("bag-3", five); }).first == Code::io);

    auto four = staged("four");
    const auto [present, present_detail] = thrown([&] { four.publish("bag-1", five); });
    CHECK(present == Code::exists);
    CHECK_EQ(present_detail, std::string("rename: error 183 (ERROR_ALREADY_EXISTS)"));
    CHECK_EQ(listing(scratch.root).find(".tmp"), std::string::npos);
    CHECK_EQ(bytes_of(scratch.root / "bag-1/data/headers.jsonl"), std::string("one"));

    auto refused = fake.stage(root);
    CHECK(thrown([&] { refused.open("../x"); }) == std::pair(Code::path, std::string("an empty, . or .. component")));
    CHECK(thrown([&] { refused.open("a\xff"); }) == std::pair(Code::path, std::string("not UTF-8")));
    auto sink = refused.open("x");
    CHECK(thrown([&] { refused.open("x"); }).first == Code::exists);
    CHECK(thrown([&] { refused.publish("a/b", five); }) == std::pair(Code::path, std::string("more than one component")));
    refused.discard();
    CHECK(thrown([&] { sink.write("late"); }).first == Code::io);
    { const auto dropped = staged("five"); }  // its destructor discards it
    CHECK_EQ(listing(scratch.root), std::string("bag-1|bag-1/bagit.txt|bag-1/data|bag-1/data/headers.jsonl|"
                                                "bag-2|bag-2/bagit.txt|bag-2/data|bag-2/data/headers.jsonl"));
    CHECK(thrown([&] { fake.stage(root + "/missing"); }).first == Code::io);
    CHECK_EQ(fake.files().kept(), 0U);
}

// P20's lock on the fake: held, then released before the deadline; held
// past it; another holder's count.
QIVEN3_CASE(files, lock) {
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    const std::string path = text(scratch.root / "location.lock");
    auto held = fake.lock(path, five);
    const auto [code, detail] = thrown([&] { fake.lock(path, five); });
    CHECK(code == Code::busy);
    CHECK_EQ(detail, std::string("lock: error 33 (ERROR_LOCK_VIOLATION)"));
    CHECK_EQ(fake.files().elapsed(), 5000ms);
    auto moved = std::move(held);
    moved.release();
    moved.release();
    { auto again = fake.lock(path, five); }
    fake.set_busy(Busy{.lock = 2});
    auto late = fake.lock(path, five);
    CHECK_EQ(fake.files().elapsed(), 5100ms);
    late.release();
    CHECK(!fs::exists(scratch.root / "location.lock"));  // the fake's locks are in memory
    CHECK(thrown([&] { fake.lock("relative.lock", five); }).first == Code::path);
    CHECK_EQ(fake.files().kept(), 0U);
}

// Each system call and each platform call failed in turn: a typed error,
// the target's bytes whole, no temporary file and nothing kept after.
QIVEN3_CASE(files, faults) {
#if defined(QIVEN3_FAULTS)
    const Scratch scratch;
    FakePlatform fake(scratch.config());
    qiven3::memory::Countdown countdown;
    const std::string root = text(scratch.root);
    const fs::path target = scratch.root / "record.json";
    write_bytes(target, "old");
    const auto clean = [&](std::size_t) {
        const std::string bytes = bytes_of(target);
        return (bytes == "old" || bytes == "new") && fake.files().kept() == 0 &&
               listing(scratch.root).find(".tmp") == std::string::npos;
    };
    fake.attach(&countdown);
    const auto runs = [&](qiven3::log::Name site, auto op, std::size_t completed) {
        const qiven3::test::FaultRuns r =
            qiven3::test::run_faults<PlatformError>(qiven3_ctx.log, site, countdown, op, clean);
        std::cout << "files.faults: completed at " << r.completed_at << ", typed " << r.typed << "\n";
        CHECK_EQ(r.completed_at, completed);
        CHECK_EQ(r.typed, completed - 1);
        CHECK_EQ(r.untyped, 0U);
        CHECK_EQ(r.unchecked, 0U);
    };
    // record, random_bytes, write, rename.
    runs("files.replace", [&] { fake.replace(text(target), "new", five); }, 5);
    // record, random_bytes, make, open, write, close, close_all, publish.
    runs("files.stage", [&] {
        std::error_code error;
        fs::remove_all(scratch.root / "bag", error);
        auto stage = fake.stage(root);
        auto sink = stage.open("data/headers.jsonl");
        sink.write("{}\n");
        sink.close();
        stage.publish("bag", five);
    }, 9);
    // record, open, try_lock.
    runs("files.lock", [&] { fake.lock(root + "/location.lock", five).release(); }, 4);
    // A lock's other error: io at once, not retried as a holder's.
    countdown.arm(3);
    CHECK(thrown([&] { fake.lock(root + "/location.lock", five); }).first == Code::io);
    countdown.disarm();
    // A discard failed: io, then the destructor's discard removes it.
    {
        auto stage = fake.stage(root);
        stage.open("inside").close();
        countdown.arm(1);
        CHECK(thrown([&] { stage.discard(); }).first == Code::io);
        countdown.disarm();
    }
    CHECK(clean(0));
    fake.attach(nullptr);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN3_FAULTS");
#endif
}
