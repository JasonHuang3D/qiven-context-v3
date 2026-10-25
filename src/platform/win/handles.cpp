// The Windows adapter's handles (platform/win/windows.hpp): Pages over
// VirtualAlloc, and the processes' handle, whose operations are its
// module's (win/process) and PlatformError unsupported until then. The
// Lock, Staged and Sink are files' (platform/win/files.hpp).

#include "platform/win/windows.hpp"

#include <windows.h>

#include <string_view>
#include <utility>

namespace qiven3::platform::win {
namespace {

[[noreturn]] void not_yet(std::string_view operation) {
    throw PlatformError(Code::unsupported, operation, "not implemented by the Windows adapter yet");
}

}  // namespace

WindowsPages::WindowsPages(WindowsPages&& other) noexcept
    : address_(std::exchange(other.address_, nullptr)), size_(std::exchange(other.size_, 0)) {}

WindowsPages& WindowsPages::operator=(WindowsPages&& other) noexcept {
    if (this != &other) {
        WindowsPages old(std::move(*this));
        address_ = std::exchange(other.address_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

WindowsPages::~WindowsPages() {
    if (address_ != nullptr) {
        VirtualFree(address_, 0, MEM_RELEASE);
    }
}

std::span<std::byte> WindowsPages::bytes() const noexcept {
    return std::span<std::byte>(static_cast<std::byte*>(address_), size_);
}

WindowsChild::WindowsChild(WindowsChild&& other) noexcept
    : process_(std::exchange(other.process_, nullptr)), job_(std::exchange(other.job_, nullptr)),
      input_(std::exchange(other.input_, nullptr)), output_(std::exchange(other.output_, nullptr)),
      error_(std::exchange(other.error_, nullptr)) {}

WindowsChild& WindowsChild::operator=(WindowsChild&& other) noexcept {
    if (this != &other) {
        WindowsChild old(std::move(*this));
        process_ = std::exchange(other.process_, nullptr);
        job_ = std::exchange(other.job_, nullptr);
        input_ = std::exchange(other.input_, nullptr);
        output_ = std::exchange(other.output_, nullptr);
        error_ = std::exchange(other.error_, nullptr);
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

bool WindowsChild::read_line(Seconds, String& out) {
    out.clear();
    not_yet("read_line");
}

void WindowsChild::write(std::string_view) { not_yet("write"); }

void WindowsChild::close_input() { not_yet("close_input"); }

int WindowsChild::wait(Seconds) { not_yet("wait"); }

void WindowsChild::kill() { not_yet("kill"); }

}  // namespace qiven3::platform::win
