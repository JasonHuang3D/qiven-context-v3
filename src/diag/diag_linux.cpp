// diag on Linux and macOS: the install, the alternate stacks and the
// watcher's permission. The handlers and the report are signal.cpp's, the
// terminate handler's report among them.

#include <execinfo.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

#include <array>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <string_view>

#include "diag/diag.hpp"
#include "diag/signal.hpp"

namespace qiven3::diag {

namespace {

#ifdef __linux__
// The process QIVEN3_WATCHER names, by its decimal id, or 0.
unsigned long watcher() noexcept {
    const char* value = std::getenv("QIVEN3_WATCHER");
    if (value == nullptr) {
        return 0;
    }
    unsigned long id = 0;
    const std::string_view text(value);
    if (text.empty() || text.size() > 9) {
        return 0;
    }
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return 0;
        }
        id = id * 10 + static_cast<unsigned long>(c - '0');
    }
    return id;
}
#endif

}  // namespace

void thread_start() noexcept {
#ifndef QIVEN3_SANITIZED
    // Each thread's alternate stack, for a handler of a stack overflow.
    constexpr std::size_t alternate_bytes = 64 * 1024;
    stack_t now{};
    if (sigaltstack(nullptr, &now) == 0 && (now.ss_flags & SS_DISABLE) == 0) {
        return;
    }
    // Pages of its own, for as long as the thread lives: a residual on a
    // thread's exit, which keeps them mapped.
    void* pages = mmap(nullptr, alternate_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pages == MAP_FAILED) {
        return;
    }
    stack_t alternate{};
    alternate.ss_sp = pages;
    alternate.ss_size = alternate_bytes;
    if (sigaltstack(&alternate, nullptr) != 0) {
        munmap(pages, alternate_bytes);
    }
#endif
}

void install() noexcept {
    std::set_terminate(detail::on_terminate);
    // backtrace loads its library on its first call: before any fault.
    std::array<void*, 1> one{};
    backtrace(one.data(), 1);
#ifndef QIVEN3_SANITIZED
    // Under a sanitizer its own report stands for the handlers'.
    thread_start();
    struct sigaction action{};
    action.sa_sigaction = detail::on_signal;
    // The flags are unsigned in glibc, SA_RESETHAND its top bit; sa_flags is an int.
    action.sa_flags = static_cast<int>(SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER);
    sigemptyset(&action.sa_mask);
    for (const int number : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) {
        sigaction(number, &action, nullptr);
    }
#endif
#ifdef __linux__
    // The watcher's debugger may attach under Yama's ptrace_scope 1.
    if (const unsigned long id = watcher(); id != 0) {
        prctl(PR_SET_PTRACER, id, 0UL, 0UL, 0UL);
    }
#endif
}

namespace detail {

// Linux ends a fail-fast as it does an abort: by SIGABRT.
void fatal(std::string_view cause, End /*end*/) noexcept { report(cause, SIGABRT); }

}  // namespace detail

}  // namespace qiven3::diag
