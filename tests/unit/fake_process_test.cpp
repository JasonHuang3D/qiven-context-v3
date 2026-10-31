// The fake's processes held to their contract: start and run refuse a
// relative argv[0], a .cmd and a .bat with start, before any program is
// looked up; a Child's read_line keeps its line when an allocation fails,
// the next call reading it whole.

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "memory/memory.hpp"
#include "platform/platform.hpp"
#include "runner/runner.hpp"
#include "support/fake_platform/fake_platform.hpp"

namespace {

namespace fs = std::filesystem;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::MemoryError;
using qiven3::memory::String;
using qiven3::platform::Code;
using qiven3::platform::Command;
using qiven3::platform::PlatformError;
using qiven3::platform::Ran;
using qiven3::platform::Seconds;
using qiven3::platform::fake::Config;
using qiven3::platform::fake::FakePlatform;
using qiven3::platform::fake::Program;

constexpr Seconds five{5.0};

// name under the temporary directory: an absolute path by the fake's rule.
std::string absolute(std::string_view name) {
    std::string out;
    for (const char8_t c : (fs::temp_directory_path() / name).u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// A program printing lines.
Program printing(std::vector<std::string> lines) {
    Program program;
    program.lines = std::move(lines);
    return program;
}

// A command of argv, with caps of 1 KiB.
Command of(std::span<const std::string_view> argv) {
    Command command;
    command.argv = argv;
    command.caps.out = 1024;
    command.caps.err = 1024;
    return command;
}

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

}  // namespace

// Each refused argv[0] is one of the fake's programs, so only the refusal
// keeps it from running; one more is none of them, refused alike.
QIVEN3_CASE(fake_process, argv_refused) {
    FakePlatform fake{Config{}};
    const std::string talker = absolute("talker.exe");
    const std::vector<std::pair<std::string, std::string>> refused = {
        {"talker.exe", "argv[0] is not absolute"},
        {"bin/talker.exe", "argv[0] is not absolute"},
        {"missing.exe", "argv[0] is not absolute"},
        {"a\xff.exe", "argv[0] is not UTF-8"},
        {absolute("script.cmd"), "argv[0] is a .cmd or a .bat: no shell runs it"},
        {absolute("script.bat"), "argv[0] is a .cmd or a .bat: no shell runs it"},
        {absolute("SCRIPT.BAT"), "argv[0] is a .cmd or a .bat: no shell runs it"},
        {absolute("script.Cmd"), "argv[0] is a .cmd or a .bat: no shell runs it"},
    };
    std::map<std::vector<std::string>, Program> programs;
    programs[{talker}] = printing({"one", "two"});
    for (const auto& entry : refused) {
        if (entry.first != "missing.exe") {
            programs[{entry.first}] = printing({"ran"});
        }
    }
    fake.set_programs(std::move(programs));
    std::vector<std::byte> pages(64 * 1024);  // on the heap: past the frame bound
    Arena arena(pages);
    for (const auto& [first, detail] : refused) {
        const std::array<std::string_view, 1> argv{first};
        const Command command = of(argv);
        CHECK(thrown([&] { static_cast<void>(fake.start(command)); }) == std::pair(Code::start, detail));
        Ran ran(arena);
        CHECK(thrown([&] { fake.run(command, "", ran); }) == std::pair(Code::start, detail));
        CHECK(ran.out.empty());
    }
    // The absolute .exe runs: start's lines, then run's.
    const std::array<std::string_view, 1> argv{talker};
    const Command command = of(argv);
    auto child = fake.start(command);
    String line{Allocator<char>(arena)};
    CHECK(child.read_line(five, line));
    CHECK_EQ(std::string(line), std::string("one\n"));
    Ran ran(arena);
    fake.run(command, "", ran);
    CHECK_EQ(std::string(ran.out), std::string("one\ntwo\n"));
}

// A line past a short string's buffer, read into an arena too small for it:
// the budget; the same line is then read whole, then the next. Under
// QIVEN_FAULTS each of the line's allocations is failed in turn on a new
// Child, its next read_line giving the first line whole.
QIVEN3_CASE(fake_process, read_line_kept) {
    FakePlatform fake{Config{}};
    const std::string talker = absolute("talker.exe");
    const std::string first(200, 'x');
    std::map<std::vector<std::string>, Program> programs;
    programs[{talker}] = printing({first, "second"});
    fake.set_programs(std::move(programs));
    const std::array<std::string_view, 1> argv{talker};
    const Command command = of(argv);
    std::vector<std::byte> small(64);
    std::vector<std::byte> big(64 * 1024);  // on the heap: past the frame bound
    auto child = fake.start(command);
    {
        Arena arena(small);
        String line{Allocator<char>(arena)};
        bool budget = false;
        try {
            static_cast<void>(child.read_line(five, line));
        } catch (const MemoryError&) {
            budget = true;
        }
        CHECK(budget);
    }
    {
        Arena arena(big);
        String line{Allocator<char>(arena)};
        CHECK(child.read_line(five, line));
        CHECK_EQ(std::string(line), first + "\n");
        CHECK(child.read_line(five, line));
        CHECK_EQ(std::string(line), std::string("second\n"));
        CHECK(!child.read_line(five, line));
    }
#if defined(QIVEN_FAULTS)
    qiven3::memory::Countdown countdown;
    std::size_t k = 1;
    for (; k <= 8; ++k) {
        auto again = fake.start(command);
        Arena arena(big);
        arena.attach(&countdown);
        String line{Allocator<char>(arena)};
        countdown.arm(k);
        bool failed = false;
        try {
            static_cast<void>(again.read_line(five, line));
        } catch (const MemoryError&) {
            failed = true;
        }
        const bool fired = countdown.fired();
        countdown.disarm();
        CHECK_EQ(failed, fired);
        if (!failed) {
            CHECK_EQ(std::string(line), first + "\n");
            break;
        }
        CHECK(again.read_line(five, line));
        CHECK_EQ(std::string(line), first + "\n");
    }
    std::cout << "fake_process.read_line_kept: the line's allocations " << k - 1 << "\n";
    CHECK(k > 1 && k <= 8);
#endif
}
