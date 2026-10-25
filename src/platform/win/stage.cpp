// win/files (platform/win/files.hpp): stage's calls, a staging
// directory's files written as streams, published by one rename or
// deleted. A Dir holds its path on its own pages; an open file holds one
// of WindowsFiles' slots, so close_all and discard find a directory's
// files with no memory taken. Each Win32 call is a fault point but the
// unchecked closes of discard and forget and FindClose, which free what
// is given up anyway.

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>

#include "platform/win/files.hpp"
#include "platform/win/paths.hpp"
#include "platform/win/windows.hpp"

namespace qiven3::platform::win {
namespace {

using paths::units;

// ".<digits>.tmp": the staging directory's name, after its parent's path
// and a separator.
constexpr std::size_t name_units = 1 + std::tuple_size_v<files::Digits> + 4;

// The Dir's path and its null, on its pages.
std::span<const wchar_t> path_of(const WindowsFiles::Dir& dir) noexcept {
    return std::span<const wchar_t>(static_cast<const wchar_t*>(dir.path.address()), dir.units + 1);
}

// The path's units at [from, to) as UTF-8 into left, each '\' as '/'; the
// path's units changed in place.
void name_into(Detail& left, std::span<wchar_t> path, std::size_t from, std::size_t to) noexcept {
    if (to <= from) {
        return;  // the staging directory itself: files names it
    }
    std::replace(path.begin() + static_cast<std::ptrdiff_t>(from), path.begin() + static_cast<std::ptrdiff_t>(to),
                 L'\\', L'/');
    // In pieces of at most 64 units, 192 bytes, a surrogate pair kept
    // whole; a lone surrogate becomes U+FFFD. Detail keeps what fits.
    std::array<char, 200> bytes{};
    for (std::size_t at = from; at < to;) {
        std::size_t n = std::min<std::size_t>(to - at, 64);
        if (at + n < to && path[at + n - 1] >= 0xD800 && path[at + n - 1] <= 0xDBFF) {
            --n;
        }
        const int made = WideCharToMultiByte(CP_UTF8, 0, path.subspan(at).data(), static_cast<int>(n), bytes.data(),
                                             static_cast<int>(bytes.size()), nullptr, nullptr);
        if (made <= 0) {
            return;
        }
        left.add(std::string_view(bytes.data(), static_cast<std::size_t>(made)));
        at += n;
    }
}

}  // namespace

WindowsFiles::Slot* WindowsFiles::held(std::uint64_t file) noexcept {
    for (Slot& slot : slots_) {
        if (file != 0 && slot.file == file) {
            return &slot;
        }
    }
    return nullptr;
}

std::size_t WindowsFiles::open_files() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.file != 0; }));
}

files::Error WindowsFiles::shut_slot(Slot& slot) noexcept {
    DWORD error = faulted() ? ERROR_GEN_FAILURE : FlushFileBuffers(slot.handle) == FALSE ? GetLastError() : 0;
    if (faulted()) {
        shut(slot.handle);
        error = error != 0 ? error : ERROR_GEN_FAILURE;
    } else {
        const BOOL closed = CloseHandle(slot.handle);
        error = error != 0 ? error : closed == FALSE ? GetLastError() : 0;
    }
    slot = Slot{};
    return error;
}

files::Error WindowsFiles::make_staged(std::string_view directory, const files::Digits& digits, Dir& out) {
    const std::span<wchar_t> a = first();
    DWORD error = 0;
    std::size_t n = to_wide(directory, a, error);
    if (n == 0) {
        return error;
    }
    // The directory, a separator unless it ends in one, ".<digits>.tmp".
    const bool separated = a[n - 1] == L'\\' || a[n - 1] == L'/';
    if (n + 1 + name_units >= units) {
        return ERROR_FILENAME_EXCED_RANGE;
    }
    if (!separated) {
        a[n++] = L'\\';
    }
    a[n++] = L'.';
    for (const char c : digits) {
        a[n++] = static_cast<wchar_t>(c);
    }
    for (const wchar_t c : std::wstring_view(L".tmp")) {
        a[n++] = c;
    }
    a[n] = L'\0';
    if (faulted()) {
        // The pages' VirtualAlloc refused, as take_pages refuses: memory.
        Detail detail("VirtualAlloc: ");
        add_error(detail, ERROR_GEN_FAILURE);
        throw PlatformError(Code::memory, "pages", detail);
    }
    WindowsPages pages = take_pages((n + 1) * sizeof(wchar_t));
    const std::span<wchar_t> path(static_cast<wchar_t*>(pages.address()), n + 1);
    std::copy_n(a.begin(), n + 1, path.begin());
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    if (CreateDirectoryW(path.data(), nullptr) == FALSE) {
        return GetLastError();
    }
    out.id = ++serial_;
    out.path = std::move(pages);
    out.units = n;
    return 0;
}

files::Error WindowsFiles::open_staged(const Dir& dir, std::string_view path, File& out) {
    if (dir.id == 0) {
        return ERROR_PATH_NOT_FOUND;
    }
    Slot* slot = nullptr;
    for (Slot& free : slots_) {
        if (free.file == 0) {
            slot = &free;
            break;
        }
    }
    if (slot == nullptr) {
        return ERROR_TOO_MANY_OPEN_FILES;  // never reached: see Slot
    }
    // The staging directory's path, a separator, then path, '/' as '\'.
    const std::span<wchar_t> a = first();
    const std::span<const wchar_t> staging = path_of(dir);
    std::copy_n(staging.begin(), dir.units, a.begin());
    std::size_t n = dir.units;
    a[n++] = L'\\';
    DWORD error = 0;
    const std::size_t m = to_wide(path, a.subspan(n), error);
    if (m == 0) {
        return error;
    }
    const std::size_t end = n + m;
    std::replace(a.begin() + static_cast<std::ptrdiff_t>(n), a.begin() + static_cast<std::ptrdiff_t>(end), L'/', L'\\');
    // Its parents, each made unless present.
    for (std::size_t at = n; at < end; ++at) {
        if (a[at] != L'\\') {
            continue;
        }
        a[at] = L'\0';
        if (faulted()) {
            return ERROR_GEN_FAILURE;
        }
        if (CreateDirectoryW(a.data(), nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS) {
            return GetLastError();
        }
        a[at] = L'\\';
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    void* file = CreateFileW(a.data(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    *slot = Slot{file, dir.id, ++serial_};
    out.serial = slot->file;
    return 0;
}

files::Error WindowsFiles::write(const File& file, std::string_view bytes) {
    const Slot* const slot = held(file.serial);
    if (slot == nullptr) {
        return ERROR_INVALID_HANDLE;
    }
    for (std::size_t at = 0; at < bytes.size();) {
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - at, 1U << 30U));
        DWORD wrote = 0;
        if (faulted()) {
            return ERROR_GEN_FAILURE;
        }
        if (WriteFile(slot->handle, bytes.substr(at).data(), chunk, &wrote, nullptr) == FALSE) {
            return GetLastError();
        }
        if (wrote == 0) {
            return ERROR_WRITE_FAULT;
        }
        at += wrote;
    }
    return 0;
}

files::Error WindowsFiles::close(File& file) noexcept {
    Slot* const slot = held(std::exchange(file.serial, 0));
    return slot == nullptr ? 0 : shut_slot(*slot);
}

files::Error WindowsFiles::close_all(const Dir& dir) {
    files::Error error = 0;
    for (Slot& slot : slots_) {
        if (slot.file != 0 && slot.dir == dir.id) {
            const files::Error one = shut_slot(slot);
            error = error != 0 ? error : one;
        }
    }
    return error;
}

files::Error WindowsFiles::publish(const Dir& dir, std::string_view name) {
    if (dir.id == 0) {
        return ERROR_PATH_NOT_FOUND;
    }
    // The staging directory's head, its separator kept, then name.
    const std::span<const wchar_t> staging = path_of(dir);
    const std::size_t head = dir.units - name_units;
    const std::span<wchar_t> b = second();
    std::copy_n(staging.begin(), head, b.begin());
    DWORD error = 0;
    if (to_wide(name, b.subspan(head), error) == 0) {
        return error;
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    return MoveFileExW(staging.data(), b.data(), 0) == FALSE ? GetLastError() : 0;
}

files::Error WindowsFiles::discard(const Dir& dir, Detail& left) noexcept {
    if (dir.id == 0) {
        return 0;
    }
    for (Slot& slot : slots_) {
        if (slot.file != 0 && slot.dir == dir.id) {
            shut(slot.handle);  // unchecked: its file is deleted next
            slot = Slot{};
        }
    }
    return walk(dir, left);
}

void WindowsFiles::forget(Dir& dir) noexcept {
    for (Slot& slot : slots_) {
        if (slot.file != 0 && slot.dir == dir.id) {
            shut(slot.handle);
            slot = Slot{};
        }
    }
    dir = Dir{};
}

// From the staging directory down: a directory's first entry by its own
// FindFirstFileW handle; its files and links deleted as they come; a
// directory descended into, the handle closed first; an empty one removed,
// then its parent searched again. Each step deletes an entry or stops, so
// the walk ends; a directory gone (2, 3) counts as removed.
files::Error WindowsFiles::walk(const Dir& dir, Detail& left) noexcept {
    const std::span<wchar_t> a = first();
    const std::size_t root = dir.units;
    std::copy_n(path_of(dir).begin(), root + 1, a.begin());
    std::size_t n = root;
    WIN32_FIND_DATAW found{};
    for (;;) {
        DWORD error = n + 2 >= units ? ERROR_FILENAME_EXCED_RANGE : 0;
        std::size_t failed_at = n;
        bool descend = false;
        bool gone = false;
        HANDLE search = INVALID_HANDLE_VALUE;
        if (error == 0) {
            a[n] = L'\\';
            a[n + 1] = L'*';
            a[n + 2] = L'\0';
            if (faulted()) {
                error = ERROR_GEN_FAILURE;
            } else {
                search = FindFirstFileW(a.data(), &found);
                error = search == INVALID_HANDLE_VALUE ? GetLastError() : 0;
            }
            a[n] = L'\0';
            gone = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        }
        for (bool more = error == 0; more;) {
            const std::wstring_view entry(static_cast<const wchar_t*>(found.cFileName));
            if (entry != L"." && entry != L"..") {
                const std::size_t end = n + 1 + entry.size();
                if (end >= units) {
                    error = ERROR_FILENAME_EXCED_RANGE;
                    break;
                }
                a[n] = L'\\';
                std::copy(entry.begin(), entry.end(), a.begin() + static_cast<std::ptrdiff_t>(n + 1));
                a[end] = L'\0';
                const bool directory = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                if (directory && (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                    descend = true;
                    n = end;
                    break;
                }
                // A file, or a link removed as itself.
                const bool fault = faulted();
                const BOOL removed = fault ? FALSE : directory ? RemoveDirectoryW(a.data()) : DeleteFileW(a.data());
                if (removed == FALSE) {
                    error = fault ? ERROR_GEN_FAILURE : GetLastError();
                    failed_at = end;
                    break;
                }
                a[n] = L'\0';
            }
            if (faulted()) {
                error = ERROR_GEN_FAILURE;
                more = false;
            } else if (FindNextFileW(search, &found) == FALSE) {
                const DWORD next = GetLastError();
                error = next == ERROR_NO_MORE_FILES ? 0 : next;
                more = false;
            }
        }
        if (search != INVALID_HANDLE_VALUE) {
            FindClose(search);
        }
        if (descend) {
            continue;
        }
        if (error != 0 && !gone) {
            name_into(left, a, root + 1, failed_at);
            return error;
        }
        if (!gone) {
            if (faulted()) {
                error = ERROR_GEN_FAILURE;
            } else if (RemoveDirectoryW(a.data()) == FALSE) {
                error = GetLastError();
            }
            if (error != 0) {
                name_into(left, a, root + 1, n);
                return error;
            }
        }
        if (n == root) {
            return 0;
        }
        // Back to the parent, its search begun again.
        while (n > root && a[n] != L'\\') {
            --n;
        }
        a[n] = L'\0';
    }
}

}  // namespace qiven3::platform::win
