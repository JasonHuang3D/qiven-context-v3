// win/facts' calls (platform/win/facts_calls.hpp): the registry reads
// facts.cpp and synced.cpp share, each asking the countdown first.

#include "platform/win/facts_calls.hpp"

#include <windows.h>

#include <cstddef>
#include <span>
#include <string_view>

#include "platform/win/files.hpp"
#include "platform/win/windows.hpp"

namespace qiven3::platform::win::calls {

[[noreturn]] void failed(std::string_view operation, std::string_view call, unsigned long error) {
    Detail detail(call);
    detail.add(": ");
    add_error(detail, error);
    throw PlatformError(Code::path, operation, detail);
}

// UTF-16 into the caller's out, strictly: a refusal is path.
void narrow(std::string_view operation, std::span<const wchar_t> text, String& out) {
    out.clear();
    if (text.empty()) {
        return;
    }
    const int size = static_cast<int>(text.size());
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr,
                                      nullptr);
    if (n <= 0) {
        failed(operation, "not valid UTF-16: WideCharToMultiByte", GetLastError());
    }
    out.resize(static_cast<std::size_t>(n));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, out.data(), n, nullptr, nullptr);
}

LSTATUS open_key(WindowsFiles& f, HKEY root, const wchar_t* name, Key& out) noexcept {
    return f.faulted() ? ERROR_GEN_FAILURE : RegOpenKeyExW(root, name, 0, KEY_READ, out.put());
}

LSTATUS get_dword(WindowsFiles& f, HKEY root, const wchar_t* key, const wchar_t* value, DWORD& out) noexcept {
    DWORD size = sizeof(out);
    return f.faulted() ? ERROR_GEN_FAILURE : RegGetValueW(root, key, value, RRF_RT_REG_DWORD, nullptr, &out, &size);
}

// A string value (types: RRF_RT_REG_SZ or RRF_RT_REG_MULTI_SZ) into
// `into`, its units in n, the last null not counted.
LSTATUS get_text(WindowsFiles& f, HKEY root, const wchar_t* key, const wchar_t* value, DWORD types,
                 std::span<wchar_t> into, std::size_t& n) noexcept {
    n = 0;
    DWORD size = static_cast<DWORD>((into.size() - 1) * sizeof(wchar_t));
    const LSTATUS status =
        f.faulted() ? ERROR_GEN_FAILURE : RegGetValueW(root, key, value, types, nullptr, into.data(), &size);
    if (status == ERROR_SUCCESS) {
        n = size / sizeof(wchar_t);
        n -= n > 0 && into[n - 1] == L'\0' ? 1 : 0;
        into[n] = L'\0';
    }
    return status;
}

// The index-th subkey's name, or value's name where `values`.
LSTATUS name_at(WindowsFiles& f, HKEY key, DWORD index, bool values, std::span<wchar_t> into) noexcept {
    if (f.faulted()) {
        return ERROR_GEN_FAILURE;
    }
    auto size = static_cast<DWORD>(into.size());
    return values ? RegEnumValueW(key, index, into.data(), &size, nullptr, nullptr, nullptr, nullptr)
                  : RegEnumKeyExW(key, index, into.data(), &size, nullptr, nullptr, nullptr, nullptr);
}

}  // namespace qiven3::platform::win::calls
