#pragma once

// win/files: the Windows adapter's file calls. WindowsFiles is the System
// that platform/files.hpp's retries call for replace, stage and lock, each
// giving its Win32 error by number, 0 for none, never throwing but
// make_staged's pages; beside them create, read and remove_directory,
// which throw PlatformError as the contract states. Its stage members are
// in stage.cpp.
//
// It lives in its adapter, whose scratch it borrows for a path's UTF-16
// forms, and takes no memory but read's out on the caller's arena and a
// staging directory's pages. Under QIVEN3_FAULTS each Win32 call asks the
// countdown first and, when it fires, fails as the call would with error
// 31 (ERROR_GEN_FAILURE), a handle it opened closed first.
//
// thread: one (the adapter's)

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "platform/files.hpp"
#include "platform/platform.hpp"

namespace qiven3::platform::win {

class WindowsPlatform;

// Pages from VirtualAlloc, reserved and committed at once; the destructor
// gives them back by VirtualFree. Empty for a count of 0. Here, before
// WindowsFiles, as a staging directory holds its path on them.
class WindowsPages {
public:
    WindowsPages() noexcept = default;
    WindowsPages(void* address, std::size_t size) noexcept : address_(address), size_(size) {}
    WindowsPages(WindowsPages&& other) noexcept;
    WindowsPages& operator=(WindowsPages&& other) noexcept;
    WindowsPages(const WindowsPages&) = delete;
    WindowsPages& operator=(const WindowsPages&) = delete;
    ~WindowsPages();

    [[nodiscard]] std::span<std::byte> bytes() const noexcept;
    // The pages' first byte, as VirtualAlloc gave it.
    [[nodiscard]] void* address() const noexcept { return address_; }

private:
    void* address_ = nullptr;
    std::size_t size_ = 0;
};

class WindowsFiles {
public:
    // A file open in a staging directory: its serial, which its slot
    // records while it is open; 0 none. A serial is never reused, so a
    // file closed by close_all or discard finds no slot: a write gives 6
    // (ERROR_INVALID_HANDLE), a close none.
    struct File {
        std::uint64_t serial = 0;
    };
    // A staging directory: its id, a serial never reused, 0 none; its
    // path's `units` UTF-16 units and a null on pages taken at make_staged.
    struct Dir {
        std::uint64_t id = 0;
        WindowsPages path;
        std::size_t units = 0;
    };
    // The system's handle, as void*: a HANDLE, null for none.
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

    // stage's (stage.cpp). make_staged: the path ".<digits>.tmp" in
    // directory on pages (PlatformError memory if the system refuses
    // them), then CreateDirectoryW. open_staged: each parent by
    // CreateDirectoryW (183 passes), then CreateFileW (CREATE_NEW, no
    // sharing) into a free slot. write: WriteFile; close: FlushFileBuffers
    // and CloseHandle, the slot freed whatever they give, the first error
    // kept; close_all: close for each of the directory's. publish:
    // MoveFileExW with no flag to name beside the directory. discard: its
    // files closed unchecked, then its tree deleted without recursion, one
    // FindFirstFileW handle at a time, stopping at the first failure, which
    // left names by its path inside with '/'. forget: the directory's
    // slots closed unchecked and its pages given back.
    files::Error make_staged(std::string_view directory, const files::Digits& digits, Dir& out);
    files::Error open_staged(const Dir& dir, std::string_view path, File& out);
    files::Error write(const File& file, std::string_view bytes);
    files::Error close(File& file) noexcept;
    files::Error close_all(const Dir& dir);
    // The slots held.
    [[nodiscard]] std::size_t open_files() const noexcept;
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
    // The countdown, which a started Child's calls ask too.
    [[nodiscard]] memory::Countdown* countdown() const noexcept { return countdown_; }
#endif

    // Whether the countdown fires: the call then fails with 31. win/facts'
    // calls ask it too.
    bool faulted() noexcept;
    // path's UTF-16 in the adapter's first path buffer, for create, read,
    // remove_directory and win/facts' members: text_fault's, then the
    // conversion's, then not absolute: path. Its length.
    std::size_t checked(std::string_view operation, std::string_view path);

private:
    // An open file of a staging directory: its handle, its directory's id
    // and its serial; all 0 when free. files' bound refuses an open at
    // open_bound held, before any slot is sought, so a full table is never
    // reached by a System serving one thread at a time.
    struct Slot {
        void* handle = nullptr;
        std::uint64_t dir = 0;
        std::uint64_t file = 0;
    };

    // The adapter's two path buffers.
    [[nodiscard]] std::span<wchar_t> first() const noexcept;
    [[nodiscard]] std::span<wchar_t> second() const noexcept;
    // The temporary file's UTF-16 in second(), path's in first(): 0, or the
    // conversion's error.
    unsigned long temporary(std::string_view path, const files::Digits& digits);
    // The slot holding file, or null.
    Slot* held(std::uint64_t file) noexcept;
    // The slot's file flushed and closed, the slot freed: the first error.
    files::Error shut_slot(Slot& slot) noexcept;
    // The directory's tree deleted from its path in first(): discard's.
    files::Error walk(const Dir& dir, Detail& left) noexcept;

    // UTF-8 into `into`, strictly, ended by a null: its length, or 0 with
    // error the conversion's (1113), or 206 past the buffer. An empty text
    // is 0 with error 123 (ERROR_INVALID_NAME).
    static std::size_t to_wide(std::string_view text, std::span<wchar_t> into, unsigned long& error) noexcept;
    // The handle closed, if open, and nulled.
    static void shut(void*& handle) noexcept;

    WindowsPlatform* platform_;
#if defined(QIVEN3_FAULTS)
    memory::Countdown* countdown_ = nullptr;
#endif
    std::array<Slot, files::open_bound> slots_{};
    std::uint64_t serial_ = 0;  // the last File's or Dir's
};

}  // namespace qiven3::platform::win
