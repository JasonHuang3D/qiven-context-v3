// win/process' Child and run (platform/win/windows.hpp): a started process
// tree in its Job Object, which process.cpp's start makes. Its state and
// its streams' buffers live on pages it takes before the process starts:
// the state first, then out's buffer and err's, each its cap and one byte,
// so a stream read to its buffer's end has passed its cap. Three threads
// serve it: each stream's reader, which reads its pipe into its buffer
// until the pipe's end or past the cap, and the deadline's watch, which
// waits on the stop event until the deadline. A thread that ends the tree
// records its reason first, the first one kept, and each change sets the
// changed event, on which read_line and run wait.
//
// Each Win32 call of the Child's own thread asks the countdown start gave
// it first and, when it fires, fails as the call would with error 31
// (ERROR_GEN_FAILURE); a thread's start is one such point. The threads'
// own calls and the waits on the changed event, whose count follows the
// timing, ask none.
//
// thread: shared (a Child serves one thread at a time; its threads write
// each stream's length and end and the reason, each read and written
// through std::atomic_ref alone, the two streams' fields 64 bytes apart)

#include "platform/win/windows.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "memory/fail.hpp"

namespace qiven3::platform::win {
namespace {

using Clock = std::chrono::steady_clock;

// A write's largest chunk, and a read's.
constexpr std::size_t write_chunk = std::size_t{1} << 20U;
constexpr std::size_t read_chunk = std::size_t{1} << 16U;
// The longest deadline or timeout counted, about 31 years.
constexpr double most_seconds = 1e9;
// How long run waits past the deadline for the killed tree's streams.
constexpr Clock::duration grace = std::chrono::seconds(5);

// Why a thread ended the tree; none until one did.
enum class Reason : std::uint32_t { none, deadline, out_cap, err_cap };

[[noreturn]] void failed(Code code, std::string_view operation, std::string_view call, unsigned long error) {
    Detail detail(call);
    detail.add(": ");
    add_error(detail, error);
    throw PlatformError(code, operation, detail);
}

// A timeout as the system's milliseconds, rounded up, below INFINITE.
DWORD milliseconds(Seconds timeout) noexcept {
    const double ms = std::ceil(timeout.count() * 1000.0);
    if (!(ms > 0.0)) {
        return 0;
    }
    return ms >= 4294967294.0 ? DWORD{4294967294U} : static_cast<DWORD>(ms);
}

// Now and a span, none below 0 and none past most_seconds.
Clock::time_point after(Seconds span) {
    const double seconds = span.count() > 0.0 ? std::min(span.count(), most_seconds) : 0.0;
    return Clock::now() + std::chrono::duration_cast<Clock::duration>(Seconds(seconds));
}

template <class T>
std::atomic_ref<T> shared(T& value) noexcept {
    return std::atomic_ref<T>(value);
}

}  // namespace

// A stream, its reader's: its pipe's read end and its buffer, 64 bytes.
struct ChildStream {
    void* pipe = nullptr;
    char* data = nullptr;
    std::size_t capacity = 0;         // the cap and one byte
    std::size_t length = 0;           // shared: the reader's release, the Child's acquire
    std::uint32_t ended = 0;          // shared: 1 at the pipe's end, past the cap or at a failed read
    std::uint32_t error = 0;          // the failed read's error, 0 for none: written before ended
    std::array<std::byte, 24> pad{};  // the other stream's fields 64 bytes apart
};
static_assert(sizeof(ChildStream) == 64, "a stream's fields on one cache line");

// The Child's state on its pages, made there by a copy, so its type is
// trivially copyable: each handle the Child's, closed by its destructor.
struct ChildState {
    ChildStream out;
    ChildStream err;
    void* process = nullptr;
    void* job = nullptr;
    void* input = nullptr;
    void* changed = nullptr;  // auto-reset: a stream grew or ended, or the tree was ended
    void* stop = nullptr;     // manual-reset: set by the destructor, ending the watch
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown = nullptr;
#endif
    Clock::rep deadline = 0;   // the steady clock's count at the deadline, before the threads
    std::size_t given = 0;     // out's bytes read_line gave
    std::uint32_t reason = 0;  // shared: a Reason, the first kept
    std::uint32_t killed = 0;  // kill() called: no reason thrown since
};
// The constructor's memcpy onto the pages makes the state there: so its
// type is trivially copyable, and an aggregate, an implicit-lifetime type.
static_assert(std::is_trivially_copyable_v<ChildState> && std::is_aggregate_v<ChildState>,
              "the state is placed on the pages by memcpy");

namespace {

// The tree ended for `why` unless a reason came first; the change told.
void claim(ChildState& s, Reason why) noexcept {
    std::uint32_t none = 0;
    // acq_rel: pairs with check()'s acquire, before the stream's end.
    if (shared(s.reason).compare_exchange_strong(none, static_cast<std::uint32_t>(why), std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
        TerminateJobObject(s.job, 1);
    }
    SetEvent(s.changed);
}

// A stream's reader: its pipe into its buffer until the pipe's end (109),
// a failed read, or the buffer full, which is past the cap.
void drain(ChildState* s, ChildStream* stream, Reason past) noexcept {
    const std::span<char> buffer(stream->data, stream->capacity);
    std::size_t length = 0;
    for (;;) {
        if (length == buffer.size()) {
            claim(*s, past);
            break;
        }
        const auto chunk = static_cast<DWORD>(std::min(buffer.size() - length, read_chunk));
        DWORD got = 0;
        if (ReadFile(stream->pipe, buffer.subspan(length).data(), chunk, &got, nullptr) == 0) {
            const DWORD error = GetLastError();
            stream->error = error == ERROR_BROKEN_PIPE ? 0 : error;
            break;
        }
        length += got;
        shared(stream->length).store(length, std::memory_order_release);  // pairs with the Child's acquire
        SetEvent(s->changed);
    }
    shared(stream->ended).store(1, std::memory_order_release);  // pairs with the Child's acquire
    SetEvent(s->changed);
}

// The deadline's watch: the tree ended at the deadline, unless the stop
// event comes first.
void watch(ChildState* s) noexcept {
    for (;;) {
        const Clock::duration left(s->deadline - Clock::now().time_since_epoch().count());
        if (left <= Clock::duration::zero()) {
            claim(*s, Reason::deadline);
            return;
        }
        if (WaitForSingleObject(s->stop, milliseconds(left)) != WAIT_TIMEOUT) {
            return;
        }
    }
}

}  // namespace

// Delegating, so the destructor closes what a later failure leaves.
WindowsChild::WindowsChild(const Caps& caps, WindowsFiles& files) : WindowsChild() {
    const std::optional<std::size_t> out_bytes = memory::add(caps.out, 1);
    const std::optional<std::size_t> err_bytes = memory::add(caps.err, 1);
    const std::optional<std::size_t> buffers =
        out_bytes && err_bytes ? memory::add(*out_bytes, *err_bytes) : std::nullopt;
    const std::optional<std::size_t> total = buffers ? memory::add(*buffers, sizeof(ChildState)) : std::nullopt;
    if (!total) {
        throw PlatformError(Code::start, "start", "the caps pass the address space");
    }
    if (files.faulted()) {
        failed(Code::start, "start", "VirtualAlloc", ERROR_GEN_FAILURE);
    }
    try {
        pages_ = take_pages(*total);
    } catch (const PlatformError& error) {
        throw PlatformError(Code::start, "start", error.detail.view());
    }
    const std::span<char> bytes(static_cast<char*>(pages_.address()), *total);
    ChildState fresh;
    fresh.out.data = bytes.subspan(sizeof(ChildState), *out_bytes).data();
    fresh.out.capacity = *out_bytes;
    fresh.err.data = bytes.subspan(sizeof(ChildState) + *out_bytes).data();
    fresh.err.capacity = *err_bytes;
#if defined(QIVEN3_FAULTS)
    fresh.countdown = files.countdown();
#endif
    std::memcpy(bytes.data(), &fresh, sizeof(fresh));
    ChildState& s = state();
    for (const auto& [event, manual] : {std::pair{&s.changed, FALSE}, std::pair{&s.stop, TRUE}}) {
        const bool fault = files.faulted();
        *event = fault ? nullptr : CreateEventW(nullptr, manual, FALSE, nullptr);
        if (*event == nullptr) {
            failed(Code::start, "start", "CreateEventW", fault ? ERROR_GEN_FAILURE : GetLastError());
        }
    }
}

void WindowsChild::begin(void* process, void* job, void* input, void* output, void* error, Seconds deadline) {
    ChildState& s = state();
    s.process = process;
    s.job = job;
    s.input = input;
    s.out.pipe = output;
    s.err.pipe = error;
    s.deadline = after(deadline).time_since_epoch().count();
    for (std::size_t i = 0; i < threads_.size(); ++i) {
        if (faulted()) {
            failed(Code::start, "start", "a thread's start", ERROR_GEN_FAILURE);
        }
        try {
            threads_.at(i) = i == 0   ? std::jthread(drain, &s, &s.out, Reason::out_cap)
                             : i == 1 ? std::jthread(drain, &s, &s.err, Reason::err_cap)
                                      : std::jthread(watch, &s);
        } catch (...) {
            throw PlatformError(Code::start, "start", "a thread's start: the system refused it");
        }
    }
}

WindowsChild& WindowsChild::operator=(WindowsChild&& other) noexcept {
    if (this != &other) {
        WindowsChild old(std::move(*this));
        pages_ = std::move(other.pages_);
        threads_ = std::move(other.threads_);
    }
    return *this;
}

// The tree ends with its Job Object, so do the readers; the watch at the
// stop event.
WindowsChild::~WindowsChild() {
    if (pages_.address() == nullptr) {
        return;
    }
    ChildState& s = state();
    if (s.job != nullptr) {
        TerminateJobObject(s.job, 1);
    }
    if (s.stop != nullptr) {
        SetEvent(s.stop);
    }
    for (std::jthread& thread : threads_) {
        if (thread.joinable()) {
            try {
                thread.join();
            } catch (...) {
                memory::fail_fast("WindowsChild: a thread's join failed");
            }
        }
    }
    for (void* handle : {s.process, s.job, s.input, s.changed, s.stop, s.out.pipe, s.err.pipe}) {
        if (handle != nullptr) {
            CloseHandle(handle);
        }
    }
}

ChildState& WindowsChild::state() const noexcept { return *static_cast<ChildState*>(pages_.address()); }

bool WindowsChild::faulted() noexcept {
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown = state().countdown;
    return countdown != nullptr && countdown->fail();
#else
    return false;
#endif
}

// The reason a thread ended the tree, thrown unless kill() came since.
void WindowsChild::check(std::string_view operation) const {
    ChildState& s = state();
    if (s.killed != 0) {
        return;
    }
    switch (static_cast<Reason>(shared(s.reason).load(std::memory_order_acquire))) {  // pairs with claim()
    case Reason::deadline:
        throw PlatformError(Code::deadline, operation, "the tree killed at its deadline");
    case Reason::out_cap:
        throw PlatformError(Code::output, operation, "out past its cap, the tree killed");
    case Reason::err_cap:
        throw PlatformError(Code::output, operation, "err past its cap, the tree killed");
    case Reason::none:
        break;
    }
}

// A wait for the next change until `until`: false past it.
bool WindowsChild::changed(Clock::time_point until, std::string_view operation) {
    const Clock::duration left = until - Clock::now();
    if (left <= Clock::duration::zero()) {
        return false;
    }
    const DWORD status = WaitForSingleObject(state().changed, milliseconds(left));
    if (status != WAIT_OBJECT_0 && status != WAIT_TIMEOUT) {
        failed(Code::io, operation, "WaitForSingleObject", GetLastError());
    }
    return true;
}

// out's next line with its LF, or its last bytes with none, copied before
// they count as given, so a failed allocation leaves them to the next
// call; false at the end; past the timeout deadline, the tree running.
bool WindowsChild::read_line(Seconds timeout, String& out) {
    out.clear();
    if (pages_.address() == nullptr) {
        memory::fail_fast("WindowsChild::read_line: a Child with no process");
    }
    ChildState& s = state();
    const Clock::time_point until = after(timeout);
    for (;;) {
        // The end first: its acquire shows the reason and the last length.
        const bool ended = shared(s.out.ended).load(std::memory_order_acquire) != 0;
        check("read_line");
        const std::size_t length = shared(s.out.length).load(std::memory_order_acquire);
        const std::string_view unread = std::string_view(s.out.data, length).substr(s.given);
        const std::size_t end = unread.find('\n');
        if (end != std::string_view::npos || (ended && !unread.empty())) {
            const std::string_view line = unread.substr(0, end == std::string_view::npos ? end : end + 1);
            out.assign(line.data(), line.size());
            s.given += line.size();
            return true;
        }
        if (ended) {
            if (s.out.error != 0) {
                failed(Code::io, "read_line", "ReadFile", s.out.error);
            }
            return false;
        }
        if (!changed(until, "read_line")) {
            throw PlatformError(Code::deadline, "read_line", "no line within the timeout");
        }
    }
}

// The bytes into the child's input; a child that closed it drops the
// rest, as a pipe whose reader is gone (232, 109): io.
void WindowsChild::write(std::string_view bytes) {
    if (pages_.address() == nullptr) {
        memory::fail_fast("WindowsChild::write: a Child with no process");
    }
    ChildState& s = state();
    check("write");
    if (s.input == nullptr) {
        throw PlatformError(Code::io, "write", "the input is closed");
    }
    while (!bytes.empty()) {
        const auto chunk = static_cast<DWORD>(std::min(bytes.size(), write_chunk));
        DWORD written = 0;
        const bool fault = faulted();
        if (fault || WriteFile(s.input, bytes.data(), chunk, &written, nullptr) == 0) {
            const DWORD error = fault ? ERROR_GEN_FAILURE : GetLastError();
            if (error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE) {
                check("write");  // the tree ended by a thread, else the child's own close
                return;
            }
            failed(Code::io, "write", "WriteFile", error);
        }
        bytes.remove_prefix(written);
    }
}

void WindowsChild::close_input() {
    if (pages_.address() == nullptr) {
        return;
    }
    ChildState& s = state();
    if (s.input != nullptr) {
        CloseHandle(s.input);
        s.input = nullptr;
    }
}

// The exit code once the process ends within the timeout; past it
// deadline, the tree left running: io.
int WindowsChild::wait(Seconds timeout) {
    if (pages_.address() == nullptr) {
        memory::fail_fast("WindowsChild::wait: a Child with no process");
    }
    ChildState& s = state();
    check("wait");
    const bool fault = faulted();
    const DWORD status = fault ? WAIT_FAILED : WaitForSingleObject(s.process, milliseconds(timeout));
    if (status == WAIT_TIMEOUT) {
        throw PlatformError(Code::deadline, "wait", "the child runs past the timeout");
    }
    if (status != WAIT_OBJECT_0) {
        failed(Code::io, "wait", "WaitForSingleObject", fault ? ERROR_GEN_FAILURE : GetLastError());
    }
    check("wait");  // a thread's end of the tree before the process ended
    DWORD code = 0;
    const bool exit_fault = faulted();
    if (exit_fault || GetExitCodeProcess(s.process, &code) == 0) {
        failed(Code::io, "wait", "GetExitCodeProcess", exit_fault ? ERROR_GEN_FAILURE : GetLastError());
    }
    return static_cast<int>(code);
}

// The tree ended by its Job Object, its exit code 1; again, nothing more.
void WindowsChild::kill() {
    if (pages_.address() == nullptr || state().job == nullptr) {
        return;
    }
    state().killed = 1;
    const bool fault = faulted();
    if (fault || TerminateJobObject(state().job, 1) == 0) {
        failed(Code::io, "kill", "TerminateJobObject", fault ? ERROR_GEN_FAILURE : GetLastError());
    }
}

void WindowsChild::finish(std::string_view data, Ran& out) {
    ChildState& s = state();
    write(data);
    close_input();
    const Clock::time_point until = Clock::time_point(Clock::duration(s.deadline)) + grace;
    while (shared(s.out.ended).load(std::memory_order_acquire) == 0 ||  // pairs with each reader's release
           shared(s.err.ended).load(std::memory_order_acquire) == 0) {
        check("run");
        if (!changed(until, "run")) {
            throw PlatformError(Code::deadline, "run", "the streams open past the deadline");
        }
    }
    check("run");
    for (const ChildStream* stream : {&s.out, &s.err}) {
        if (stream->error != 0) {
            failed(Code::io, "run", "ReadFile", stream->error);
        }
    }
    out.code = wait(Seconds(until - Clock::now()));
    out.out.assign(s.out.data, shared(s.out.length).load(std::memory_order_acquire));
    out.err.assign(s.err.data, shared(s.err.length).load(std::memory_order_acquire));
}

// start, then the Child's rest; each PlatformError named run's.
void WindowsPlatform::run(const Command& command, std::string_view data, Ran& out) {
    try {
        WindowsChild child = start(command);
        child.finish(data, out);
    } catch (const PlatformError& error) {
        throw PlatformError(error.code, "run", error.detail.view());
    }
}

}  // namespace qiven3::platform::win
