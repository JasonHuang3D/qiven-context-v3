// win/files (platform/win/files.hpp): list's, remove's and rename's calls,
// a directory's names read one find handle at a time, a file deleted, a
// file renamed in its directory with no replacing. Each Win32 call is a
// fault point but FindClose and a conversion's second call, which repeat
// what passed or free what is given up anyway.

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

#include "platform/win/files.hpp"
#include "platform/win/paths.hpp"
#include "platform/win/windows.hpp"

namespace qiven3::platform::win {
namespace {

using paths::units;

// A find handle closed at the scope's end.
class Found {
public:
    explicit Found(void* handle) noexcept : handle_(handle) {}
    Found(const Found&) = delete;
    Found& operator=(const Found&) = delete;
    Found(Found&&) = delete;
    Found& operator=(Found&&) = delete;
    ~Found() { FindClose(handle_); }

private:
    void* handle_;
};

}  // namespace

files::Error WindowsFiles::list_names(std::string_view directory, Vector<String>& names) {
    const std::span<wchar_t> a = first();
    DWORD error = 0;
    std::size_t n = to_wide(directory, a, error);
    if (n == 0) {
        return error;
    }
    if (n + 2 >= units) {
        return ERROR_FILENAME_EXCED_RANGE;
    }
    // "<directory>\*": files' check leaves no separator at its end.
    a[n++] = L'\\';
    a[n++] = L'*';
    a[n] = L'\0';
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    WIN32_FIND_DATAW found{};
    void* find = FindFirstFileExW(a.data(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, 0);
    if (find == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    const Found closed(find);  // on every way out, an allocation's MemoryError among them
    const memory::Allocator<char> arena(names.get_allocator());
    for (;;) {
        const std::span<const wchar_t> held(found.cFileName);
        const wchar_t* name = held.data();
        const auto length = static_cast<int>(std::find(held.begin(), held.end(), L'\0') - held.begin());
        const bool fault = faulted();
        const int bytes =
            fault ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, length, nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) {
            return fault ? ERROR_GEN_FAILURE : length == 0 ? ERROR_INVALID_NAME : GetLastError();
        }
        String& out = names.emplace_back(static_cast<std::size_t>(bytes), '\0', arena);
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name, length, out.data(), bytes, nullptr, nullptr);
        const bool next_fault = faulted();
        if (next_fault || FindNextFileW(find, &found) == FALSE) {
            const DWORD next = next_fault ? ERROR_GEN_FAILURE : GetLastError();
            return next == ERROR_NO_MORE_FILES ? 0 : next;
        }
    }
}

files::Error WindowsFiles::remove_file(std::string_view path) {
    DWORD error = 0;
    if (to_wide(path, first(), error) == 0) {
        return error;
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    return DeleteFileW(first().data()) != FALSE ? 0 : GetLastError();
}

files::Error WindowsFiles::rename_file(std::string_view path, std::string_view name) {
    const std::span<wchar_t> a = first();
    const std::span<wchar_t> b = second();
    DWORD error = 0;
    const std::size_t n = to_wide(path, a, error);
    if (n == 0) {
        return error;
    }
    // The head through the last separator, then name.
    std::size_t head = n;
    while (head > 0 && a[head - 1] != L'\\' && a[head - 1] != L'/') {
        --head;
    }
    std::copy_n(a.begin(), head, b.begin());
    const std::size_t m = to_wide(name, b.subspan(head), error);
    if (m == 0) {
        return error;
    }
    if (faulted()) {
        return ERROR_GEN_FAILURE;
    }
    // No flag: never MOVEFILE_REPLACE_EXISTING, never MOVEFILE_COPY_ALLOWED.
    return MoveFileExW(a.data(), b.data(), 0) != FALSE ? 0 : GetLastError();
}

}  // namespace qiven3::platform::win
