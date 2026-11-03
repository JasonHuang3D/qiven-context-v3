// The fake's own members beyond files and processes: config_dir's location
// as its Config gives it, made only by create, and its errors, in a scratch
// directory of each case's own; normcase folding ASCII letters alone; wipe
// zeroing, each call recorded; random_bytes by its seed's blocks; now as its
// clock truncated to milliseconds; pages zeroed in whole pages, refused when
// set to, lent counting them; resolve and a Staged's open refusing a path
// not UTF-8; create, read and remove_directory on real files, two fakes'
// creates racing (P27's).

#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

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
        config.open_bound = 16;  // the cases' own stage bound
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

// The detail of the PlatformError op throws; "none thrown" when it returns.
template <class Op>
std::string detail_of(Op&& op) {
    try {
        std::forward<Op>(op)();
    } catch (const PlatformError& error) {
        return std::string(error.detail.view());
    }
    return "none thrown";
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

// The bytes' lowercase hex digits.
std::string hex(std::span<const std::byte> bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const std::byte b : bytes) {
        out.push_back(digits[std::to_integer<unsigned>(b) >> 4U]);
        out.push_back(digits[std::to_integer<unsigned>(b) & 15U]);
    }
    return out;
}

// The fake's blocks: seed 1's first two, seed 2's first.
constexpr std::array<std::string_view, 2> seed_1{
    "783825822a6f9e62da2190e828e4c9d2576e5977e3a0b3620b092dfb9e9996fa",
    "532deabf88729cb43995ab5a9cd49bf9b90a079904dc0645ecda9e47ce7345a9",
};
constexpr std::string_view seed_2 = "1309ac3f4e41512820fbf259ae492bb686480eb4a7f5fa4bbc38215266ad984c";

// The test's clock, in milliseconds since the epoch.
constexpr std::int64_t clock_ms = 1'792'229'400'123;

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

// random_bytes(32) twice on the fake's default seed: the two differing;
// seeded 1, then 2: its blocks, a call taking the next n bytes across them
// (16, 40, then 8).
QIVEN_CASE(fake_platform, random_bytes) {
    {
        FakePlatform fake{Config{}};
        std::array<std::byte, 32> first{};
        std::array<std::byte, 32> second{};
        fake.random_bytes(first);
        fake.random_bytes(second);
        CHECK(first != second);
    }
    Config one;
    one.seed = 1;
    {
        FakePlatform fake(one);
        for (const std::string_view block : seed_1) {
            std::array<std::byte, 32> drawn{};
            fake.random_bytes(drawn);
            CHECK_EQ(hex(drawn), std::string(block));
        }
    }
    {
        FakePlatform fake(one);
        std::array<std::byte, 64> drawn{};
        const std::span<std::byte> all(drawn);
        fake.random_bytes(all.first(16));
        fake.random_bytes(all.subspan(16, 40));
        fake.random_bytes(all.subspan(56));
        CHECK_EQ(hex(drawn), std::string(seed_1[0]) + std::string(seed_1[1]));
    }
    Config two;
    two.seed = 2;
    FakePlatform fake(two);
    std::array<std::byte, 32> drawn{};
    fake.random_bytes(drawn);
    CHECK_EQ(hex(drawn), std::string(seed_2));
}

// now() on the fake set to 1,792,229,400,123 ms in microseconds, then to
// its last microsecond: the clock truncated to milliseconds each time.
QIVEN_CASE(fake_platform, now) {
    FakePlatform fake{Config{}};
    for (const std::int64_t us : {clock_ms * 1000, clock_ms * 1000 + 999}) {
        fake.set_clock(std::chrono::sys_time<std::chrono::microseconds>(std::chrono::microseconds(us)));
        CHECK_EQ(fake.now().time_since_epoch().count(), clock_ms);
    }
}

// pages(1), pages(5000) and pages(4096): 4,096, 8,192 and 4,096 bytes,
// zeroed, each given back; the fake set to refuse its second call: memory,
// the first's pages whole and lent counting them, back to its start when
// freed, the next call served.
QIVEN_CASE(fake_platform, pages) {
    FakePlatform fake{Config{}};
    const std::size_t start = fake.lent();
    {
        const auto first = fake.pages(1);
        const auto second = fake.pages(5000);
        const auto third = fake.pages(4096);
        CHECK_EQ(first.bytes().size(), 4096U);
        CHECK_EQ(second.bytes().size(), 8192U);
        CHECK_EQ(third.bytes().size(), 4096U);
        CHECK(zeroed(first.bytes()) && zeroed(second.bytes()) && zeroed(third.bytes()));
        CHECK_EQ(fake.lent(), start + 16384U);
    }
    CHECK_EQ(fake.lent(), start);
    fake.refuse_pages(2);
    {
        const auto first = fake.pages(1);
        const auto refused = thrown([&] { static_cast<void>(fake.pages(5000)); });
        CHECK_EQ(refused.first, Code::memory);
        CHECK_EQ(refused.second, std::string("pages"));
        CHECK_EQ(first.bytes().size(), 4096U);
        CHECK(zeroed(first.bytes()));
        CHECK_EQ(fake.lent(), start + 4096U);
    }
    CHECK_EQ(fake.lent(), start);
    {
        const auto last = fake.pages(1);
        CHECK_EQ(last.bytes().size(), 4096U);
    }
    CHECK_EQ(fake.lent(), start);
}

// A path holding 0xff, then a lone surrogate's three bytes, to resolve:
// path; the surrogate's to a Staged's open (0xff's is files_test.cpp's
// stage case, and replace's both its replace_refused): path; nothing
// written in the scratch directory, the staging directory left empty.
QIVEN_CASE(fake_platform, not_utf8) {
    const Scratch scratch;
    const fs::path inside = scratch.root / "staged";
    fs::create_directory(inside);
    const std::vector<std::string> before = names_in(scratch.root);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        FakePlatform fake(scratch.config(""));
        String out{Allocator<char>(arena)};
        for (const std::string_view bad : {std::string_view("\xff"), std::string_view("\xed\xa0\x80")}) {
            const std::string path = text(scratch.root) + "/a" + std::string(bad) + ".txt";
            const auto refused = thrown([&] { fake.resolve(path, out); });
            CHECK_EQ(refused.first, Code::path);
            CHECK_EQ(refused.second, std::string("resolve"));
        }
        auto staged = fake.stage(text(inside));
        const auto opened = thrown([&] { static_cast<void>(staged.open("a\xed\xa0\x80")); });
        CHECK_EQ(opened.first, Code::path);
        CHECK_EQ(opened.second, std::string("open"));
        staged.discard();
    }
    CHECK_EQ(arena.used(), 0U);
    CHECK(names_in(scratch.root) == before);
    CHECK(fs::is_empty(inside));
}

// P27's create on the fake: an absent file made empty, an absent directory
// made; each again exists, what each holds kept; one whose parent is
// missing io, the parent not made; a relative path path.
QIVEN_CASE(fake_platform, create) {
    const Scratch scratch;
    FakePlatform fake(scratch.config(""));
    const fs::path made = scratch.root / "made";
    const fs::path folder = scratch.root / "folder";
    fake.create(text(made), false);
    CHECK(fs::is_regular_file(made));
    CHECK_EQ(fs::file_size(made), 0U);
    fake.create(text(folder), true);
    CHECK(fs::is_directory(folder));

    write_bytes(made, "kept");
    write_bytes(folder / "inside", "inside");
    CHECK(thrown([&] { fake.create(text(made), false); }) == std::pair(Code::exists, std::string("create")));
    CHECK(thrown([&] { fake.create(text(folder), true); }).first == Code::exists);
    CHECK_EQ(bytes_of(made), std::string("kept"));
    CHECK_EQ(bytes_of(folder / "inside"), std::string("inside"));

    const fs::path orphan = scratch.root / "missing" / "child";
    for (const bool directory : {false, true}) {
        CHECK(thrown([&] { fake.create(text(orphan), directory); }) == std::pair(Code::io, std::string("create")));
    }
    CHECK(!fs::exists(orphan.parent_path()));
    CHECK(thrown([&] { fake.create("relative", false); }).first == Code::path);
}

// P27's read on the fake: a file of every byte value read whole, CRLF and
// the C runtime's end of text among them; a missing file io, its detail
// naming the error; the error the fake is given for a path, its detail.
QIVEN_CASE(fake_platform, read) {
    const Scratch scratch;
    FakePlatform fake(scratch.config(""));
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    String out{Allocator<char>(arena)};
    std::string whole;
    for (int c = 0; c < 256; ++c) {
        whole.push_back(static_cast<char>(c));
    }
    whole += std::string_view("\r\n\x1a\0tail", 8);
    const fs::path written = scratch.root / "written.bin";
    write_bytes(written, whole);
    fake.read(text(written), out);
    CHECK(std::string_view(out) == whole);

    const std::string missing = detail_of([&] { fake.read(text(scratch.root / "absent"), out); });
    CHECK(missing.starts_with("not found: "));
    CHECK(out.empty());
    const std::string planted = "ERROR_LOCK_VIOLATION (33)";
    fake.set_read_error(text(written), planted);
    CHECK_EQ(detail_of([&] { fake.read(text(written), out); }), planted);
    CHECK(thrown([&] { fake.read(text(written), out); }) == std::pair(Code::io, std::string("read")));
}

// P27's remove_directory on the fake: an empty directory removed, true,
// and true again with none there; one holding a file and one holding a
// directory kept, false; a file io, kept; a relative path path; under
// QIVEN_FAULTS refused by its fault point, io, the directory kept.
QIVEN_CASE(fake_platform, remove_directory) {
    const Scratch scratch;
    FakePlatform fake(scratch.config(""));
    const fs::path empty = scratch.root / "empty";
    const fs::path full = scratch.root / "full";
    const fs::path nested = scratch.root / "nested";
    const fs::path plain = scratch.root / "plain";
    fs::create_directory(empty);
    CHECK(fake.remove_directory(text(empty)));
    CHECK(!fs::exists(empty));
    CHECK(fake.remove_directory(text(empty)));
    fs::create_directory(full);
    write_bytes(full / "inside", "inside");
    fs::create_directories(nested / "inner");
    CHECK(!fake.remove_directory(text(full)));
    CHECK(!fake.remove_directory(text(nested)));
    CHECK_EQ(bytes_of(full / "inside"), std::string("inside"));
    CHECK(fs::is_directory(nested / "inner"));
    write_bytes(plain, "plain");
    CHECK(thrown([&] { fake.remove_directory(text(plain)); }) ==
          std::pair(Code::io, std::string("remove_directory")));
    CHECK_EQ(bytes_of(plain), std::string("plain"));
    CHECK(thrown([&] { fake.remove_directory("relative"); }).first == Code::path);
#if defined(QIVEN_FAULTS)
    fs::create_directory(empty);
    qiven::memory::Countdown countdown;
    fake.attach(&countdown);
    countdown.arm(1);
    CHECK(thrown([&] { fake.remove_directory(text(empty)); }) ==
          std::pair(Code::io, std::string("remove_directory")));
    countdown.disarm();
    fake.attach(nullptr);
    CHECK(fs::is_directory(empty));
#endif
}

// P27's race on the fake, whose create is the system's exclusive one: two
// fakes on one root, each on a thread of its own, create the same absent
// files, each pair of creates released together by a barrier: of each pair
// one makes the empty file and the other finds it present, never both.
QIVEN_CASE(fake_platform, create_race) {
    const Scratch scratch;
    constexpr std::size_t races = 64;
    FakePlatform one(scratch.config(""));
    FakePlatform two(scratch.config(""));
    const std::array<FakePlatform*, 2> creators{&one, &two};
    std::vector<std::vector<std::pair<Code, std::string>>> outcomes(
        2, std::vector<std::pair<Code, std::string>>(races));
    std::barrier gate(2);
    const auto creates = [&](std::size_t creator) {
        for (std::size_t k = 0; k < races; ++k) {
            const std::string path = text(scratch.root / ("raced-" + std::to_string(k)));
            gate.arrive_and_wait();
            outcomes[creator][k] = thrown([&] { creators[creator]->create(path, false); });
        }
    };
    std::thread second(creates, std::size_t{1});
    creates(0);
    second.join();
    const std::pair<Code, std::string> made(Code::unsupported, "none thrown");
    const std::pair<Code, std::string> present(Code::exists, "create");
    std::array<std::size_t, 2> won{};
    for (std::size_t k = 0; k < races; ++k) {
        const std::string name = "raced-" + std::to_string(k);
        const auto& first = outcomes[0][k];
        const auto& other = outcomes[1][k];
        won[0] += first == made ? 1U : 0U;
        won[1] += other == made ? 1U : 0U;
        const bool decided = (first == made && other == present) || (first == present && other == made);
        CHECK_EQ(name + (decided ? ": one made, one present" : ": not so"), name + ": one made, one present");
        CHECK_EQ(fs::file_size(scratch.root / name), 0U);
    }
    std::cout << "fake_platform.create_race: of " << races << " pairs the first made " << won[0] << ", the second "
              << won[1] << "\n";
}
