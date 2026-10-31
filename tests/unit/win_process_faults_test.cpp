// win/process' fault runs on this machine, under QIVEN_FAULTS: each Win32
// call of program, start, the Child's members and run failed in turn, as
// error 31, until a run completes, each completed point counted. start's
// every failure is start naming its call, with no process left: each run's
// child starts in a directory of its own, which no live process holds, so
// it is removed after. The Child's write, wait and kill fail io; read_line
// and close_input make no call that fails. Then each arena allocation of
// read_line and run failed in turn: MemoryError, read_line's line kept for
// the next call. The allowlist's four names are set here, none given.

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "runner/runner.hpp"
#include "support/fixtures/win_process_case.hpp"

#if defined(QIVEN_FAULTS)

namespace {

using namespace qiven3::test::process;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::memory::Countdown;
using qiven3::memory::MemoryError;
using qiven3::platform::Ran;

// "<code> <call>" for a fault's error, as the detail names the call failed
// with error 31; "none thrown", or the detail where it names none.
std::string seen_as(const Thrown& t) {
    constexpr std::string_view tail = ": ERROR_GEN_FAILURE (31)";
    if (t.detail.ends_with(tail)) {
        return std::string(qiven3::platform::name(t.code)) + " " + t.detail.substr(0, t.detail.size() - tail.size());
    }
    return t.detail;
}

std::string joined(const std::vector<std::string>& seen) {
    std::string out;
    for (const std::string& s : seen) {
        out += (out.empty() ? "" : "; ") + s;
    }
    return out;
}

// Each call of op failed in turn, by the countdown, until a run completes.
template <class Op>
std::vector<std::string> sweep(Countdown& countdown, Op op) {
    std::vector<std::string> seen;
    for (std::size_t k = 1; k <= 100; ++k) {
        countdown.arm(k);
        const Thrown t = thrown([&] { op(k); });
        const bool fired = countdown.fired();
        countdown.disarm();
        if (!fired) {
            seen.push_back(t.detail == "none thrown" ? "completed" : "thrown when no call failed: " + t.detail);
            break;
        }
        seen.push_back(t.detail == "none thrown" ? "answered" : seen_as(t));
    }
    return seen;
}

// A directory no process holds is removed within 5 s.
bool removed(const fs::path& directory) {
    for (int i = 0; i < 100; ++i) {
        std::error_code error;
        if (fs::remove(directory, error) && !error) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

constexpr std::string_view start_calls =
    "start GetEnvironmentVariableW; start GetEnvironmentVariableW; start GetEnvironmentVariableW; start "
    "GetEnvironmentVariableW; start VirtualAlloc; start CreateEventW; start CreateEventW; start CreatePipe; start "
    "CreatePipe; start CreatePipe; start SetHandleInformation; start SetHandleInformation; start "
    "SetHandleInformation; start CreateJobObjectW; start SetInformationJobObject; start "
    "InitializeProcThreadAttributeList; start InitializeProcThreadAttributeList; start UpdateProcThreadAttribute; "
    "start UpdateProcThreadAttribute; start CreateProcessW; start ResumeThread; start a thread's start; start a "
    "thread's start; start a thread's start";

}  // namespace

#endif

QIVEN3_CASE(win_process, faults) {
#if defined(QIVEN_FAULTS)
    const Scratch scratch("win-process");
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    Countdown countdown;
    platform.attach(&countdown);
    std::size_t points = 0;
    {
        String out{Allocator<char>(arena)};
        // program: PATH, then each absolute entry until cmd.exe's; a failed
        // entry is skipped.
        const std::vector<std::string> found = sweep(countdown, [&](std::size_t) { platform.program("cmd", out); });
        std::cout << "win_process.faults: program: " << joined(found) << "\n";
        REQUIRE(found.size() >= 3);
        CHECK_EQ(found.front(), std::string("start GetEnvironmentVariableW"));
        CHECK_EQ(found.back(), std::string("completed"));
        for (std::size_t i = 1; i + 1 < found.size(); ++i) {
            // The entry holding cmd.exe skipped: no later one holds it.
            CHECK(found.at(i) == "answered" || found.at(i) == "no cmd.exe on an absolute entry of PATH");
        }
        points += found.size() - 1;

        ChildView sleep({"sleep", "0"});
        std::vector<std::string> cwds;
        const std::vector<std::string> started = sweep(countdown, [&](std::size_t k) {
            const fs::path cwd = scratch.root / std::to_string(k);
            fs::create_directory(cwd);
            cwds.push_back(text(cwd));
            sleep.command.cwd = cwds.back();
            static_cast<void>(platform.start(sleep.command));
        });
        std::cout << "win_process.faults: start: " << joined(started) << "\n";
        CHECK_EQ(joined(started), std::string(start_calls) + "; completed");
        for (const std::string& cwd : cwds) {
            CHECK(removed(fs::path(std::u8string(cwd.begin(), cwd.end()))));
        }
        points += started.size() - 1;
        sleep.command.cwd = "";

        ChildView cat({"cat", "0"});
        WindowsChild child = platform.start(cat.command);
        const std::vector<std::string> wrote = sweep(countdown, [&](std::size_t) { child.write("x\n"); });
        CHECK_EQ(joined(wrote), std::string("io WriteFile; completed"));
        child.close_input();
        CHECK(child.read_line(Seconds(30.0), out));
        CHECK_EQ(std::string(out), std::string("x\n"));
        const std::vector<std::string> waited =
            sweep(countdown, [&](std::size_t) { static_cast<void>(child.wait(Seconds(30.0))); });
        CHECK_EQ(joined(waited), std::string("io WaitForSingleObject; io GetExitCodeProcess; completed"));
        const std::vector<std::string> killed = sweep(countdown, [&](std::size_t) { child.kill(); });
        CHECK_EQ(joined(killed), std::string("io TerminateJobObject; completed"));
        points += wrote.size() + waited.size() + killed.size() - 3;

        Ran ran(arena);
        std::size_t named_run = 0;
        const std::vector<std::string> ran_calls = sweep(countdown, [&](std::size_t) {
            try {
                platform.run(cat.command, "in", ran);
            } catch (const PlatformError& error) {
                named_run += error.operation.view() == "run" ? 1U : 0U;
                throw;
            }
        });
        CHECK_EQ(joined(ran_calls), std::string(start_calls) +
                                        "; io WriteFile; io WaitForSingleObject; io GetExitCodeProcess; completed");
        CHECK_EQ(named_run, ran_calls.size() - 1);
        CHECK_EQ(std::string_view(ran.out), std::string_view("in"));
        points += ran_calls.size() - 1;
        std::cout << "win_process.faults: run: " << joined(ran_calls) << "\n";

        // The arena's allocations: read_line's long line, run's two copies.
        Countdown allocations;
        arena.attach(&allocations);
        ChildView talk({"print", hex(std::string(300, 'l') + "\n"), hex(std::string(300, 'e')), "0"});
        WindowsChild talker = platform.start(talk.command);
        allocations.arm(1);
        bool memory = false;
        try {
            static_cast<void>(talker.read_line(Seconds(30.0), out));
        } catch (const MemoryError&) {
            memory = true;
        }
        allocations.disarm();
        CHECK(memory);
        CHECK(talker.read_line(Seconds(30.0), out));
        CHECK_EQ(out.size(), 301U);
        std::size_t copies = 0;
        for (std::size_t k = 1; k <= 10; ++k) {
            Ran given(arena);
            allocations.arm(k);
            bool failed = false;
            try {
                platform.run(talk.command, "", given);
            } catch (const MemoryError&) {
                failed = true;
            }
            const bool fired = allocations.fired();
            allocations.disarm();
            if (!fired) {
                CHECK(!failed && given.out.size() == 301U && given.err.size() == 300U);
                break;
            }
            CHECK(failed);
            ++copies;
        }
        CHECK_EQ(copies, 2U);
        arena.attach(nullptr);
        points += 1 + copies;
    }
    platform.attach(nullptr);
    std::cout << "win_process.faults: " << points << " fault points, each run to completion\n";
    CHECK_EQ(arena.used(), 0U);
#else
    QIVEN3_SKIP("not applicable: built without QIVEN_FAULTS");
#endif
}
