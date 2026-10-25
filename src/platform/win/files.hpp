#pragma once

// win/files: the Windows adapter's file calls. WindowsFiles is the System
// that platform/files.hpp's retries call for replace and lock, each giving
// its Win32 error by number, 0 for none, never throwing; beside them
// create, read and remove_directory, which throw PlatformError as the
// contract states. stage's calls are not built yet: each gives error 50
// (ERROR_NOT_SUPPORTED), and the adapter's stage answers unsupported.
//
// It lives in its adapter, whose scratch it borrows for a path's UTF-16
// forms, and takes no memory but read's out on the caller's arena. Under
// QIVEN3_FAULTS each Win32 call asks the countdown first and, when it
// fires, fails as the call would with error 31 (ERROR_GEN_FAILURE), a
// handle it opened closed first.
//
// thread: one (the adapter's)

#include <chrono>
#include <cstddef>
#include <span>
#include <string_view>

#include "platform/files.hpp"
#include "platform/platform.hpp"

namespace qiven3::platform::win {

class WindowsPlatform;

class WindowsFiles {
public:
    // The system's handles, as void*: a HANDLE, null for none.
    struct File {
        void* handle = nullptr;
    };
    struct Dir {
        std::size_t id = 0;
    };
    struct Held {
        void* handle = nullptr;
    };

    explicit WindowsFiles(WindowsPlatform& platform) noexcept : platform_(&platform) {}
    WindowsFiles(const WindowsFiles&) = delete;
    WindowsFiles& operator=(const WindowsFiles&) = delete;
    WindowsFiles(WindowsFiles&&) = delete;
    WindowsFiles& operator=(WindowsFiles&&) = delete;
    ~WindowsFiles() = default;

    // The adapter's random bytes; a monotonic clock; Sleep; v3's absolute
    // rule on the path's first three UTF-16 units.
    void random_bytes(std::span<std::byte> into);
    [[nodiscard]] std::chrono::milliseconds elapsed() const noexcept;
    void pause(std::chrono::milliseconds wait) noexcept;
    [[nodiscard]] bool absolute(std::string_view path) const noexcept;

    // replace's: ".<name>.<digits>.tmp" beside path, made by CreateFileW
    // (CREATE_NEW), written, flushed and closed; renamed over path by
    // MoveFileExW (MOVEFILE_REPLACE_EXISTING); deleted by DeleteFileW.
    files::Error write_temporary(std::string_view path, const files::Digits& digits, std::string_view data);
    files::Error rename_temporary(std::string_view path, const files::Digits& digits);
    files::Error delete_temporary(std::string_view path, const files::Digits& digits);

    // stage's: not built yet, error 50 each; close and discard none.
    files::Error make_staged(std::string_view directory, const files::Digits& digits, Dir& out);
    files::Error open_staged(const Dir& dir, std::string_view path, File& out);
    files::Error write(const File& file, std::string_view bytes);
    files::Error close(File& file) noexcept;
    files::Error close_all(const Dir& dir);
    files::Error publish(const Dir& dir, std::string_view name);
    files::Error discard(const Dir& dir, Detail& left) noexcept;
    void forget(Dir& dir) noexcept;

    // lock's: CreateFileW (OPEN_ALWAYS, sharing read and write); LockFileEx
    // of the first byte, exclusive, failing at once with 33; UnlockFileEx
    // and CloseHandle, the handle nulled.
    files::Error open_lock(std::string_view path, Held& out);
    files::Error try_lock(const Held& held);
    void unlock(Held& held) noexcept;

    // create: an empty file (CreateFileW, CREATE_NEW) or a directory
    // (CreateDirectoryW), only if absent: path, exists, io.
    void create(std::string_view path, bool directory);
    // RemoveDirectoryW: true when removed or none there (2, 3); false for
    // 145, the directory kept; another error io, a file there (267) among
    // them: path, io.
    bool remove_directory(std::string_view path);
    // CreateFileW (OPEN_EXISTING, sharing read and write), GetFileSizeEx,
    // ReadFile until the size or the end: the bytes into out, one
    // allocation of the caller's arena; any error io: path, io.
    void read(std::string_view path, String& out);

#if defined(QIVEN3_FAULTS)
    void attach(memory::Countdown* countdown) noexcept { countdown_ = countdown; }
#endif

private:
    // Whether the countdown fires: the call then fails with 31.
    bool faulted() noexcept;
    // The adapter's two path buffers.
    [[nodiscard]] std::span<wchar_t> first() const noexcept;
    [[nodiscard]] std::span<wchar_t> second() const noexcept;
    // path's UTF-16 in first(), for create, read and remove_directory:
    // text_fault's, then the conversion's, then not absolute: path.
    std::size_t checked(std::string_view operation, std::string_view path);
    // The temporary file's UTF-16 in second(), path's in first(): 0, or the
    // conversion's error.
    unsigned long temporary(std::string_view path, const files::Digits& digits);

    WindowsPlatform* platform_;
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown_ = nullptr;
#endif
};

}  // namespace qiven3::platform::win
