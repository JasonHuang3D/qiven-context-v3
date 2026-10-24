// signal: Linux's fault handlers, async-signal-safe. This file calls only
// write, _exit, raise and sigaction of POSIX's async-signal-safe functions,
// backtrace and backtrace_symbols_fd, which install() loads before any
// fault, and lock-free atomics; K18 reads its object's undefined symbols
// against that list. The kept rings' table is here, which the report reads
// by diag/events.hpp's inline reader. Nothing is allocated, and no stdio,
// lock, exception or json is used: each line is built in a buffer on the stack, the
// alternate one in a handler. No checked member of the standard library
// is called either (indexing, substr, remove_prefix), since the hardened
// library's report of a failed check is not on the list; nor a string
// literal's length at run time, strlen. The frames are
// backtrace_symbols_fd's raw lines, which the watcher symbolizes after the
// exit.

#include "diag/signal.hpp"

#include <execinfo.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>

#include "diag/events.hpp"

namespace qiven3::diag::detail {

namespace {

using namespace std::string_view_literals;

constexpr int frames_max = 32;
constexpr std::size_t line_max = 300;
constexpr std::size_t part_max = 128;
// A fault this near the faulting thread's stack pointer is in the stack's
// guard: a stack overflow.
constexpr std::uintptr_t stack_near = 64 * 1024;

// The first report's signal, 0 before it: a second report ends the process
// at once with the first's signal.
std::atomic<int> first{0};
static_assert(std::atomic<int>::is_always_lock_free);

// The default action, no mask, no flags: zero-initialized, so that no code
// clears it in a handler.
const struct sigaction default_action{};

// The kept rings, null where a slot is free.
Rings rings{};

void out(std::string_view bytes) noexcept {
    std::size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t wrote = ::write(STDERR_FILENO, std::next(bytes.data(), static_cast<std::ptrdiff_t>(done)),
                                      bytes.size() - done);
        if (wrote <= 0) {
            return;
        }
        done += static_cast<std::size_t>(wrote);
    }
}

// One line, built in place and written in parts of part_max bytes; text
// past the bound is dropped. The part keeps a Text small: the build fills
// each automatic object with a pattern, and the compiler may fill one past
// 256 bytes by a call of memset, which is not on the list.
class Text {
public:
    Text& put(char c) noexcept {
        if (count_ < line_max) {
            if (size_ == part_max) {
                flush();
            }
            *std::next(bytes_.begin(), static_cast<std::ptrdiff_t>(size_)) = c == '\n' ? ' ' : c;
            ++size_;
            ++count_;
        }
        return *this;
    }
    Text& put(std::string_view part) noexcept {
        for (const char c : part) {
            put(c);
        }
        return *this;
    }
    Text& number(std::uint64_t value) noexcept {
        if (value >= 10) {
            number(value / 10);
        }
        return put(static_cast<char>('0' + value % 10));
    }
    // 0x and sixteen upper-case digits.
    Text& hex(std::uint64_t value) noexcept {
        put("0x"sv);
        for (unsigned shift = 64; shift > 0;) {
            shift -= 4;
            const auto digit = static_cast<unsigned>((value >> shift) & 0xFU);
            put(static_cast<char>(digit < 10 ? '0' + digit : 'A' + digit - 10));
        }
        return *this;
    }
    // The line, with its newline unless `newline` is false.
    void write(bool newline = true) noexcept {
        if (newline) {
            *std::next(bytes_.begin(), static_cast<std::ptrdiff_t>(size_)) = '\n';
        }
        out({bytes_.data(), size_ + (newline ? 1 : 0)});
    }

private:
    void flush() noexcept {
        out({bytes_.data(), size_});
        size_ = 0;
    }

    // Written up to size_ alone, the part's room and the newline's: no code
    // clears it.
    std::array<char, part_max + 1> bytes_;
    std::size_t size_ = 0;
    // The line's bytes so far, up to line_max.
    std::size_t count_ = 0;
};
static_assert(sizeof(Text) <= 256);

std::string_view name(int number) noexcept {
    switch (number) {
    case SIGSEGV: return "SIGSEGV"sv;
    case SIGBUS: return "SIGBUS"sv;
    case SIGFPE: return "SIGFPE"sv;
    case SIGILL: return "SIGILL"sv;
    case SIGABRT: return "SIGABRT"sv;
    default: return "a signal"sv;
    }
}

// The faulting thread's stack pointer, from the handler's context; 0 where
// it is not read.
std::uintptr_t stack_pointer(const void* context) noexcept {
#if defined(__linux__) && defined(__x86_64__)
    return static_cast<std::uintptr_t>(static_cast<const ucontext_t*>(context)->uc_mcontext.gregs[REG_RSP]);
#elif defined(__linux__) && defined(__aarch64__)
    return static_cast<const ucontext_t*>(context)->uc_mcontext.sp;
#else
    (void)context;
    return 0;
#endif
}

// The end: the signal again under its default action, which SA_NODEFER
// leaves unblocked in a handler, so that the status is the signal's.
[[noreturn]] void end(int number) noexcept {
    ::sigaction(number, &default_action, nullptr);
    ::raise(number);
    _exit(128 + number);
}

// The first report's claim; a second ends the process with the first's.
void claim(int number) noexcept {
    int before = 0;
    if (!first.compare_exchange_strong(before, number)) {
        end(before);
    }
}

// The report after its cause line: the stack, the events and the end.
[[noreturn]] void finish(int number) noexcept {
    // backtrace fills it: no code clears it.
    std::array<void*, frames_max> frames;
    const int n = backtrace(frames.data(), frames_max);
    Text head;
    head.put("fatal: stack "sv).number(static_cast<std::uint64_t>(n < 0 ? 0 : n)).write();
    for (int i = 0; i < n; ++i) {
        Text frame;
        frame.put("  #"sv).number(static_cast<std::uint64_t>(i)).put(' ').write(false);
        backtrace_symbols_fd(std::next(frames.data(), i), 1, STDERR_FILENO);
    }
    events(
        rings,
        [](std::uint64_t count) noexcept {
            Text line;
            line.put("fatal: events "sv).number(count).write();
        },
        [](std::string_view line) noexcept {
            out(line);
            out("\n"sv);
        });
    // Linux writes no minidump.
    out("fatal: dump none\n"sv);
    Text last;
    last.put("fatal: end "sv).put(name(number)).write();
    end(number);
}

}  // namespace

bool keep_ring(const log::Ring& ring) noexcept { return hold(rings, ring); }

bool drop_ring(const log::Ring& ring) noexcept { return release(rings, ring); }

void on_signal(int number, siginfo_t* info, void* context) noexcept {
    claim(number);
    Text cause;
    cause.put("fatal: "sv).put(name(number));
    if (number != SIGABRT) {
        const auto at = std::bit_cast<std::uintptr_t>(info->si_addr);
        cause.put(" at "sv).hex(at);
        const std::uintptr_t sp = stack_pointer(context);
        if (number == SIGSEGV && sp != 0 && (at > sp ? at - sp : sp - at) <= stack_near) {
            cause.put(", stack overflow"sv);
        }
    }
    cause.write();
    finish(number);
}

void report(std::string_view cause, int number) noexcept {
    claim(number);
    Text line;
    line.put("fatal: "sv).put(cause).write();
    finish(number);
}

}  // namespace qiven3::diag::detail
