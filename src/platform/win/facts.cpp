// win/facts (platform/win/facts.hpp): WindowsPlatform's facts and residual;
// synced is in synced.cpp.

#include "platform/win/facts.hpp"

#include <windows.h>

#include <knownfolders.h>
#include <propsys.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "platform/win/facts_calls.hpp"
#include "platform/win/files.hpp"
#include "platform/win/paths.hpp"
#include "platform/win/windows.hpp"

namespace qiven3::platform::win {

void readings::add_result(Detail& detail, long result) noexcept {
    const auto bits = static_cast<std::uint32_t>(result);
    detail.add("HRESULT ").hex(bits);
    if ((bits & 0xFFFF0000U) == 0x80070000U) {
        detail.add(", ");
        add_error(detail, bits & 0xFFFFU);
        return;
    }
    static constexpr std::array<std::pair<std::uint32_t, std::string_view>, 5> names = {{
        {0x80004005U, "E_FAIL"},
        {0x80004002U, "E_NOINTERFACE"},
        {0x80010106U, "RPC_E_CHANGED_MODE"},
        {0x800401F0U, "CO_E_NOTINITIALIZED"},
        {0x8002802BU, "TYPE_E_ELEMENTNOTFOUND"},
    }};
    for (const auto& entry : names) {
        if (entry.first == bits) {
            detail.add(", ").add(entry.second);
            return;
        }
    }
}

State readings::unopened(Detail& reason, long status) noexcept {
    if (status == ERROR_ACCESS_DENIED) {
        reason.add("admin_only");
        return State::unknown;
    }
    if (status == ERROR_FILE_NOT_FOUND) {
        reason.add("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SPP\\Clients: ");
    }
    reason.add("RegOpenKeyExW: ");
    add_error(reason, static_cast<unsigned long>(status));
    return State::unknown;
}

namespace {

using paths::units;
using readings::Applies;
using calls::failed;
using calls::get_dword;
using calls::get_text;
using calls::Key;
using calls::narrow;
using calls::open_key;

constexpr HRESULT faulted_result = HRESULT_FROM_WIN32(ERROR_GEN_FAILURE);

State unknown(Detail& reason, std::string_view call, unsigned long error) noexcept {
    reason.add(call).add(": ");
    add_error(reason, error);
    return State::unknown;
}

State unknown_result(Detail& reason, std::string_view call, HRESULT result) noexcept {
    reason.add(call).add(": ");
    readings::add_result(reason, result);
    return State::unknown;
}

// The user's local application data, its units in n.
HRESULT local_data(WindowsFiles& f, std::span<wchar_t> into, std::size_t& n) noexcept {
    n = 0;
    PWSTR folder = nullptr;
    const HRESULT result =
        f.faulted() ? faulted_result : SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder);
    if (FAILED(result) || folder == nullptr) {
        CoTaskMemFree(folder);  // freed whether the call succeeds or not
        return FAILED(result) ? result : E_FAIL;
    }
    const std::wstring_view found(folder);
    n = std::min(found.size(), into.size() - 1);
    std::copy_n(found.begin(), n, into.begin());
    into[n] = L'\0';
    CoTaskMemFree(folder);
    return S_OK;
}

// The mount point of the path in `path`, ending in '\', into `into`: its
// length, or 0 with error.
std::size_t mount(WindowsFiles& f, const wchar_t* path, std::span<wchar_t> into, DWORD& error) noexcept {
    error = ERROR_GEN_FAILURE;
    if (f.faulted()) {
        return 0;
    }
    if (GetVolumePathNameW(path, into.data(), static_cast<DWORD>(into.size())) == FALSE) {
        error = GetLastError();
        return 0;
    }
    error = 0;
    return std::wstring_view(into.data()).size();
}

// System Protection on the volume mounted at root: the policy's DisableSR
// 1 off; else the SPP Clients key read, every value a list naming the
// volumes it protects (RegEnumValueW, then RegGetValueW of each), on for
// a volume one names and off for another; the key unopened unknown
// (readings::unopened), a value refused unknown, admin_only. No value's
// name is assumed. `list` takes each value.
State protection(WindowsFiles& f, const wchar_t* root, std::span<wchar_t> list, Detail& reason) noexcept {
    DWORD value = 0;
    LSTATUS status = get_dword(f, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\SystemRestore",
                               L"DisableSR", value);
    if (status == ERROR_SUCCESS && value == 1) {
        return State::off;
    }
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        return unknown(reason, "RegGetValueW", static_cast<unsigned long>(status));
    }
    Key clients;
    status = open_key(f, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\SPP\\Clients", clients);
    if (status != ERROR_SUCCESS) {
        return readings::unopened(reason, status);
    }
    std::array<wchar_t, 64> volume{};
    DWORD error = ERROR_GEN_FAILURE;
    if (!f.faulted()) {
        error = GetVolumeNameForVolumeMountPointW(root, volume.data(), static_cast<DWORD>(volume.size())) == FALSE
                    ? GetLastError()
                    : 0;
    }
    if (error != 0) {
        return unknown(reason, "GetVolumeNameForVolumeMountPointW", error);
    }
    std::array<wchar_t, 256> name{};  // a value's name; one past 255 units is ERROR_MORE_DATA, unknown
    for (DWORD i = 0;; ++i) {
        status = calls::name_at(f, clients.get(), i, true, name);
        if (status == ERROR_NO_MORE_ITEMS) {
            return State::off;
        }
        std::string_view call = "RegEnumValueW";
        std::size_t n = 0;
        if (status == ERROR_SUCCESS) {
            call = "RegGetValueW";
            status = get_text(f, clients.get(), nullptr, name.data(), RRF_RT_REG_MULTI_SZ, list, n);
        }
        if (status == ERROR_ACCESS_DENIED) {
            reason.add("admin_only");
            return State::unknown;
        }
        if (status == ERROR_FILE_NOT_FOUND) {
            continue;  // gone since it was listed
        }
        if (status != ERROR_SUCCESS) {
            return unknown(reason, call, static_cast<unsigned long>(status));
        }
        if (readings::names(std::wstring_view(list.data(), n), volume.data())) {
            return State::on;
        }
    }
}

// System.Volume.BitLockerProtection's key, by its name, from the property
// system's own object, the one PSGetPropertyKeyFromName asks, so that the
// adapter imports no library past ole32 and shell32 for it; `call` names
// the call that failed.
HRESULT bitlocker_key(WindowsFiles& f, PROPERTYKEY& key, std::string_view& call) {
    call = "CoCreateInstance";
    IPropertySystem* system = nullptr;
    HRESULT result = f.faulted() ? faulted_result
                                 : CoCreateInstance(__uuidof(PropertySystem), nullptr, CLSCTX_INPROC_SERVER,
                                                    IID_PPV_ARGS(&system));
    if (FAILED(result) || system == nullptr) {
        return FAILED(result) ? result : E_FAIL;
    }
    call = "IPropertySystem::GetPropertyDescriptionByName";
    IPropertyDescription* description = nullptr;
    result = f.faulted() ? faulted_result
                         : system->GetPropertyDescriptionByName(L"System.Volume.BitLockerProtection",
                                                                IID_PPV_ARGS(&description));
    system->Release();
    if (FAILED(result) || description == nullptr) {
        return FAILED(result) ? result : E_FAIL;
    }
    call = "IPropertyDescription::GetPropertyKey";
    result = f.faulted() ? faulted_result : description->GetPropertyKey(&key);
    description->Release();
    return result;
}

// System.Volume.BitLockerProtection of the volume's root, read with COM
// set up by the caller.
State read_bitlocker(WindowsFiles& f, const wchar_t* root, Detail& reason) {
    PROPERTYKEY key{};
    std::string_view call;
    HRESULT result = bitlocker_key(f, key, call);
    if (FAILED(result)) {
        return unknown_result(reason, call, result);
    }
    IPropertyStore* store = nullptr;
    result = f.faulted() ? faulted_result
                         : SHGetPropertyStoreFromParsingName(root, nullptr, GPS_DEFAULT, IID_PPV_ARGS(&store));
    if (FAILED(result) || store == nullptr) {
        return unknown_result(reason, "SHGetPropertyStoreFromParsingName", FAILED(result) ? result : E_FAIL);
    }
    PROPVARIANT value;
    PropVariantInit(&value);
    result = f.faulted() ? faulted_result : store->GetValue(key, &value);
    store->Release();
    if (FAILED(result)) {
        return unknown_result(reason, "IPropertyStore::GetValue", result);
    }
    State state = State::unknown;
    if (value.vt == VT_I4 || value.vt == VT_UI4) {
        const auto number = value.vt == VT_I4 ? static_cast<std::uint32_t>(value.lVal) : value.ulVal;
        state = readings::bitlocker(number);
        if (state == State::unknown) {
            reason.add("System.Volume.BitLockerProtection ").number(number);
        }
    } else {
        reason.add(value.vt == VT_EMPTY ? "no value" : "a value of another type");
    }
    PropVariantClear(&value);
    return state;
}

// BitLocker: COM set up for the read, on this thread's apartment if it has
// one, and torn down after.
State bitlocker(WindowsFiles& f, const wchar_t* root, Detail& reason) {
    constexpr DWORD apartment =
        static_cast<DWORD>(COINIT_APARTMENTTHREADED) | static_cast<DWORD>(COINIT_DISABLE_OLE1DDE);
    const HRESULT result = f.faulted() ? faulted_result : CoInitializeEx(nullptr, apartment);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        return unknown_result(reason, "CoInitializeEx", result);
    }
    const State state = read_bitlocker(f, root, reason);
    if (SUCCEEDED(result)) {
        CoUninitialize();
    }
    return state;
}

// File History, per user: the policy's Disabled 1 off; else the
// configuration directory in the local application data on, none off.
// `into` takes the directory's path.
State file_history(WindowsFiles& f, std::span<wchar_t> into, Detail& reason) noexcept {
    DWORD value = 0;
    const LSTATUS status =
        get_dword(f, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Policies\\Microsoft\\Windows\\FileHistory", L"Disabled", value);
    if (status == ERROR_SUCCESS && value == 1) {
        return State::off;
    }
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        return unknown(reason, "RegGetValueW", static_cast<unsigned long>(status));
    }
    std::size_t n = 0;
    const HRESULT result = local_data(f, into, n);
    if (FAILED(result)) {
        return unknown_result(reason, "SHGetKnownFolderPath", result);
    }
    constexpr std::wstring_view leaf = L"\\Microsoft\\Windows\\FileHistory\\Configuration";
    if (n + leaf.size() >= into.size()) {
        return unknown(reason, "SHGetKnownFolderPath", ERROR_FILENAME_EXCED_RANGE);
    }
    std::ranges::copy(leaf, into.subspan(n).begin());
    into[n + leaf.size()] = L'\0';
    DWORD attributes = INVALID_FILE_ATTRIBUTES;
    DWORD error = ERROR_GEN_FAILURE;
    if (!f.faulted()) {
        attributes = GetFileAttributesW(into.data());
        error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
    }
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            return State::off;
        }
        return unknown(reason, "GetFileAttributesW", error);
    }
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? State::on : State::off;
}

// residual's reads beyond the facts: each value as the row reads it, a
// failure but for none there unknown.
Applies pagefile(WindowsFiles& f) noexcept {
    DWORD value = 0;
    const LSTATUS status =
        get_dword(f, HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                  L"ClearPageFileAtShutdown", value);
    if (status == ERROR_FILE_NOT_FOUND) {
        return Applies::yes;
    }
    return status != ERROR_SUCCESS ? Applies::unknown : value == 1 ? Applies::no : Applies::yes;
}

Applies hibernation(WindowsFiles& f) noexcept {
    DWORD value = 0;
    const LSTATUS status =
        get_dword(f, HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Power", L"HibernateEnabled", value);
    if (status == ERROR_FILE_NOT_FOUND) {
        return Applies::no;
    }
    return status != ERROR_SUCCESS ? Applies::unknown : value == 1 ? Applies::yes : Applies::no;
}

// A LocalDumps key yes; else Windows Error Reporting's Disabled 1, its key
// or its policy, HKLM's or HKCU's, no; else yes, unknown where a read fails.
Applies crash_dumps(WindowsFiles& f) noexcept {
    Key dumps;
    LSTATUS status =
        open_key(f, HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps", dumps);
    if (status == ERROR_SUCCESS) {
        return Applies::yes;
    }
    bool unread = status != ERROR_FILE_NOT_FOUND;
    const std::array<HKEY, 2> roots = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    static constexpr std::array<const wchar_t*, 2> keys = {
        L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting",
        L"SOFTWARE\\Policies\\Microsoft\\Windows\\Windows Error Reporting"};
    for (HKEY root : roots) {
        for (const wchar_t* key : keys) {
            DWORD value = 0;
            status = get_dword(f, root, key, L"Disabled", value);
            if (status == ERROR_SUCCESS && value == 1) {
                return Applies::no;
            }
            unread = unread || (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND);
        }
    }
    return unread ? Applies::unknown : Applies::yes;
}

void assign(Reading& out, State state, const Detail& reason) {
    out.state = state;
    out.reason.assign(reason.view());
}

}  // namespace

// The volume, then System Protection and BitLocker on it, then File
// History: the path's buffer then the mount point's reused.
void WindowsPlatform::facts(std::string_view path, Facts& out) {
    out.volume.clear();
    for (Reading* reading : std::array<Reading*, 3>{&out.protection, &out.file_history, &out.bitlocker}) {
        reading->state = State::unknown;
        reading->reason.clear();
    }
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    const std::span<wchar_t> b = all.subspan(units, units);
    files_.checked("facts", path);
    DWORD error = 0;
    const std::size_t m = mount(files_, a.data(), b, error);
    if (m == 0) {
        failed("facts", "GetVolumePathNameW", error);
    }
    narrow("facts", b.first(m > 1 && b[m - 1] == L'\\' ? m - 1 : m), out.volume);
    Detail reason;
    assign(out.protection, protection(files_, b.data(), a, reason), reason);
    reason = Detail();
    assign(out.bitlocker, bitlocker(files_, b.data(), reason), reason);
    reason = Detail();
    assign(out.file_history, file_history(files_, b, reason), reason);
}

// Each path's volume read for restore_points and encryption, then the
// rows, each once; out cleared on a failure.
void WindowsPlatform::residual(std::span<const std::string_view> paths, Vector<Row>& out) {
    out.clear();
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    const std::span<wchar_t> b = all.subspan(units, units);
    Applies restore = Applies::no;
    Applies unencrypted = Applies::no;
    Detail reason;  // a row holds none
    for (const std::string_view path : paths) {
        files_.checked("residual", path);
        DWORD error = 0;
        if (mount(files_, a.data(), b, error) == 0) {
            failed("residual", "GetVolumePathNameW", error);
        }
        restore = readings::fold(restore, readings::kept_by(protection(files_, b.data(), a, reason)));
        unencrypted = readings::fold(unencrypted, readings::unencrypted(bitlocker(files_, b.data(), reason)));
    }
    const std::array<Applies, readings::rows.size()> applies = {
        restore,         readings::kept_by(file_history(files_, b, reason)),
        pagefile(files_), hibernation(files_),
        crash_dumps(files_), unencrypted};
    memory::Arena& arena = out.get_allocator().arena();
    try {
        out.reserve(applies.size());
        for (std::size_t i = 0; i < applies.size(); ++i) {
            const readings::RowView& text = readings::rows.at(i);
            Row& row = out.emplace_back(arena);
            row.name.assign(text.name);
            row.applies.assign(readings::text(applies.at(i)));
            row.kept.assign(text.kept);
            row.how_long.assign(text.how_long);
            row.owner_can.assign(text.owner_can);
        }
    } catch (...) {
        out.clear();
        throw;
    }
}

}  // namespace qiven3::platform::win