// win/files (platform/win/files.hpp): the Windows adapter's file calls.

#include "platform/win/files.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <limits>
#include <string_view>

#include "platform/win/paths.hpp"
#include "platform/win/windows.hpp"

namespace qiven3::platform::win {
namespace {

using paths::units;

[[noreturn]] void failed(Code code, std::string_view operation, std::string_view call, unsigned long error) {
    Detail detail(call);
    detail.add(": ");
    add_error(detail, error);
    throw PlatformError(code, operation, detail);
}

// A handle closed at the scope's end.
class Opened {
public:
    explicit Opened(void* handle) noexcept : handle_(handle) {}
    Opened(const Opened&) = delete;
    Opened& operator=(const Opened&) = delete;
    Opened(Opened&&) = delete;
    Opened& operator=(Opened&&) = delete;
    ~Opened() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }

private:
    void* handle_;
};

}  // namespace

// UTF-8 into `into`, strictly, ended by a null: its length, or 0 with
// error the conversion's (1113), or 206 past the buffer. An empty text is
// 0 with error 123 (ERROR_INVALID_NAME).
std::size_t WindowsFiles::to_wide(std::string_view text, std::span<wchar_t> into, DWORD& error) noexcept {
    into[0] = L'\0';
    error = ERROR_INVALID_NAME;
    if (text.empty()) {
        return 0;
    }
    if (text.size() >= into.size() * 3) {
        error = ERROR_FILENAME_EXCED_RANGE;
        return 0;
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) {
        error = GetLastError();
        return 0;
    }
    const auto length = static_cast<std::size_t>(n);
    if (length >= into.size()) {
        error = ERROR_FILENAME_EXCED_RANGE;
        return 0;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), into.data(), n);
    into[length] = L'\0';
    error = 0;
    return length;
}

// The handle closed, if open, and nulled.
void WindowsFiles::shut(void*& handle) noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

bool WindowsFiles::faulted() noexcept {
#if defined(QIVEN3_FAULTS)
    return countdown_ != nullptr && countdown_->fail();
#else
    return false;
#endif
}

std::span<wchar_t> WindowsFiles::first() const noexcept { return paths::scratch(platform_->scratch_).first(units); }

std::span<wchar_t> WindowsFiles::second() const noexcept {
    return paths::scratch(platform_->scratch_).subspan(units, units);
}

void WindowsFiles::random_bytes(std::span<std::byte> into) {
    if (faulted()) {
        failed(Code::io, "random_bytes", "BCryptGenRandom", ERROR_GEN_FAILURE);
    }
    platform_->random_bytes(into);
}

std::chrono::milliseconds WindowsFiles::elapsed() const noexcept {
    const auto read =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch());
#if defined(QIVEN3_FAULTS)
    if (trace_ != nullptr) {
        trace_->first = trace_->reads == 0 ? read : trace_->first;
        trace_->previous = trace_->reads == 0 ? read : trace_->last;
        trace_->last = read;
        ++trace_->reads;
    }
#endif
    return read;
}

void WindowsFiles::pause(std::chrono::milliseconds wait) noexcept {
#if defined(QIVEN3_FAULTS)
    if (trace_ != nullptr) {
        ++trace_->pauses;
        trace_->asked += wait;
    }
#endif
    // Below DWORD's largest value, which Sleep takes as no deadline.
    constexpr std::chrono::milliseconds::rep longest = std::numeric_limits<DWORD>::max() - 1;
    const auto count = std::clamp<std::chrono::milliseconds::rep>(wait.count(), 0, longest);
    Sleep(static_cast<DWORD>(count));
}

bool WindowsFiles::absolute(std::string_view path) const noexcept {
    // The first three code points at most: 12 bytes, cut back to a code
    // point's start, give the first three units.
    std::size_t cut = std::min<std::size_t>(path.size(), 12);
    while (cut > 0 && cut < path.size() && (static_cast<unsigned char>(path[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    std::array<wchar_t, 16> head{};
    DWORD error = 0;
    const std::size_t n = to_wide(path.substr(0, cut), head, error);
    return n > 0 && paths::absolute(std::span<const wchar_t>(head.data(), n));
}

std::size_t WindowsFiles::checked(std::string_view operation, std::string_view path) {
    files::refuse(operation, files::text_fault(path));
    DWORD error = 0;
    const std::size_t n = to_wide(path, first(), error);
    if (n == 0) {
        failed(Code::path, operation, "MultiByteToWideChar", error);
    }
    if (!paths::absolute(first().first(n))) {
        throw PlatformError(Code::path, operation, "not absolute");
    }
    return n;
}

unsigned long WindowsFiles::temporary(std::string_view path, const files::Digits& digits) {
    const std::span<wchar_t> a = first();
    const std::span<wchar_t> b = second();
    DWORD error = 0;
    const std::size_t n = to_wide(path, a, error);
    if (n == 0) {
        return error;
    }
    // The head through the last separator, then ".<name>.<digits>.tmp".
    std::size_t name = n;
    while (name > 0 && a[name - 1] != L'\\' && a[name - 1] != L'/') {
        --name;
    }
    constexpr std::size_t extra = 1 + 1 + std::tuple_size_v<files::Digits> + 4;
    if (n + extra >= units) {
        return ERROR_FILENAME_EXCED_RANGE;
    }
    std::size_t at = 0;
    const auto put = [&](wchar_t c) { b[at++] = c; };
    std::for_each(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(name), put);
    put(L'.');
    std::for_each(a.begin() + static_cast<std::ptrdiff_t>(name), a.begin() + static_cast<std::ptrdiff_t>(n), put);
    put(L'.');
    for (const char c : digits) {
        put(static_cast<wchar_t>(c));
    }
    for (const wchar_t c : std::wstring_view(L".tmp")) {
        put(c);
    }
    b[at] = L'\0';
    return 0;
}

// --- replace ---

files::Error WindowsFiles::write_temporary(std::string_view path, const files::Digits& digits, std::string_view data) {
    const DWORD composed = temporary(path, digits);
    if (composed != 0) {
        return composed;
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    void* file = CreateFileW(second().data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    DWORD error = 0;
    for (std::size_t at = 0; at < data.size() && error == 0;) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - at, 1U << 30U));
        DWORD wrote = 0;
        if (faulted()) {
            error = ERROR_GEN_FAILURE;
        } else if (WriteFile(file, data.substr(at).data(), chunk, &wrote, nullptr) == FALSE) {
            error = GetLastError();
        } else if (wrote == 0) {
            error = ERROR_WRITE_FAULT;
        }
        at += wrote;
    }
    if (error == 0) {
        error = faulted() ? ERROR_GEN_FAILURE : FlushFileBuffers(file) == FALSE ? GetLastError() : 0;
    }
    if (faulted()) {
        shut(file);
        return error != 0 ? error : ERROR_GEN_FAILURE;
    }
    const BOOL closed = CloseHandle(file);
    return error != 0 ? error : closed == FALSE ? GetLastError() : 0;
}

files::Error WindowsFiles::rename_temporary(std::string_view path, const files::Digits& digits) {
    const DWORD composed = temporary(path, digits);
    if (composed != 0) {
        return composed;
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    return MoveFileExW(second().data(), first().data(), MOVEFILE_REPLACE_EXISTING) == FALSE ? GetLastError() : 0;
}

files::Error WindowsFiles::delete_temporary(std::string_view path, const files::Digits& digits) {
    const DWORD composed = temporary(path, digits);
    if (composed != 0) {
        return composed;
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    return DeleteFileW(second().data()) == FALSE ? GetLastError() : 0;
}

// --- stage: stage.cpp ---

// --- lock ---

files::Error WindowsFiles::open_lock(std::string_view path, Held& out) {
    DWORD error = 0;
    if (to_wide(path, first(), error) == 0) {
        return error;
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    void* file = CreateFileW(first().data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    out.handle = file;
    return 0;
}

files::Error WindowsFiles::try_lock(const Held& held) {
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    OVERLAPPED at{};
    return LockFileEx(held.handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at) == FALSE
               ? GetLastError()
               : 0;
}

void WindowsFiles::unlock(Held& held) noexcept {
    if (held.handle != nullptr) {
        OVERLAPPED at{};
        UnlockFileEx(held.handle, 0, 1, 0, &at);  // 158 when it was not locked: nothing to undo
        shut(held.handle);
    }
}

// --- create, remove_directory, read ---

void WindowsFiles::create(std::string_view path, bool directory) {
    checked("create", path);
    const std::span<wchar_t> a = first();
    if (directory) {
        if (faulted()) {
            failed(Code::io, "create", "CreateDirectoryW", ERROR_GEN_FAILURE);
        }
        if (CreateDirectoryW(a.data(), nullptr) == FALSE) {
            const DWORD error = GetLastError();
            failed(error == ERROR_ALREADY_EXISTS ? Code::exists : Code::io, "create", "CreateDirectoryW", error);
        }
        return;
    }
    if (faulted()) {
        failed(Code::io, "create", "CreateFileW", ERROR_GEN_FAILURE);
    }
    void* file = CreateFileW(a.data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        // A present directory gives 5, as a present file gives 80.
        const DWORD attributes = error == ERROR_ACCESS_DENIED ? GetFileAttributesW(a.data()) : INVALID_FILE_ATTRIBUTES;
        const bool present = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS ||
                             (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        failed(present ? Code::exists : Code::io, "create", "CreateFileW", error);
    }
    // Closed unchecked, no fault point: the file is made, and a failed
    // create makes nothing.
    shut(file);
}

bool WindowsFiles::remove_directory(std::string_view path) {
    checked("remove_directory", path);
    if (faulted()) {
        failed(Code::io, "remove_directory", "RemoveDirectoryW", ERROR_GEN_FAILURE);
    }
    if (RemoveDirectoryW(first().data()) != FALSE) {
        return true;
    }
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    if (error == ERROR_DIR_NOT_EMPTY) {
        return false;
    }
    failed(Code::io, "remove_directory", "RemoveDirectoryW", error);
}

void WindowsFiles::read(std::string_view path, String& out) {
    out.clear();
    checked("read", path);
    if (faulted()) {
        failed(Code::io, "read", "CreateFileW", ERROR_GEN_FAILURE);
    }
    void* file = CreateFileW(first().data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        failed(Code::io, "read", "CreateFileW", GetLastError());
    }
    const Opened opened(file);  // closed on every way out, an allocation's MemoryError among them
    LARGE_INTEGER size{};
    if (faulted()) {
        failed(Code::io, "read", "GetFileSizeEx", ERROR_GEN_FAILURE);
    }
    if (GetFileSizeEx(file, &size) == FALSE) {
        failed(Code::io, "read", "GetFileSizeEx", GetLastError());
    }
    if (size.QuadPart < 0 || static_cast<std::uint64_t>(size.QuadPart) > out.max_size()) {
        failed(Code::io, "read", "GetFileSizeEx", ERROR_FILE_TOO_LARGE);
    }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t at = 0;
    while (at < out.size()) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(out.size() - at, 1U << 30U));
        DWORD got = 0;
        const bool fault = faulted();
        const BOOL done = fault ? FALSE : ReadFile(file, std::span(out).subspan(at).data(), chunk, &got, nullptr);
        if (done == FALSE) {
            const DWORD error = fault ? ERROR_GEN_FAILURE : GetLastError();
            out.clear();
            failed(Code::io, "read", "ReadFile", error);
        }
        if (got == 0) {
            break;  // the end, come early: the file shrank since its size was read
        }
        at += got;
    }
    out.resize(at);
}

}  // namespace qiven3::platform::win
