// win/process' streams on this machine: read_line's lines, its end and its
// timeout; each cap, the error naming its stream alone and the tree killed;
// the deadline killing the tree, each next call deadline until kill(); run
// with its input, both streams and the exit code, and its errors named
// run's. Each starts `qiven3-child platform` (platform.cpp): machine's.

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "runner/runner.hpp"
#include "support/fixtures/win_process_case.hpp"

namespace {

using namespace qiven3::test::process;
using qiven3::memory::Allocator;
using qiven3::memory::Arena;
using qiven3::platform::Ran;

// Whether text holds word whole: no word character ([A-Za-z0-9_], a
// regex's \b) on either side of it.
bool holds_word(std::string_view text, std::string_view word) {
    const auto inner = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    for (std::size_t at = text.find(word); at != std::string_view::npos; at = text.find(word, at + 1)) {
        const std::size_t end = at + word.size();
        if ((at == 0 || !inner(text.at(at - 1))) && (end == text.size() || !inner(text.at(end)))) {
            return true;
        }
    }
    return false;
}

// Whether an output error's detail names the stream and not the other.
bool names(const Thrown& t, std::string_view stream) {
    const std::string_view other = stream == "out" ? "err" : "out";
    return t.code == Code::output && holds_word(t.detail, stream) && !holds_word(t.detail, other);
}

}  // namespace

QIVEN3_CASE(win_process, read_line) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        const std::string long_line(5000, 'z');
        ChildView run({"print", hex("a\nbb\r\n\n" + long_line + "\nlast"), hex("on err\n"), "0"});
        WindowsChild child = platform.start(run.command);
        String line{Allocator<char>(arena)};
        const std::vector<std::string> back = lines(child, line);
        REQUIRE(back.size() == 5);
        CHECK_EQ(back.at(0), std::string("a\n"));
        CHECK_EQ(back.at(1), std::string("bb\r\n"));
        CHECK_EQ(back.at(2), std::string("\n"));
        CHECK(back.at(3) == long_line + "\n");
        CHECK_EQ(back.at(4), std::string("last"));
        CHECK(!child.read_line(Seconds(1.0), line));
        CHECK(line.empty());
        CHECK_EQ(child.wait(Seconds(10.0)), 0);
    }
    CHECK_EQ(arena.used(), 0U);
}

// No line within the timeout: deadline, the tree running; once killed,
// the stream's end.
QIVEN3_CASE(win_process, read_line_timeout) {
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
        const Thrown t = thrown([&] { static_cast<void>(child.read_line(Seconds(0.3), line)); });
        const double took = since(start);
        std::cout << "win_process.read_line_timeout: deadline after " << took << " s for 0.3 s\n";
        CHECK_EQ(t.code, Code::deadline);
        CHECK_EQ(t.operation, std::string("read_line"));
        CHECK(took >= 0.29 && took < 5.0);
        CHECK_EQ(gone(platform, line, pid, 0), std::string("running\n"));
        child.kill();
        CHECK(!child.read_line(Seconds(10.0), line));
        CHECK_EQ(child.wait(Seconds(5.0)), 1);
    }
    CHECK_EQ(arena.used(), 0U);
}

// Past each cap: output naming its stream alone, from read_line, wait and
// write, the tree killed (run ends long before the child's sleep); at the
// cap whole. kill() stops the reason.
QIVEN3_CASE(win_process, caps) {
    CHECK(holds_word("out", "out") && holds_word("the (out) cap", "out") && holds_word("x out", "out"));
    CHECK(!holds_word("stdout", "out") && !holds_word("out_", "out") && !holds_word("err0 errs", "err"));
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String line{Allocator<char>(arena)};
        for (const std::string_view stream : {"out", "err"}) {
            ChildView run({"flood", std::string(stream), "100000", "60000"}, Seconds(60.0), Caps{4096, 4096});
            WindowsChild child = platform.start(run.command);
            const Thrown read = thrown([&] {
                while (child.read_line(Seconds(30.0), line)) {
                }
            });
            CHECK(names(read, stream));
            CHECK_EQ(read.operation, std::string("read_line"));
            CHECK(names(thrown([&] { static_cast<void>(child.wait(Seconds(5.0))); }), stream));
            CHECK(names(thrown([&] { child.write("x"); }), stream));
            child.kill();
            CHECK_EQ(child.wait(Seconds(5.0)), 1);

            Ran ran(arena);
            const Clock::time_point start = Clock::now();
            const Thrown by_run = thrown([&] { platform.run(run.command, "", ran); });
            const double took = since(start);
            std::cout << "win_process.caps: " << stream << ": " << by_run.detail << " after " << took << " s\n";
            CHECK(names(by_run, stream));
            CHECK_EQ(by_run.operation, std::string("run"));
            CHECK(took < 30.0);
        }
        Ran ran(arena);
        ChildView whole({"flood", "out", "4096"}, Seconds(60.0), Caps{4096, 0});
        platform.run(whole.command, "", ran);
        CHECK_EQ(ran.code, 0);
        CHECK_EQ(ran.out.size(), 4096U);
        CHECK(ran.err.empty());
    }
    CHECK_EQ(arena.used(), 0U);
}

// At the deadline the tree is killed; each next call but kill throws
// deadline; kill() stops it.
QIVEN3_CASE(win_process, deadline) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        String line{Allocator<char>(arena)};
        ChildView run({"tree", "60000"}, Seconds(1.0));
        const Clock::time_point start = Clock::now();
        WindowsChild child = platform.start(run.command);
        REQUIRE(child.read_line(Seconds(30.0), line));
        const std::string grandchild = std::string(line).substr(line.find(' ') + 1);
        const Thrown t = thrown([&] {
            while (child.read_line(Seconds(30.0), line)) {
            }
        });
        const double took = since(start);
        std::cout << "win_process.deadline: " << t.detail << " after " << took << " s for 1 s\n";
        CHECK_EQ(t.code, Code::deadline);
        CHECK(took >= 0.99 && took < 10.0);
        CHECK_EQ(gone(platform, line, grandchild, 1000), std::string("gone\n"));
        for (int i = 0; i < 2; ++i) {
            CHECK_EQ(thrown([&] { static_cast<void>(child.read_line(Seconds(1.0), line)); }).code, Code::deadline);
            CHECK_EQ(thrown([&] { static_cast<void>(child.wait(Seconds(1.0))); }).code, Code::deadline);
            CHECK_EQ(thrown([&] { child.write("x"); }).code, Code::deadline);
        }
        child.kill();
        CHECK_EQ(child.wait(Seconds(5.0)), 1);
    }
    CHECK_EQ(arena.used(), 0U);
}

// run: the input, both streams and a nonzero code as a result; its
// refusals and its deadline named run's, the latter with a child that
// never reads its input.
QIVEN3_CASE(win_process, run) {
    WindowsPlatform platform(std::nullopt);
    const auto pages = std::make_unique<Pages>();
    Arena arena(pages->bytes);
    {
        Ran ran(arena);
        std::string data;
        for (std::size_t i = 0; data.size() < 300000; ++i) {
            data += std::to_string(i) + "\n";
        }
        platform.run(ChildView({"cat", "4"}).command, data, ran);
        CHECK_EQ(ran.code, 4);
        CHECK(std::string_view(ran.out) == data);
        CHECK(ran.err.empty());
        platform.run(ChildView({"print", hex("to out"), hex("to err\n"), "9"}).command, "", ran);
        CHECK_EQ(ran.code, 9);
        CHECK_EQ(std::string(ran.out), std::string("to out"));
        CHECK_EQ(std::string(ran.err), std::string("to err\n"));

        ChildView relative({"sleep", "0"});
        relative.views.at(0) = "qiven3-child.exe";
        relative.command.argv = relative.views;
        const Thrown refused = thrown([&] { platform.run(relative.command, "", ran); });
        CHECK_EQ(refused.code, Code::start);
        CHECK_EQ(refused.operation, std::string("run"));
        for (const std::size_t size : {std::size_t{0}, std::size_t{3} << 20U}) {
            const std::string many(size, 'q');
            const Clock::time_point start = Clock::now();
            const ChildView sleep({"sleep", "60000"}, Seconds(1.0));
            const Thrown late = thrown([&] { platform.run(sleep.command, many, ran); });
            const double took = since(start);
            std::cout << "win_process.run: deadline, " << many.size() << " bytes in, after " << took << " s\n";
            CHECK_EQ(late.code, Code::deadline);
            CHECK_EQ(late.operation, std::string("run"));
            CHECK(took >= 0.99 && took < 10.0);
        }
    }
    CHECK_EQ(arena.used(), 0U);
}
