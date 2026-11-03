// The Windows adapter's handles (qiven/platform/windows/windows.hpp): Pages over
// VirtualAlloc. The Child is windows/process' (process.cpp); the Lock, Staged
// and Sink are files' (qiven/platform/windows/files.hpp).

#include "qiven/platform/windows/windows.hpp"

#include <windows.h>

#include <utility>

namespace qiven::platform::win {
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

}  // namespace qiven::platform::win
