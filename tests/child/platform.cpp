// platform: the children of the platform's process cases (P10 to P15, P20's
// child half and win/process' own cases), each started by the Windows
// adapter's start. Each writes its streams by WriteFile, byte for byte, a
// line ended by LF alone; a number is decimal. Its forms:
// - sleep MS: "pid <its id>" on out, then MS milliseconds asleep; exit 0.
// - env [NAME...]: each name of its environment block a line, in the
//   block's order; then each NAME given as "NAME=<value>", or "NAME unset".
// - argv [ARG...]: each ARG a line, its UTF-8 bytes in hex, an empty ARG
//   an empty line.
// - flood out|err BYTES [MS]: BYTES bytes on the stream, lines of 63 'x'
//   and an LF, the last cut; then MS milliseconds asleep; exit 0.
// - print OUT ERR CODE: OUT's bytes, in hex, on out and ERR's on err;
//   exit CODE.
// - cat [CODE]: its input copied to out to the input's end; exit CODE.
// - replace PATH HEX MS: PATH held by a reader of its own, sharing reading
//   alone, so that a rename onto it fails and is retried; "held" on out;
//   then the adapter's own replace of PATH by HEX's bytes, its deadline MS
//   milliseconds: its temporary file written, flushed and closed, its
//   rename retried every 50 ms, the time a kill falls between the write and
//   the rename; past the deadline, the error's code on out ("busy"), exit 1.
// - tree MS [exit|out BYTES|err BYTES]: "platform sleep MS" started by the
//   adapter's start, a grandchild in a job of its own nested in its
//   parent's, its id on out as "grandchild <id>", then "pid <its id>"; then
//   MS milliseconds asleep; with exit ended at once by _Exit(3), its job's
//   handle closed by the system, which kills the grandchild (kill on
//   close); with out or err, once its input gives a line or ends, first
//   BYTES bytes on that stream, as flood's, so its two lines are read
//   before a cap is passed.
// - gone PID MS: "gone" where the process PID ends within MS milliseconds
//   or is none, else "running".
// - init CONFIG DIR: "ready" on out; once its input gives a line or ends,
//   location's init of DIR with --accept, on the adapter made with CONFIG
//   as its configuration directory (instances.cpp instantiates location's
//   main on it): init's lines on out, its exit status the child's. Two at
//   once over one CONFIG are P20's child half.
// A wrong argument ends it with child::usage_status and a line on err; a
// failed call with 1. The forms run on Windows alone: elsewhere usage.

#include <span>
#include <string_view>

#include "child/child.hpp"

#if !defined(_WIN32)
#include <cstdio>
#endif

#if defined(_WIN32)
#include <windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "memory/arena.hpp"
#include "platform/win/windows.hpp"
#include "qiven3/location.hpp"
#endif

namespace qiven3::child {

#if defined(_WIN32)

namespace {

void put(DWORD stream, std::string_view bytes) {
    const HANDLE h = GetStdHandle(stream);
    while (!bytes.empty()) {
        DWORD wrote = 0;
        const auto n = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), std::size_t{1} << 20U));
        if (WriteFile(h, bytes.data(), n, &wrote, nullptr) == 0) {
            return;  // the reader gone: what is left has no reader
        }
        bytes.remove_prefix(wrote);
    }
}

int wrong(std::string_view why) {
    put(STD_ERROR_HANDLE, std::string("qiven3-child platform: ").append(why).append("\n"));
    return usage_status;
}

int failed(std::string_view call) {
    put(STD_ERROR_HANDLE, std::string("qiven3-child platform: ").append(call).append(" failed: ")
                              .append(std::to_string(GetLastError())).append("\n"));
    return 1;
}

std::optional<std::uint64_t> number(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size() && !text.empty() ? std::optional(value)
                                                                                      : std::nullopt;
}

std::optional<std::string> unhex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::string out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        std::uint8_t byte = 0;
        const auto [end, error] = std::from_chars(hex.data() + i, hex.data() + i + 2, byte, 16);
        if (error != std::errc() || end != hex.data() + i + 2) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>(byte));
    }
    return out;
}

std::string utf8(std::wstring_view wide) {
    std::string out(wide.size() * 3, '\0');
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                                      static_cast<int>(out.size()), nullptr, nullptr);
    out.resize(n > 0 ? static_cast<std::size_t>(n) : 0U);
    return out;
}

std::wstring wide(std::string_view text) {
    std::wstring out(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                                      static_cast<int>(out.size()));
    out.resize(n > 0 ? static_cast<std::size_t>(n) : 0U);
    return out;
}

void pid_line(std::string_view label, std::uint64_t id) {
    put(STD_OUTPUT_HANDLE, std::string(label) + " " + std::to_string(id) + "\n");
}

int env(std::span<const std::string_view> names) {
    wchar_t* const block = GetEnvironmentStringsW();
    if (block == nullptr) {
        return failed("GetEnvironmentStringsW");
    }
    for (const wchar_t* at = block; *at != L'\0'; at += std::wstring_view(at).size() + 1) {
        const std::wstring_view pair(at);
        put(STD_OUTPUT_HANDLE, utf8(pair.substr(0, pair.find(L'=', 1))) + "\n");
    }
    FreeEnvironmentStringsW(block);
    for (const std::string_view name : names) {
        std::wstring value(32767, L'\0');
        const DWORD n = GetEnvironmentVariableW(wide(name).c_str(), value.data(), static_cast<DWORD>(value.size()));
        put(STD_OUTPUT_HANDLE, std::string(name) + (n == 0 ? std::string(" unset")
                                                           : "=" + utf8(std::wstring_view(value.data(), n))) + "\n");
    }
    return 0;
}

int flood(std::span<const std::string_view> args) {
    const std::optional<std::uint64_t> bytes = args.size() >= 2 ? number(args[1]) : std::nullopt;
    const std::optional<std::uint64_t> ms = args.size() == 3 ? number(args[2]) : std::optional<std::uint64_t>(0);
    if (!bytes || !ms || args.size() > 3 || (args[0] != "out" && args[0] != "err")) {
        return wrong("flood out|err BYTES [MS]");
    }
    std::string chunk;
    for (std::size_t i = 0; i < 1024; ++i) {
        chunk.append(63, 'x').push_back('\n');
    }
    for (std::uint64_t left = *bytes; left > 0;) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(left, chunk.size()));
        put(args[0] == "out" ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE, std::string_view(chunk).substr(0, n));
        left -= n;
    }
    Sleep(static_cast<DWORD>(*ms));
    return 0;
}

int cat(std::span<const std::string_view> args) {
    const std::optional<std::uint64_t> code = args.empty() ? std::optional<std::uint64_t>(0) : number(args[0]);
    if (!code || args.size() > 1) {
        return wrong("cat [CODE]");
    }
    std::vector<char> buffer(std::size_t{1} << 16U);
    DWORD got = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer.data(), static_cast<DWORD>(buffer.size()), &got,
                    nullptr) != 0 && got > 0) {
        put(STD_OUTPUT_HANDLE, std::string_view(buffer.data(), got));
    }
    return static_cast<int>(*code);
}

int replace(std::span<const std::string_view> args) {
    const std::optional<std::string> data = args.size() == 3 ? unhex(args[1]) : std::nullopt;
    const std::optional<std::uint64_t> ms = args.size() == 3 ? number(args[2]) : std::nullopt;
    if (!data || !ms) {
        return wrong("replace PATH HEX MS");
    }
    const std::wstring path = wide(args[0]);
    const HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (reader == INVALID_HANDLE_VALUE) {
        return failed("CreateFileW");
    }
    put(STD_OUTPUT_HANDLE, "held\n");
    int status = 0;
    try {
        platform::win::WindowsPlatform adapter(std::nullopt);
        adapter.replace(args[0], *data, platform::Seconds(static_cast<double>(*ms) / 1000.0));
        put(STD_OUTPUT_HANDLE, "replaced\n");
    } catch (const platform::PlatformError& error) {
        put(STD_OUTPUT_HANDLE, std::string(platform::name(error.code)) + "\n");
        status = 1;
    }
    CloseHandle(reader);
    return status;
}

// Until the input gives a line or ends.
void await_line() {
    char byte = 0;
    DWORD got = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &got, nullptr) != 0 && got == 1 && byte != '\n') {
    }
}

int tree(std::span<const std::string_view> args) {
    const std::optional<std::uint64_t> ms = args.empty() ? std::nullopt : number(args[0]);
    const bool floods = args.size() == 3 && (args[1] == "out" || args[1] == "err") && number(args[2]).has_value();
    if (!ms || !(args.size() == 1 || (args.size() == 2 && args[1] == "exit") || floods)) {
        return wrong("tree MS [exit|out BYTES|err BYTES]");
    }
    std::wstring self(32767, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (n == 0 || n == self.size()) {
        return failed("GetModuleFileNameW");
    }
    const std::string program = utf8(std::wstring_view(self.data(), n));
    const std::array<std::string_view, 4> argv = {program, "platform", "sleep", args[0]};
    std::vector<std::byte> pages(std::size_t{1} << 16U);
    memory::Arena arena{std::span<std::byte>(pages)};
    try {
        platform::win::WindowsPlatform adapter(std::nullopt);
        const platform::Seconds deadline(static_cast<double>(*ms) / 1000.0 + 60.0);
        platform::win::WindowsChild grandchild =
            adapter.start(platform::Command{argv, "", {}, deadline, platform::Caps{1U << 16U, 1U << 16U}});
        platform::String line{memory::Allocator<char>(arena)};
        if (!grandchild.read_line(platform::Seconds(30.0), line) || !line.starts_with("pid ")) {
            return wrong("the grandchild printed no pid");
        }
        put(STD_OUTPUT_HANDLE, "grandchild " + std::string(std::string_view(line).substr(4)));
        pid_line("pid", GetCurrentProcessId());
        if (args.size() == 2) {
            std::_Exit(3);  // no destructor: the system closes the job's handle
        }
        if (floods) {
            await_line();
            const std::array<std::string_view, 3> past = {args[1], args[2], args[0]};
            return flood(past);
        }
        Sleep(static_cast<DWORD>(*ms));
    } catch (const platform::PlatformError& error) {
        return wrong(std::string("start: ") + std::string(error.detail.view()));
    }
    return 0;
}

int gone(std::span<const std::string_view> args) {
    const std::optional<std::uint64_t> id = args.size() == 2 ? number(args[0]) : std::nullopt;
    const std::optional<std::uint64_t> ms = args.size() == 2 ? number(args[1]) : std::nullopt;
    if (!id || !ms || *id > MAXDWORD) {
        return wrong("gone PID MS");
    }
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(*id));
    if (process == nullptr) {
        if (GetLastError() != ERROR_INVALID_PARAMETER) {
            return failed("OpenProcess");
        }
        put(STD_OUTPUT_HANDLE, "gone\n");
        return 0;
    }
    const DWORD status = WaitForSingleObject(process, static_cast<DWORD>(*ms));
    CloseHandle(process);
    put(STD_OUTPUT_HANDLE, status == WAIT_OBJECT_0 ? "gone\n" : "running\n");
    return 0;
}

int init(std::span<const std::string_view> args) {
    if (args.size() != 2) {
        return wrong("init CONFIG DIR");
    }
    put(STD_OUTPUT_HANDLE, "ready\n");
    await_line();
    const std::array<std::string_view, 5> argv = {"init", args[1], "--accept", "--config", args[0]};
    try {
        platform::win::WindowsPlatform adapter(args[0]);
        return location::main(std::span<const std::string_view>(argv), adapter);
    } catch (const platform::PlatformError& error) {
        put(STD_ERROR_HANDLE, std::string("qiven3-child platform: init: ").append(error.detail.view()).append("\n"));
        return 1;
    }
}

}  // namespace

int platform(std::span<const std::string_view> args) {
    const std::string_view what = args.empty() ? std::string_view() : args[0];
    const std::span<const std::string_view> rest = args.empty() ? args : args.subspan(1);
    if (what == "sleep") {
        const std::optional<std::uint64_t> ms = rest.size() == 1 ? number(rest[0]) : std::nullopt;
        if (!ms) {
            return wrong("sleep MS");
        }
        pid_line("pid", GetCurrentProcessId());
        Sleep(static_cast<DWORD>(*ms));
        return 0;
    }
    if (what == "env") {
        return env(rest);
    }
    if (what == "argv") {
        for (const std::string_view arg : rest) {
            std::string line;
            for (const char c : arg) {
                constexpr std::string_view digits = "0123456789abcdef";
                line.push_back(digits.at(static_cast<unsigned char>(c) >> 4U));
                line.push_back(digits.at(static_cast<unsigned char>(c) & 15U));
            }
            put(STD_OUTPUT_HANDLE, line + "\n");
        }
        return 0;
    }
    if (what == "flood") {
        return flood(rest);
    }
    if (what == "print") {
        const std::optional<std::string> out = rest.size() == 3 ? unhex(rest[0]) : std::nullopt;
        const std::optional<std::string> err = rest.size() == 3 ? unhex(rest[1]) : std::nullopt;
        const std::optional<std::uint64_t> code = rest.size() == 3 ? number(rest[2]) : std::nullopt;
        if (!out || !err || !code) {
            return wrong("print OUT ERR CODE");
        }
        put(STD_OUTPUT_HANDLE, *out);
        put(STD_ERROR_HANDLE, *err);
        return static_cast<int>(*code);
    }
    if (what == "cat") {
        return cat(rest);
    }
    if (what == "replace") {
        return replace(rest);
    }
    if (what == "tree") {
        return tree(rest);
    }
    if (what == "gone") {
        return gone(rest);
    }
    if (what == "init") {
        return init(rest);
    }
    return wrong("sleep|env|argv|flood|print|cat|replace|tree|gone|init: an unknown form");
}

#else

int platform(std::span<const std::string_view> /*args*/) {
    std::fputs("qiven3-child platform: the Windows adapter's children alone\n", stderr);
    return usage_status;
}

#endif

}  // namespace qiven3::child
