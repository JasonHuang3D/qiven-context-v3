// diag on Linux and macOS: the install and the report of the terminate
// handler. The signal handlers are signal.cpp's, which this half does not
// hold yet: a fault by signal ends with the system's default action.

#include <execinfo.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <exception>

#include "diag/diag.hpp"

namespace qiven3::diag {

namespace {

using detail::End;
using detail::Line;

constexpr int frames_max = 32;

// The first report's claim: a second ends the process at once.
std::atomic<bool> reported{false};

void write(std::string_view bytes) noexcept {
    while (!bytes.empty()) {
        const ssize_t wrote = ::write(STDERR_FILENO, bytes.data(), bytes.size());
        if (wrote <= 0) {
            return;
        }
        bytes.remove_prefix(static_cast<std::size_t>(wrote));
    }
}

void write(Line& line) noexcept { write(line.line()); }

}  // namespace

void thread_start() noexcept {}

void install() noexcept {
    std::set_terminate(detail::on_terminate);
    // backtrace loads its library on its first call: before any fault.
    std::array<void*, 1> one{};
    backtrace(one.data(), 1);
}

namespace detail {

void fatal(std::string_view cause, End /*end*/) noexcept {
    if (reported.exchange(true)) {
        std::abort();
    }
    Line line;
    write(line.text("fatal: ").text(cause));
    std::array<void*, frames_max> frames{};
    const int n = backtrace(frames.data(), frames_max);
    Line head;
    write(head.text("fatal: stack ").number(static_cast<std::uint64_t>(n)));
    for (int i = 0; i < n; ++i) {
        Line frame;
        const std::string_view at = frame.text("  #").number(static_cast<std::uint64_t>(i)).text(" ").view();
        write(at);
        // The raw line, which the watcher symbolizes after the exit.
        backtrace_symbols_fd(&frames[static_cast<std::size_t>(i)], 1, STDERR_FILENO);
    }
    // The crash buffers are logging's, which keeps none yet.
    write("fatal: events 0\nfatal: dump none\nfatal: end SIGABRT\n");
    std::abort();
}

}  // namespace detail

}  // namespace qiven3::diag
