#include "diag/out.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#ifdef _WIN32
#include <windows.h>

#include <array>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#endif

namespace qiven3::diag {

Out Out::from_environment() noexcept {
    Out out;
#ifdef _WIN32
    std::array<wchar_t, 1024> name{};
    const DWORD got = GetEnvironmentVariableW(L"QIVEN3_LOG", name.data(), static_cast<DWORD>(name.size()));
    const std::wstring_view value(name.data(), got < name.size() ? got : 0);
    if (got >= name.size()) {
        out.failed_ = true;
    } else if (value == L"-") {
        out.handle_ = GetStdHandle(STD_ERROR_HANDLE);
    } else if (!value.empty()) {
        HANDLE file = CreateFileW(name.data(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        out.owned_ = file != INVALID_HANDLE_VALUE;
        out.failed_ = !out.owned_;
        out.handle_ = out.owned_ ? file : nullptr;
    }
#else
    const char* name = std::getenv("QIVEN3_LOG");
    const std::string_view value = name != nullptr ? std::string_view(name) : std::string_view();
    if (value == "-") {
        out.handle_ = STDERR_FILENO;
    } else if (!value.empty()) {
        out.handle_ = ::open(name, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        out.owned_ = out.handle_ >= 0;
        out.failed_ = !out.owned_;
    }
#endif
    return out;
}

Out::Out(Out&& other) noexcept
    : handle_(std::exchange(other.handle_, none)), owned_(std::exchange(other.owned_, false)),
      failed_(other.failed_) {}

Out& Out::operator=(Out&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = std::exchange(other.handle_, none);
        owned_ = std::exchange(other.owned_, false);
        failed_ = other.failed_;
    }
    return *this;
}

Out::~Out() { close(); }

void Out::close() noexcept {
    if (owned_) {
#ifdef _WIN32
        CloseHandle(handle_);
#else
        ::close(handle_);
#endif
        owned_ = false;
    }
}

bool Out::write(std::string_view bytes) noexcept {
#ifdef _WIN32
    const bool unset = handle_ == nullptr;
#else
    const bool unset = handle_ < 0;
#endif
    if (unset || failed_) {
        return !failed_;
    }
    while (!bytes.empty()) {
#ifdef _WIN32
        DWORD wrote = 0;
        const auto size = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1U << 20U));
        const bool ok = WriteFile(handle_, bytes.data(), size, &wrote, nullptr) != FALSE && wrote != 0;
#else
        const ssize_t wrote = ::write(handle_, bytes.data(), bytes.size());
        const bool ok = wrote > 0;
#endif
        if (!ok) {
            failed_ = true;
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(wrote));
    }
    return true;
}

}  // namespace qiven3::diag
