// win/process' cases on this machine: program over v3's own PATH; start's
// refusals before any process; each argument read back by the child as
// given; the environment the allowlist and the caller's pairs alone; write
// and close_input; wait past its timeout and kill, the tree's descendants
// gone by kill(), by the supervisor's death and by the Child's end; the
// working directory, the caller's or the one given; replace whole in a
// child killed between its write and its rename. Each starts
// `qiven_platform_child` (child/child.cpp), so each is machine's.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "qiven/runner/runner.hpp"
#include "win_process_case.hpp"

namespace {

using namespace qiven::test::process;
using qiven::memory::Allocator;
using qiven::memory::Arena;
using qiven::platform::Ran;

std::string bytes_of(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& path, std::string_view bytes) {
    std::ofstream(path, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// The path a child's line names, its LF dropped.
fs::path named(std::string_view line) {
    if (line.ends_with('\n')) {
        line.remove_suffix(1);
    }
    return fs::path(std::u8string(line.begin(), line.end()));
}

// The temporary files replace left in a directory, ".<name>.<16>.tmp".
std::vector<fs::path> temporaries(const fs::path& directory) {
    std::vector<fs::path> out;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory)) {
        const std::string name = text(entry.path().filename());
        if (name.starts_with(".") && name.ends_with(".tmp")) {
            out.push_back(entry.path());
        }
    }
    return out;
}

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

}  // namespace

QIVEN_CASE(win_process, program) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String out{Allocator<char>(arena)};
        platform.program("cmd", out);
        const fs::path found = fs::path(std::u8string(out.begin(), out.end()));
        std::string file = text(found.filename());
        for (char& c : file) {
            c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }
        CHECK(found.is_absolute());
        CHECK_EQ(file, std::string("cmd.exe"));
        CHECK(fs::is_regular_file(found));
        std::cout << "win_process.program: cmd " << out << ", peak " << arena.peak() << " B\n";
        for (const std::string_view name : {"qiven-no-such-program", "a\\cmd", "c:cmd", "cmd/x", "cmd.exe"}) {
            const Thrown t = thrown([&] { platform.program(name, out); });
            CHECK_EQ(t.code, Code::start);
            CHECK_EQ(t.operation, std::string("program"));
        }
    }
    CHECK_EQ(arena.used(), 0U);
}

QIVEN_CASE(win_process, start_refusals) {
    const Scratch scratch("win-process");
    WindowsPlatform platform(std::nullopt);
    const std::string child = child_program();
    const std::string missing = text(scratch.root / "missing.exe");
    const std::string script = text(scratch.root / "x.cmd");
    const std::string batch = text(scratch.root / "x.BAT");
    const std::string bad = std::string("C:\\") + '\xff' + ".exe";
    const std::string none = text(scratch.root / "none");
    using Pair = std::pair<std::string_view, std::string_view>;
    const std::array<Pair, 1> no_name{Pair{"", "v"}};
    const std::array<Pair, 1> equals{Pair{"A=B", "v"}};
    const std::array<Pair, 1> null{Pair{"A", std::string_view("v\0w", 3)}};
    struct Refused {
        std::vector<std::string_view> argv;
        std::string_view cwd;
        std::span<const Pair> env;
    };
    const std::vector<Refused> refused = {
        {{}, "", {}},
        {{"qiven_platform_child.exe"}, "", {}},
        {{script}, "", {}},
        {{batch}, "", {}},
        {{bad}, "", {}},
        {{missing}, "", {}},
        {{child, "sleep", "0"}, "relative", {}},
        {{child, "sleep", "0"}, none, {}},
        {{child, "sleep", "0"}, "", no_name},
        {{child, "sleep", "0"}, "", equals},
        {{child, "sleep", "0"}, "", null},
    };
    for (const Refused& r : refused) {
        const Command command{r.argv, r.cwd, r.env, Seconds(5.0), Caps{1024, 1024}};
        const Thrown t = thrown([&] { static_cast<void>(platform.start(command)); });
        std::cout << "win_process.start_refusals: " << t.detail << "\n";
        CHECK_EQ(t.code, Code::start);
        CHECK_EQ(t.operation, std::string("start"));
    }
}

// The 8 arguments of the measure and the quoting's edges: each read back.
QIVEN_CASE(win_process, argv) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    const std::vector<std::string> given = {"", "two  spaces", "a \"quoted\" word", "\xc3\xab", "trailing\\",
                                            "in\\ner", "tab\there", "\\\\\"", "\\\\", "\"", "end\\\\",
                                            "a b\\\\"};
    {
        std::vector<std::string> args = {"argv"};
        args.insert(args.end(), given.begin(), given.end());
        ChildView run(args);
        WindowsChild child = platform.start(run.command);
        String line{Allocator<char>(arena)};
        const std::vector<std::string> back = lines(child, line);
        REQUIRE(back.size() == given.size());
        for (std::size_t i = 0; i < given.size(); ++i) {
            CHECK_EQ(back.at(i), hex(given.at(i)) + "\n");
        }
        CHECK_EQ(child.wait(Seconds(10.0)), 0);
        // Started twice from one Command: the same answer.
        WindowsChild again = platform.start(run.command);
        CHECK(lines(again, line) == back);
    }
    CHECK_EQ(arena.used(), 0U);
}

// The allowlist's names v3's own environment holds, and the caller's pairs,
// one replacing TEMP by its name in any case; nothing else.
QIVEN_CASE(win_process, environment) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    using Pair = std::pair<std::string_view, std::string_view>;
    const std::array<Pair, 2> pairs{Pair{"QIVEN_PAIR", "v \xc3\xab"}, Pair{"temp", "C:\\given"}};
    {
        ChildView run({"env", "QIVEN_PAIR", "TEMP", "SystemRoot", "PATH"});
        run.command.env = pairs;
        WindowsChild child = platform.start(run.command);
        String line{Allocator<char>(arena)};
        const std::vector<std::string> back = lines(child, line);
        std::set<std::string> names;
        for (std::size_t i = 0; i + 4 < back.size(); ++i) {
            names.insert(back.at(i));
        }
        std::set<std::string> expected = {"QIVEN_PAIR\n", "temp\n"};
        for (const char* name : {"SystemRoot", "SystemDrive", "TMP"}) {
            char* value = nullptr;
            std::size_t n = 0;
            if (_dupenv_s(&value, &n, name) == 0 && value != nullptr) {
                expected.insert(std::string(name) + "\n");
            }
            std::free(value);
        }
        CHECK(names == expected);
        CHECK_EQ(back.size(), expected.size() + 4);
        REQUIRE(back.size() >= 4);
        CHECK_EQ(back.at(back.size() - 4), std::string("QIVEN_PAIR=v \xc3\xab\n"));
        CHECK_EQ(back.at(back.size() - 3), std::string("TEMP=C:\\given\n"));
        CHECK(back.at(back.size() - 2).starts_with("SystemRoot="));
        CHECK_EQ(back.at(back.size() - 1), std::string("PATH unset\n"));
        for (const std::string& name : names) {
            std::cout << "win_process.environment: " << name;
        }
    }
    CHECK_EQ(arena.used(), 0U);
}

QIVEN_CASE(win_process, write_close_input) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        ChildView run({"cat", "5"}, Seconds(60.0), Caps{4U << 20U, 1024});
        WindowsChild child = platform.start(run.command);
        // Past one write's chunk of 1 MiB, the child copying as it reads.
        const std::string many((std::size_t{3} << 20U) + 7, 'y');
        child.write("one\ntwo");
        child.write(many);
        child.close_input();
        child.close_input();
        const Thrown late = thrown([&] { child.write("late"); });
        CHECK_EQ(late.code, Code::io);
        CHECK_EQ(late.operation, std::string("write"));
        String line{Allocator<char>(arena)};
        const std::vector<std::string> back = lines(child, line);
        REQUIRE(back.size() == 2);
        CHECK_EQ(back.at(0), std::string("one\n"));
        CHECK(back.at(1) == "two" + many);
        CHECK_EQ(child.wait(Seconds(10.0)), 5);
        CHECK_EQ(child.wait(Seconds(0.0)), 5);
    }
    CHECK_EQ(arena.used(), 0U);
}

// wait past its timeout: deadline, the child running; kill(): its code 1,
// again nothing; the process gone.
QIVEN_CASE(win_process, wait_kill) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        ChildView run({"sleep", "60000"});
        WindowsChild child = platform.start(run.command);
        String line{Allocator<char>(arena)};
        REQUIRE(child.read_line(Seconds(30.0), line));
        const std::string pid = std::string(line).substr(4);
        const Clock::time_point start = Clock::now();
        const Thrown t = thrown([&] { static_cast<void>(child.wait(Seconds(0.3))); });
        CHECK_EQ(t.code, Code::deadline);
        CHECK(since(start) >= 0.29);
        CHECK_EQ(gone(platform, line, pid, 0), std::string("running\n"));
        child.kill();
        child.kill();
        CHECK_EQ(child.wait(Seconds(5.0)), 1);
        CHECK_EQ(gone(platform, line, pid, 1000), std::string("gone\n"));
    }
    CHECK_EQ(arena.used(), 0U);
}

// A grandchild in the tree: gone by kill(); by its supervisor's death,
// which closes the supervisor's job (kill on close); by the Child's end
// and by a move onto it; each within 1 s.
QIVEN_CASE(win_process, tree) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String line{Allocator<char>(arena)};
        const auto ids = [&](WindowsChild& child) {
            std::array<std::string, 2> out;
            for (std::string& id : out) {
                REQUIRE(child.read_line(Seconds(30.0), line));
                id = std::string(line).substr(line.find(' ') + 1);
            }
            return out;
        };
        ChildView killed({"tree", "60000"});
        WindowsChild child = platform.start(killed.command);
        const auto [grandchild, parent] = ids(child);
        CHECK_EQ(gone(platform, line, grandchild, 0), std::string("running\n"));
        child.kill();
        CHECK_EQ(gone(platform, line, grandchild, 1000), std::string("gone\n"));
        CHECK_EQ(gone(platform, line, parent, 1000), std::string("gone\n"));
        CHECK_EQ(child.wait(Seconds(5.0)), 1);

        ChildView supervisor({"tree", "60000", "exit"});
        WindowsChild dying = platform.start(supervisor.command);
        const auto [orphan, ended] = ids(dying);
        CHECK_EQ(dying.wait(Seconds(30.0)), 3);
        CHECK_EQ(gone(platform, line, orphan, 1000), std::string("gone\n"));
        CHECK_EQ(gone(platform, line, ended, 0), std::string("gone\n"));

        std::optional<WindowsChild> held(platform.start(killed.command));
        const auto [held_grandchild, held_parent] = ids(*held);
        held.reset();
        CHECK_EQ(gone(platform, line, held_grandchild, 1000), std::string("gone\n"));
        CHECK_EQ(gone(platform, line, held_parent, 1000), std::string("gone\n"));

        WindowsChild target = platform.start(killed.command);
        const auto [moved_grandchild, moved_parent] = ids(target);
        target = platform.start(ChildView({"sleep", "0"}).command);
        CHECK_EQ(gone(platform, line, moved_grandchild, 1000), std::string("gone\n"));
        CHECK_EQ(gone(platform, line, moved_parent, 1000), std::string("gone\n"));
        CHECK_EQ(target.wait(Seconds(10.0)), 0);
    }
    CHECK_EQ(arena.used(), 0U);
}

// The child's working directory, as its cwd form prints it: an empty cwd
// the caller's own, a given one itself; by run and by start.
QIVEN_CASE(win_process, cwd) {
    const Scratch scratch("win-process");
    const fs::path own = scratch.root / "own";
    const fs::path given = scratch.root / "given";
    fs::create_directory(own);
    fs::create_directory(given);
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        const Here here(own);
        const std::string given_text = text(given);
        for (const auto& [cwd, where] : {std::pair<std::string_view, fs::path>{"", own}, {given_text, given}}) {
            ChildView echo({"cwd"});
            echo.command.cwd = cwd;
            Ran ran(arena);
            platform.run(echo.command, "", ran);
            CHECK_EQ(ran.code, 0);
            CHECK(fs::equivalent(named(std::string_view(ran.out)), where));
            WindowsChild child = platform.start(echo.command);
            String line{Allocator<char>(arena)};
            const std::vector<std::string> back = lines(child, line);
            REQUIRE(back.size() == 1);
            CHECK(fs::equivalent(named(back.front()), where));
            CHECK_EQ(child.wait(Seconds(10.0)), 0);
        }
    }
    CHECK_EQ(arena.used(), 0U);
}

// A child killed between replace's write and its rename, 20 times: the old
// bytes whole each time, and one temporary file left, holding the new
// bytes. The child's replace is the adapter's, its target held by a reader
// of the child's own so that the rename is retried; the kill comes once the
// temporary file holds the new bytes and two of the rename's tries have
// passed.
QIVEN_CASE(win_process, replace_killed) {
    const Scratch scratch("win-process");
    const fs::path target = scratch.root / "killed.txt";
    const std::string old_bytes = "the old bytes, whole\n";
    const std::string new_bytes = "the new bytes\n";
    write_bytes(target, old_bytes);
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String line{Allocator<char>(arena)};
        const ChildView replacing({"replace", text(target), hex(new_bytes), "60000"});
        for (int k = 0; k < 20; ++k) {
            WindowsChild child = platform.start(replacing.command);
            REQUIRE(child.read_line(Seconds(30.0), line));
            CHECK_EQ(std::string(line), std::string("held\n"));
            const Clock::time_point start = Clock::now();
            bool written = false;
            while (!written && since(start) < 30.0) {
                const std::vector<fs::path> left = temporaries(scratch.root);
                std::error_code error;
                written = left.size() == 1 && fs::file_size(left.front(), error) == new_bytes.size() && !error;
                std::this_thread::sleep_for(std::chrono::milliseconds(written ? 100 : 5));
            }
            REQUIRE(written);
            child.kill();
            CHECK_EQ(child.wait(Seconds(10.0)), 1);
            CHECK_EQ(bytes_of(target), old_bytes);
            const std::vector<fs::path> left = temporaries(scratch.root);
            REQUIRE(left.size() == 1);
            CHECK_EQ(bytes_of(left.front()), new_bytes);
            fs::remove(left.front());
        }
    }
    CHECK_EQ(arena.used(), 0U);
}
