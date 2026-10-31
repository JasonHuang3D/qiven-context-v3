#pragma once

// win/facts' calls that facts.cpp and synced.cpp share: a registry key
// closed at its scope's end, and the registry reads, each asking the
// adapter's countdown first and failing with 31 (ERROR_GEN_FAILURE) when
// it fires. The adapter's sources alone include this header;
// facts_calls.cpp defines it.
//
// thread: one (the adapter's)

#include <windows.h>

#include <cstddef>
#include <span>
#include <string_view>

#include "platform/platform.hpp"
#include "platform/win/files.hpp"

namespace qiven3::platform::win::calls {

class Key {
public:
    Key() noexcept = default;
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
    Key(Key&&) = delete;
    Key& operator=(Key&&) = delete;
    ~Key() {
        if (key_ != nullptr) {
            RegCloseKey(key_);
        }
    }
    [[nodiscard]] HKEY get() const noexcept { return key_; }
    HKEY* put() noexcept { return &key_; }

private:
    HKEY key_ = nullptr;
};

// PlatformError path naming the call and its error.
[[noreturn]] void failed(std::string_view operation, std::string_view call, unsigned long error);

// UTF-16 into the caller's out, strictly: a refusal is path.
void narrow(std::string_view operation, std::span<const wchar_t> text, String& out);

// RegOpenKeyExW, KEY_READ.
LSTATUS open_key(WindowsFiles& f, HKEY root, const wchar_t* name, Key& out) noexcept;

// RegGetValueW of a REG_DWORD.
LSTATUS get_dword(WindowsFiles& f, HKEY root, const wchar_t* key, const wchar_t* value, DWORD& out) noexcept;

// RegGetValueW of a string (types: RRF_RT_REG_SZ or RRF_RT_REG_MULTI_SZ)
// into `into`, null-ended: its units in n, the last null not counted.
LSTATUS get_text(WindowsFiles& f, HKEY root, const wchar_t* key, const wchar_t* value, DWORD types,
                 std::span<wchar_t> into, std::size_t& n) noexcept;

// The index-th subkey's name (RegEnumKeyExW), or value's name where
// `values` (RegEnumValueW), into `into`.
LSTATUS name_at(WindowsFiles& f, HKEY key, DWORD index, bool values, std::span<wchar_t> into) noexcept;

}  // namespace qiven3::platform::win::calls
