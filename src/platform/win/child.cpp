// win/process' Child (platform/win/windows.hpp): a started process tree
// in its Job Object, the process, the job and three pipes, which
// process.cpp's start makes. write, close_input, wait and kill; the
// destructor ends the tree by its job. Each Win32 call asks the countdown
// start gave it first and, when it fires, fails as the call would with
// error 31 (ERROR_GEN_FAILURE).
//
// thread: one (a Child serves one thread at a time)

#include "platform/win/windows.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string_view>
#include <utility>

#include "memory/fail.hpp"

namespace qiven3::platform::win {
namespace {

// A write's largest chunk.
constexpr std::size_t write_chunk = std::size_t{1} << 20U;

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

}  // namespace

WindowsChild::WindowsChild(void* process, void* job, void* input, void* output, void* error) noexcept
    : process_(process), job_(job), input_(input), output_(output), error_(error) {}

WindowsChild::WindowsChild(WindowsChild&& other) noexcept
    : process_(std::exchange(other.process_, nullptr)), job_(std::exchange(other.job_, nullptr)),
      input_(std::exchange(other.input_, nullptr)), output_(std::exchange(other.output_, nullptr)),
      error_(std::exchange(other.error_, nullptr))
#if defined(QIVEN3_FAULTS)
      ,
      countdown_(std::exchange(other.countdown_, nullptr))
#endif
{
}

WindowsChild& WindowsChild::operator=(WindowsChild&& other) noexcept {
    if (this != &other) {
        WindowsChild old(std::move(*this));
        process_ = std::exchange(other.process_, nullptr);
        job_ = std::exchange(other.job_, nullptr);
        input_ = std::exchange(other.input_, nullptr);
        output_ = std::exchange(other.output_, nullptr);
        error_ = std::exchange(other.error_, nullptr);
#if defined(QIVEN3_FAULTS)
        countdown_ = std::exchange(other.countdown_, nullptr);
#endif
    }
    return *this;
}

void WindowsChild::close_all() noexcept {
    for (void** handle : {&process_, &job_, &input_, &output_, &error_}) {
        if (*handle != nullptr) {
            CloseHandle(*handle);
            *handle = nullptr;
        }
    }
}

// The tree ends with its Job Object.
WindowsChild::~WindowsChild() {
    if (job_ != nullptr) {
        TerminateJobObject(job_, 1);
    }
    close_all();
}

bool WindowsChild::faulted() noexcept {
#if defined(QIVEN3_FAULTS)
    return countdown_ != nullptr && countdown_->fail();
#else
    return false;
#endif
}

bool WindowsChild::read_line(Seconds, String& out) {
    out.clear();
    throw PlatformError(Code::unsupported, "read_line", "not implemented by the Windows adapter yet");
}

// The bytes into the child's input; a child that closed it drops the
// rest, as a pipe whose reader is gone (232, 109): io.
void WindowsChild::write(std::string_view bytes) {
    if (process_ == nullptr) {
        memory::fail_fast("WindowsChild::write: a Child with no process");
    }
    if (input_ == nullptr) {
        throw PlatformError(Code::io, "write", "the input is closed");
    }
    while (!bytes.empty()) {
        const auto chunk = static_cast<DWORD>(std::min(bytes.size(), write_chunk));
        DWORD written = 0;
        const bool fault = faulted();
        if (fault || WriteFile(input_, bytes.data(), chunk, &written, nullptr) == 0) {
            const DWORD error = fault ? ERROR_GEN_FAILURE : GetLastError();
            if (error == ERROR_NO_DATA || error == ERROR_BROKEN_PIPE) {
                return;
            }
            failed(Code::io, "write", "WriteFile", error);
        }
        bytes.remove_prefix(written);
    }
}

void WindowsChild::close_input() {
    if (input_ != nullptr) {
        CloseHandle(input_);
        input_ = nullptr;
    }
}

// The exit code once the process ends within the timeout; past it
// deadline, the tree left running: io.
int WindowsChild::wait(Seconds timeout) {
    if (process_ == nullptr) {
        memory::fail_fast("WindowsChild::wait: a Child with no process");
    }
    const bool fault = faulted();
    const DWORD status = fault ? WAIT_FAILED : WaitForSingleObject(process_, milliseconds(timeout));
    if (status == WAIT_TIMEOUT) {
        throw PlatformError(Code::deadline, "wait", "the child runs past the timeout");
    }
    if (status != WAIT_OBJECT_0) {
        failed(Code::io, "wait", "WaitForSingleObject", fault ? ERROR_GEN_FAILURE : GetLastError());
    }
    DWORD code = 0;
    const bool exit_fault = faulted();
    if (exit_fault || GetExitCodeProcess(process_, &code) == 0) {
        failed(Code::io, "wait", "GetExitCodeProcess", exit_fault ? ERROR_GEN_FAILURE : GetLastError());
    }
    return static_cast<int>(code);
}

// The tree ended by its Job Object, its exit code 1; again, nothing more.
void WindowsChild::kill() {
    if (job_ == nullptr) {
        return;
    }
    const bool fault = faulted();
    if (fault || TerminateJobObject(job_, 1) == 0) {
        failed(Code::io, "kill", "TerminateJobObject", fault ? ERROR_GEN_FAILURE : GetLastError());
    }
}

}  // namespace qiven3::platform::win
