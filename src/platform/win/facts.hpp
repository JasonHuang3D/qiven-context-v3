#pragma once

// win/facts: the Windows adapter's machine facts. WindowsPlatform's
// facts, synced and residual are in facts.cpp; here are the rules they
// apply that read no machine fact, the compiler able to evaluate each of
// BitLocker's value as a state, a System Protection list naming a volume,
// a residual row's applies folded over the paths, a sync root's client and
// the residual rows' texts; and an HRESULT as a detail names it, and
// System Protection's key unopened.
//
// Each member reads as a standard user, with no process, never elevating
// and never retrying a refused read by another route. In facts a read
// refused or failing is unknown, its call and error the reason, System
// Protection's list refused (error 5) unknown with the reason admin_only,
// its key missing unknown with the reason naming the key (unopened);
// in synced a read failing but for none there is PlatformError path, so
// that no synced folder is missed; in residual a row's read failing is
// applies unknown. Under QIVEN3_FAULTS each Win32, registry, COM and
// known-folder call asks the adapter's countdown first and, when it
// fires, fails as the call would with error 31 (ERROR_GEN_FAILURE), or
// the HRESULT 0x8007001f.
//
// Memory: facts writes its volume and reasons into the caller's out, and
// synced its client's name, each one allocation of the caller's arena
// where the text is past the string's own 15 bytes, none here; residual
// reserves its six rows at once and assigns each text. The scratch is the
// adapter's; the system's own memory (COM's, the known folder's) is the
// system's.
//
// thread: one (the adapter's; COM is set up and torn down within a call)

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "platform/platform.hpp"

namespace qiven3::platform::win::readings {

// System.Volume.BitLockerProtection: 2 off; 6 locked; 1, 3, 4, 5 and 8
// (on, encrypting, decrypting, suspended, waiting for activation) on;
// another unknown.
constexpr State bitlocker(std::uint32_t value) noexcept {
    switch (value) {
    case 2:
        return State::off;
    case 6:
        return State::locked;
    case 1:
    case 3:
    case 4:
    case 5:
    case 8:
        return State::on;
    default:
        return State::unknown;
    }
}

// A residual row's applies, and its text.
enum class Applies : std::uint8_t { no, yes, unknown };

constexpr std::string_view text(Applies applies) noexcept {
    return applies == Applies::yes ? "yes" : applies == Applies::no ? "no" : "unknown";
}

// Over the paths: yes where any is yes, else unknown where any is, else no.
constexpr Applies fold(Applies a, Applies b) noexcept {
    if (a == Applies::yes || b == Applies::yes) {
        return Applies::yes;
    }
    return a == Applies::unknown || b == Applies::unknown ? Applies::unknown : Applies::no;
}

// restore_points' and file_history's: on yes, off no, else unknown.
constexpr Applies kept_by(State state) noexcept {
    return state == State::on ? Applies::yes : state == State::off ? Applies::no : Applies::unknown;
}

// encryption's: BitLocker off yes, unknown unknown, on or locked no.
constexpr Applies unencrypted(State state) noexcept {
    return state == State::off ? Applies::yes : state == State::unknown ? Applies::unknown : Applies::no;
}

// A sync root's client: its key's name up to its first '!', all of it
// with none.
constexpr std::wstring_view client(std::wstring_view key) noexcept { return key.substr(0, key.find(L'!')); }

// Whether a System Protection list, its entries each ended by a null, names
// the volume: an entry, "\\?\Volume{GUID}\:(C%3A)", starting with the
// volume's name, its letters compared without case.
constexpr bool names(std::wstring_view list, std::wstring_view volume) noexcept {
    const auto fold = [](wchar_t c) { return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c; };
    while (!volume.empty() && !list.empty()) {
        const std::wstring_view entry = list.substr(0, list.find(L'\0'));
        if (entry.size() >= volume.size() && std::equal(volume.begin(), volume.end(), entry.begin(),
                                                        [&](wchar_t a, wchar_t b) { return fold(a) == fold(b); })) {
            return true;
        }
        list.remove_prefix(std::min(list.size(), entry.size() + 1));
    }
    return false;
}

// A residual row's texts: what is kept, how long and what the owner can
// do; never content.
struct RowView {
    std::string_view name, kept, how_long, owner_can;
};

// The rows in their order: restore_points, file_history, pagefile,
// hibernation, crash_dumps, encryption.
inline constexpr std::array<RowView, 6> rows = {{
    {"restore_points",
     "earlier blocks of every file on the volume: v3's, a backup's staging and the clients' transcripts",
     "until its shadow storage rotates them, at most DiskPercent of the volume",
     "delete restore points, or turn protection off"},
    {"file_history", "versions of the folders it includes", "its retention", "delete versions"},
    {"pagefile", "memory the system paged out", "until overwritten", "set the policy"},
    {"hibernation", "a memory image", "until overwritten", "turn hibernation off"},
    {"crash_dumps",
     "a faulting process's memory, v3's or a client's: dumps in LocalDumps' folder (CrashDumps in the local "
     "application data by default) and reports under Microsoft\\Windows\\WER in the local application data and "
     "ProgramData",
     "until deleted; LocalDumps keeps 10 by default", "delete them, or turn the dumps off"},
    {"encryption", "the freed blocks below the files, unencrypted at rest", "until overwritten", "turn BitLocker on"},
}};

// An HRESULT as a detail names it: "HRESULT 0x8007001f, ERROR_GEN_FAILURE
// (31)" for a Win32 error's, the adapter's names after it; "HRESULT
// 0x80004005, E_FAIL" for one of the few COM names here; else its number.
void add_result(Detail& detail, long result) noexcept;

// System Protection where RegOpenKeyExW does not open its SPP Clients key,
// by the call's status: unknown, never off, the reason admin_only where
// refused (5), else the call and its error, the key's name first where it
// is missing (2), as its place is not measured here.
State unopened(Detail& reason, long status) noexcept;

}  // namespace qiven3::platform::win::readings
