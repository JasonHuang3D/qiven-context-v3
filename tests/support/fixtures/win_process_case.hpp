#pragma once

// win/process' cases' helpers (win_process_*_test.cpp): qiven3-child, which
// the build puts beside the case binary, started by the adapter; a scratch
// directory of the case's own; a PlatformError caught whole; an arena's
// pages off the stack; text as hex, as `qiven3-child platform` prints it.
//
// thread: one

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "memory/arena.hpp"
#include "platform/win/windows.hpp"

namespace qiven3::test::process {

namespace fs = std::filesystem;
using platform::Caps;
using platform::Code;
using platform::Command;
using platform::PlatformError;
using platform::Seconds;
using platform::String;
using platform::win::WindowsChild;
using platform::win::WindowsPlatform;
using Clock = std::chrono::steady_clock;

inline std::string text(const fs::path& path) {
    std::string out;
    for (const char8_t c : path.u8string()) {
        out.push_back(static_cast<char>(c));
    }
    return out;
}

// qiven3-child beside this binary, by the C runtime's own program path.
inline std::string child_program() {
    char* self = nullptr;
    if (_get_pgmptr(&self) != 0 || self == nullptr) {
        return "";
    }
    return text(fs::path(self).parent_path() / "qiven3-child.exe");
}

// A scratch directory of the case's own, removed at its end.
struct Scratch {
    explicit Scratch(std::string_view name) {
        std::random_device draw;
        root = fs::temp_directory_path() /
               ("qiven3-" + std::string(name) + "-" + std::to_string(draw()) + std::to_string(draw()));
        fs::create_directories(root);
        root = fs::canonical(root);
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
    ~Scratch() {
        try {
            std::error_code error;
            fs::remove_all(root, error);
        } catch (const std::exception&) {
            static_cast<void>(std::fputs("win_process_case: the scratch directory was not removed\n", stderr));
        }
    }
    fs::path root;
};

struct Pages {
    std::array<std::byte, std::size_t{1} << 22U> bytes{};
};

// The PlatformError op throws: its code, operation and detail; "none
// thrown" when it returns.
struct Thrown {
    Code code = Code::unsupported;
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

inline std::string hex(std::string_view bytes) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        out.push_back(digits.at(static_cast<unsigned char>(c) >> 4U));
        out.push_back(digits.at(static_cast<unsigned char>(c) & 15U));
    }
    return out;
}

// A Command for `qiven3-child platform <args>`: its argv's strings, held
// here, and the views of them the Command takes.
struct ChildView {
    explicit ChildView(std::vector<std::string> args, Seconds deadline = Seconds(60.0),
                       Caps caps = {1U << 20U, 1U << 20U})
        : given(std::move(args)) {
        given.insert(given.begin(), {child_program(), "platform"});
        for (const std::string& s : given) {
            views.emplace_back(s);
        }
        command.argv = views;
        command.deadline = deadline;
        command.caps = caps;
    }
    ChildView(const ChildView&) = delete;
    ChildView& operator=(const ChildView&) = delete;
    ChildView(ChildView&&) = delete;
    ChildView& operator=(ChildView&&) = delete;
    ~ChildView() = default;
    std::vector<std::string> given;
    std::vector<std::string_view> views;
    Command command;
};

// Each of out's lines to its end, the line ends kept.
inline std::vector<std::string> lines(WindowsChild& child, String& line) {
    std::vector<std::string> out;
    while (child.read_line(Seconds(30.0), line)) {
        out.emplace_back(line);
    }
    return out;
}

// What `qiven3-child platform gone <id> <ms>` answers: gone or running.
inline std::string gone(WindowsPlatform& platform, String& line, std::string_view id, int ms) {
    std::string pid(id);
    while (!pid.empty() && (pid.back() == '\n' || pid.back() == '\r')) {
        pid.pop_back();
    }
    ChildView run({"gone", pid, std::to_string(ms)});
    WindowsChild child = platform.start(run.command);
    return child.read_line(Seconds(30.0), line) ? std::string(line) : std::string("no line");
}

inline double since(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }

}  // namespace qiven3::test::process
