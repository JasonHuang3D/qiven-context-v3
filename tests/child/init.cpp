// init CONFIG DIR: P20's child half, location's init run by a child the
// Windows adapter starts (the platform's own children are its program's,
// foundation/system/platform/tests/child/child.cpp). "ready" on out; once
// its input gives a line or ends, location's init of DIR with --accept, on
// the adapter made with CONFIG as its configuration directory
// (instances.cpp instantiates location's main on it): init's lines on out,
// its exit status the child's. Two at once over one CONFIG are P20's child
// half. Its streams are written by WriteFile, byte for byte. A wrong
// argument ends it with child::usage_status and a line on err; a failed
// call with 1. It runs on Windows alone: elsewhere usage.

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
#include <cstddef>
#include <string>

#include "qiven/platform/windows/windows.hpp"
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

// Until the input gives a line or ends.
void await_line() {
    char byte = 0;
    DWORD got = 0;
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), &byte, 1, &got, nullptr) != 0 && got == 1 && byte != '\n') {
    }
}

}  // namespace

int init(std::span<const std::string_view> args) {
    if (args.size() != 2) {
        put(STD_ERROR_HANDLE, "qiven3-child init: init CONFIG DIR\n");
        return usage_status;
    }
    put(STD_OUTPUT_HANDLE, "ready\n");
    await_line();
    const std::array<std::string_view, 5> argv = {"init", args[1], "--accept", "--config", args[0]};
    try {
        qiven::platform::win::WindowsPlatform adapter(args[0]);
        return location::main(std::span<const std::string_view>(argv), adapter);
    } catch (const qiven::platform::PlatformError& error) {
        put(STD_ERROR_HANDLE, std::string("qiven3-child init: ").append(error.detail.view()).append("\n"));
        return 1;
    }
}

#else

int init(std::span<const std::string_view> /*args*/) {
    std::fputs("qiven3-child init: on the Windows adapter alone\n", stderr);
    return usage_status;
}

#endif

}  // namespace qiven3::child
