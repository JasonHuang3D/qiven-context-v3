// win/facts' synced (platform/win/facts.hpp): the sync clients' folders
// that hold a path.

#include "platform/win/facts.hpp"

#include <windows.h>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "platform/win/facts_calls.hpp"
#include "platform/win/files.hpp"
#include "platform/win/paths.hpp"
#include "platform/win/windows.hpp"

namespace qiven3::platform::win {
namespace {

using calls::failed;
using calls::get_text;
using calls::Key;
using calls::name_at;
using calls::narrow;
using calls::open_key;
using paths::units;

// synced's search: the resolved path `path`; each root put in `root`,
// null-ended, and resolved into `spare`, the path's buffer past its null.
// A read failing but for none there is path.
class RootsView {
public:
    RootsView(WindowsFiles& f, std::span<const wchar_t> path, std::span<wchar_t> root,
              std::span<wchar_t> spare) noexcept
        : f_(&f), path_(path), root_(root), spare_(spare) {}

    // The folder named by OneDrive, OneDriveConsumer or OneDriveCommercial
    // in v3's environment.
    bool environment() {
        static constexpr std::array<const wchar_t*, 3> names = {L"OneDrive", L"OneDriveConsumer",
                                                                L"OneDriveCommercial"};
        for (const wchar_t* name : names) {
            DWORD got = 0;
            DWORD error = ERROR_GEN_FAILURE;
            if (!f_->faulted()) {
                SetLastError(ERROR_SUCCESS);
                got = GetEnvironmentVariableW(name, root_.data(), static_cast<DWORD>(root_.size()));
                error = got == 0 ? GetLastError() : 0;
            }
            if (got == 0 && error != ERROR_SUCCESS && error != ERROR_ENVVAR_NOT_FOUND) {
                failed("synced", "GetEnvironmentVariableW", error);
            }
            if (got >= root_.size()) {
                failed("synced", "GetEnvironmentVariableW", ERROR_FILENAME_EXCED_RANGE);
            }
            if (got > 0 && holds(got)) {
                return true;
            }
        }
        return false;
    }

    // Each account's UserFolder under HKCU's OneDrive Accounts.
    bool accounts() {
        Key accounts;
        LSTATUS status = open_key(*f_, HKEY_CURRENT_USER, L"Software\\Microsoft\\OneDrive\\Accounts", accounts);
        if (status == ERROR_FILE_NOT_FOUND) {
            return false;
        }
        check("RegOpenKeyExW", status);
        std::array<wchar_t, 256> name{};  // a key's name: at most 255 units
        for (DWORD i = 0;; ++i) {
            status = name_at(*f_, accounts.get(), i, false, name);
            if (status == ERROR_NO_MORE_ITEMS) {
                return false;
            }
            check("RegEnumKeyExW", status);
            std::size_t n = 0;
            status = get_text(*f_, accounts.get(), name.data(), L"UserFolder", RRF_RT_REG_SZ, root_, n);
            if (status != ERROR_FILE_NOT_FOUND) {
                check("RegGetValueW", status);
                if (holds(n)) {
                    return true;
                }
            }
        }
    }

    // Each sync root registered under SyncRootManager: <root>\UserSyncRoots,
    // a value per user naming the folder; the client, the root's name up
    // to its first '!', into client.
    bool registered(std::span<wchar_t> client) {
        Key manager;
        LSTATUS status = open_key(
            *f_, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SyncRootManager", manager);
        if (status == ERROR_FILE_NOT_FOUND) {
            return false;
        }
        check("RegOpenKeyExW", status);
        for (DWORD i = 0;; ++i) {
            status = name_at(*f_, manager.get(), i, false, client);
            if (status == ERROR_NO_MORE_ITEMS) {
                return false;
            }
            check("RegEnumKeyExW", status);
            Key one;
            Key users;
            status = open_key(*f_, manager.get(), client.data(), one);
            if (status == ERROR_SUCCESS) {
                status = open_key(*f_, one.get(), L"UserSyncRoots", users);
            }
            if (status == ERROR_FILE_NOT_FOUND) {
                continue;
            }
            check("RegOpenKeyExW", status);
            if (user_roots(users.get())) {
                return true;
            }
        }
    }

private:
    void check(std::string_view call, LSTATUS status) const {
        if (status != ERROR_SUCCESS) {
            failed("synced", call, static_cast<unsigned long>(status));
        }
    }

    bool user_roots(HKEY users) {
        std::array<wchar_t, 256> user{};  // a user's SID, as a value's name
        for (DWORD j = 0;; ++j) {
            LSTATUS status = name_at(*f_, users, j, true, user);
            if (status == ERROR_NO_MORE_ITEMS) {
                return false;
            }
            check("RegEnumValueW", status);
            std::size_t n = 0;
            status = get_text(*f_, users, nullptr, user.data(), RRF_RT_REG_SZ, root_, n);
            if (status != ERROR_FILE_NOT_FOUND && status != ERROR_UNSUPPORTED_TYPE) {
                check("RegGetValueW", status);
                if (holds(n)) {
                    return true;
                }
            }
        }
    }

    // Whether the path is the root's first n units or under it, the root
    // resolved first where it is present, as given where it is absent.
    bool holds(std::size_t n) {
        std::span<const wchar_t> root = root_.first(n);
        root_[n] = L'\0';
        HANDLE handle = INVALID_HANDLE_VALUE;
        DWORD error = ERROR_GEN_FAILURE;
        if (!f_->faulted()) {
            handle = CreateFileW(root_.data(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            error = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        }
        if (handle == INVALID_HANDLE_VALUE && error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            failed("synced", "CreateFileW", error);
        }
        if (handle != INVALID_HANDLE_VALUE) {
            DWORD got = 0;
            error = ERROR_GEN_FAILURE;
            if (!f_->faulted()) {
                got = GetFinalPathNameByHandleW(handle, spare_.data(), static_cast<DWORD>(spare_.size()),
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                error = got == 0 ? GetLastError() : got >= spare_.size() ? ERROR_FILENAME_EXCED_RANGE : 0;
            }
            CloseHandle(handle);
            if (error != 0) {
                failed("synced", "GetFinalPathNameByHandleW", error);
            }
            root = stripped(spare_.first(got));
        }
        if (root.size() > 3 && root.back() == L'\\') {
            root = root.first(root.size() - 1);
        }
        const std::size_t m = root.size();
        return m > 0 && path_.size() >= m &&
               CompareStringOrdinal(path_.data(), static_cast<int>(m), root.data(), static_cast<int>(m), TRUE) ==
                   CSTR_EQUAL &&
               (path_.size() == m || path_[m] == L'\\' || root.back() == L'\\');
    }

    // A final path with "\\?\" stripped, "\\?\UNC\" read as "\\", as
    // resolve gives it.
    static std::span<const wchar_t> stripped(std::span<wchar_t> final) noexcept {
        if (paths::starts(final, L"\\\\?\\UNC\\")) {
            final[6] = L'\\';
            final[7] = L'\\';
            return final.subspan(6);
        }
        return paths::starts(final, L"\\\\?\\") ? final.subspan(4) : final;
    }

    WindowsFiles* f_;
    std::span<const wchar_t> path_;
    std::span<wchar_t> root_;
    std::span<wchar_t> spare_;
};

}  // namespace

// The environment's folders, then the accounts', then the sync roots';
// the client "OneDrive" for the first two.
bool WindowsPlatform::synced(std::string_view path, String& out) {
    out.clear();
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    const std::span<wchar_t> b = all.subspan(units, units);
    const std::size_t n = files_.checked("synced", path);
    RootsView roots(files_, a.first(n), b, a.subspan(n + 1));
    if (roots.environment() || roots.accounts()) {
        out.assign("OneDrive");
        return true;
    }
    std::array<wchar_t, 256> client{};  // a key's name: at most 255 units
    if (!roots.registered(client)) {
        return false;
    }
    const std::wstring_view name = readings::client(client.data());
    narrow("synced", std::span<const wchar_t>(name.data(), name.size()), out);
    return true;
}

}  // namespace qiven3::platform::win
