// entry: the start of every binary, which qiven_executable links as an
// object. On Windows, in two stages: the image's entry point sets the error
// mode, so that no system dialog opens from the first instruction on, and
// starts the C runtime; then the first initializer, in the library's
// segment ahead of every other and of main, installs the rest. On Linux
// the initializer is a constructor of the first priority. With
// QIVEN_DIAG_PROBE=1 the initializer prints "diag: entry" and ends the
// process with 0, before main.

#include "qiven/diag/diag.hpp"

#ifdef _WIN32
#include <windows.h>

// A binary whose entry is wmain, the product's, takes the entry's wide
// variant, compiled with QIVEN_WIDE_MAIN: the C runtime's start for wmain.
#if defined(QIVEN_WIDE_MAIN)
extern "C" DWORD wmainCRTStartup(LPVOID);
#else
extern "C" DWORD mainCRTStartup(LPVOID);
#endif

extern "C" __declspec(safebuffers) DWORD qiven3_entry(LPVOID peb) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#if defined(QIVEN_WIDE_MAIN)
    return wmainCRTStartup(peb);
#else
    return mainCRTStartup(peb);
#endif
}

namespace {

void probe() noexcept {
    std::array<wchar_t, 4> value{};
    if (GetEnvironmentVariableW(L"QIVEN_DIAG_PROBE", value.data(), static_cast<DWORD>(value.size())) == 1
        && value[0] == L'1') {
        DWORD wrote = 0;
        constexpr std::string_view line = "diag: entry\n";
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), static_cast<DWORD>(line.size()), &wrote, nullptr);
        TerminateProcess(GetCurrentProcess(), 0);
    }
}

struct First {
    First() noexcept {
        qiven::diag::install();
        probe();
    }
};

}  // namespace

#pragma init_seg(lib)
const First first;

#else
#include <unistd.h>

#include <cstdlib>
#include <string_view>

extern "C" __attribute__((constructor(101))) void qiven3_entry() {
    qiven::diag::install();
    const char* value = std::getenv("QIVEN_DIAG_PROBE");
    if (value != nullptr && std::string_view(value) == "1") {
        constexpr std::string_view line = "diag: entry\n";
        if (::write(STDOUT_FILENO, line.data(), line.size()) < 0) {
            _exit(1);
        }
        _exit(0);
    }
}
#endif
