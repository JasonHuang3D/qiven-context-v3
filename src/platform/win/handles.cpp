// The Windows adapter's handles (platform/win/windows.hpp): Pages over
// VirtualAlloc. The Child is win/process' (process.cpp); the Lock, Staged
// and Sink are files' (platform/win/files.hpp).

#include "platform/win/windows.hpp"

#include <windows.h>

#include <utility>

namespace qiven3::platform::win {
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

}  // namespace qiven3::platform::win
