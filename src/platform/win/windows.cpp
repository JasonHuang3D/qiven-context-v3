#include "platform/win/windows.hpp"

#include <windows.h>

#include <bcrypt.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>
#include <string_view>
#include <utility>

#include "platform/win/paths.hpp"

namespace qiven3::platform::win {
namespace {

using paths::units;

[[noreturn]] void fail(Code code, std::string_view operation, std::string_view detail) {
    throw PlatformError(code, operation, detail);
}

[[noreturn]] void fail(Code code, std::string_view operation, const Detail& detail) {
    throw PlatformError(code, operation, detail);
}

[[noreturn]] void failed(Code code, std::string_view operation, std::string_view call, unsigned long error) {
    Detail detail(call);
    detail.add(": ");
    add_error(detail, error);
    fail(code, operation, detail);
}

[[noreturn]] void not_yet(std::string_view operation) {
    fail(Code::unsupported, operation, "not implemented by the Windows adapter yet");
}

// UTF-8 into `into`, strictly, ended by a null: its length in units. A
// refusal, or a path past the buffer, is path.
std::size_t wide(std::string_view operation, std::string_view text, std::span<wchar_t> into) {
    into[0] = L'\0';
    if (text.empty()) {
        return 0;
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        fail(Code::path, operation, "longer than the conversion takes");
    }
    // Each call takes the text's size beside its bytes, never a terminator.
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0);
    if (n <= 0) {
        failed(Code::path, operation, "not UTF-8: MultiByteToWideChar", GetLastError());
    }
    const auto length = static_cast<std::size_t>(n);
    if (length >= into.size()) {
        fail(Code::path, operation, "longer than 32,767 UTF-16 units");
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), into.data(), n);
    into[length] = L'\0';
    return length;
}

// UTF-16 into the caller's out, strictly: a refusal is code.
void narrow(Code code, std::string_view operation, std::span<const wchar_t> text, String& out) {
    out.clear();
    if (text.empty()) {
        return;
    }
    const int size = static_cast<int>(text.size());
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0, nullptr,
                                      nullptr);
    if (n <= 0) {
        failed(code, operation, "not valid UTF-16: WideCharToMultiByte", GetLastError());
    }
    out.resize(static_cast<std::size_t>(n));
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, out.data(), n, nullptr, nullptr);
}

// The final path of a present file or directory, the first `length` units
// of `path`, into `into`, by a handle opened with no access and backup
// semantics: its length, or 0 with the error number in `error`.
std::size_t final_path(std::span<wchar_t> path, std::size_t length, std::span<wchar_t> into, DWORD& error) {
    const wchar_t saved = path[length];
    path[length] = L'\0';
    HANDLE handle =
        CreateFileW(path.data(), 0, 0, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    path[length] = saved;
    if (handle == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        return 0;
    }
    const DWORD n = GetFinalPathNameByHandleW(handle, into.data(), static_cast<DWORD>(into.size()),
                                              FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    error = n == 0 ? GetLastError() : 0;
    CloseHandle(handle);
    if (n >= into.size()) {
        fail(Code::path, "resolve", "a final path longer than 32,767 UTF-16 units");
    }
    return n;
}

// The bytes of each code point of `text` in the ANSI code page, appended to
// `into` from `held`, through `flush` when it fills: one '?' for a character
// the code page lacks, never a look-alike; CRLF for LF. Each code point is
// converted alone, since the call writes one '?' per UTF-16 unit, into a
// buffer of the code page's widest character per unit.
template <class Flush>
void encode(std::span<const wchar_t> text, std::span<char> into, std::size_t& held, Flush& flush) {
    const UINT acp = GetACP();
    CPINFO info{};
    const std::size_t widest =
        GetCPInfo(acp, &info) && info.MaxCharSize > 0 ? std::min<std::size_t>(info.MaxCharSize, 8) : 4;
    const bool utf8 = acp == CP_UTF8;  // takes no flag and no default character
    std::array<char, 16> one{};
    for (std::size_t i = 0; i < text.size();) {
        const wchar_t unit = text[i];
        const std::size_t n = (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < text.size()) ? 2 : 1;
        if (held + one.size() > into.size()) {
            flush(into.first(held));
            held = 0;
        }
        if (unit == L'\n') {
            into[held++] = '\r';
            into[held++] = '\n';
        } else if (n == 1 && unit < 0x80) {
            into[held++] = static_cast<char>(unit);
        } else {
            BOOL used = FALSE;
            const int wrote = WideCharToMultiByte(acp, utf8 ? 0 : WC_NO_BEST_FIT_CHARS, text.subspan(i, n).data(),
                                                  static_cast<int>(n), one.data(), static_cast<int>(2 * widest),
                                                  utf8 ? nullptr : "?", utf8 ? nullptr : &used);
            if (wrote <= 0 || used) {
                into[held++] = '?';
            } else {
                for (std::size_t k = 0; k < static_cast<std::size_t>(wrote); ++k) {
                    into[held++] = one[k];
                }
            }
        }
        i += n;
    }
}

}  // namespace

void add_error(Detail& detail, unsigned long error) noexcept {
    static constexpr std::array<std::pair<unsigned long, std::string_view>, 26> names = {{
        {1, "ERROR_INVALID_FUNCTION"},       {2, "ERROR_FILE_NOT_FOUND"},
        {3, "ERROR_PATH_NOT_FOUND"},         {5, "ERROR_ACCESS_DENIED"},
        {6, "ERROR_INVALID_HANDLE"},         {8, "ERROR_NOT_ENOUGH_MEMORY"},
        {21, "ERROR_NOT_READY"},             {31, "ERROR_GEN_FAILURE"},
        {32, "ERROR_SHARING_VIOLATION"},     {33, "ERROR_LOCK_VIOLATION"},
        {50, "ERROR_NOT_SUPPORTED"},         {53, "ERROR_BAD_NETPATH"},
        {67, "ERROR_BAD_NET_NAME"},          {80, "ERROR_FILE_EXISTS"},
        {87, "ERROR_INVALID_PARAMETER"},     {109, "ERROR_BROKEN_PIPE"},
        {122, "ERROR_INSUFFICIENT_BUFFER"},  {123, "ERROR_INVALID_NAME"},
        {145, "ERROR_DIR_NOT_EMPTY"},        {183, "ERROR_ALREADY_EXISTS"},
        {206, "ERROR_FILENAME_EXCED_RANGE"}, {232, "ERROR_NO_DATA"},
        {267, "ERROR_DIRECTORY"},            {1113, "ERROR_NO_UNICODE_TRANSLATION"},
        {1920, "ERROR_CANT_ACCESS_FILE"},    {1921, "ERROR_CANT_RESOLVE_FILENAME"},
    }};
    for (const auto& entry : names) {
        if (entry.first == error) {
            detail.add(entry.second).add(" (").number(error).add(")");
            return;
        }
    }
    detail.add("Win32 error ").number(error);
}

void StandardOutput::write(std::string_view utf8) { platform_->write_output(utf8); }

// --- the adapter ---

WindowsPlatform::WindowsPlatform(std::optional<std::string_view> config_dir) {
    std::size_t extra = 0;
    if (config_dir) {
        overridden_ = true;
        const std::string_view text = *config_dir;
        if (text.size() > static_cast<std::size_t>(INT_MAX)) {
            bad_override_ = true;
        } else if (!text.empty()) {
            const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), nullptr, 0);
            bad_override_ = n <= 0 || static_cast<std::size_t>(n) >= units;
            extra = bad_override_ ? 0 : static_cast<std::size_t>(n);
        }
    }
    scratch_ = pages((2 * units + extra) * sizeof(wchar_t));
    if (extra > 0 && config_dir) {
        const std::span<wchar_t> into = paths::scratch(scratch_).subspan(2 * units, extra);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, config_dir->data(),
                            static_cast<int>(config_dir->size()), into.data(), static_cast<int>(extra));
    }
    override_ = extra;
}

void WindowsPlatform::config_dir(bool create, String& out) {
    out.clear();
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    std::size_t m = 0;
    if (overridden_) {
        if (bad_override_) {
            fail(Code::path, "config_dir", "the override is not UTF-8, or longer than 32,767 UTF-16 units");
        }
        const std::span<const wchar_t> given = all.subspan(2 * units, override_);
        if (!paths::absolute(given)) {
            fail(Code::path, "config_dir", "the override is not absolute");
        }
        std::copy(given.begin(), given.end(), a.begin());
        m = given.size();
    } else {
        PWSTR folder = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder);
        if (FAILED(hr) || folder == nullptr) {
            CoTaskMemFree(folder);
            fail(Code::config, "config_dir", Detail("SHGetKnownFolderPath: HRESULT ").hex(static_cast<std::uint32_t>(hr)));
        }
        const std::wstring_view found(folder);
        constexpr std::wstring_view leaf = L"\\qiven3";
        const bool fits = found.size() + leaf.size() < units;
        if (fits) {
            std::copy(found.begin(), found.end(), a.begin());
            std::copy(leaf.begin(), leaf.end(), a.subspan(found.size()).begin());
            m = found.size() + leaf.size();
        }
        CoTaskMemFree(folder);
        if (!fits) {
            fail(Code::config, "config_dir", "the known folder is longer than 32,767 UTF-16 units");
        }
    }
    a[m] = L'\0';
    narrow(overridden_ ? Code::path : Code::config, "config_dir", a.first(m), out);
    if (create && !CreateDirectoryW(a.data(), nullptr)) {
        const DWORD error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) {
            const DWORD attributes = GetFileAttributesW(a.data());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                return;
            }
            out.clear();
            failed(Code::exists, "config_dir", "not a directory: CreateDirectoryW", error);
        }
        out.clear();
        failed(Code::io, "config_dir", "CreateDirectoryW", error);
    }
}

// The one form: the path made normal; the final path of it, or of its
// longest present ancestor with the rest joined; "\\?\" stripped unless the
// path began with it, "\\?\UNC\" read as "\\"; on a drive letter, or path.
void WindowsPlatform::resolve(std::string_view path, String& out) {
    out.clear();
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    const std::span<wchar_t> b = all.subspan(units, units);
    const std::size_t given = wide("resolve", path, a);
    if (std::ranges::any_of(a.first(given), paths::control)) {
        fail(Code::path, "resolve", "holds a control character");
    }
    if (!paths::absolute(a.first(given))) {
        fail(Code::path, "resolve", "not absolute");
    }
    const std::size_t m = paths::normal(a, given);
    const bool had_prefix = paths::starts(a.first(m), L"\\\\?\\");
    std::size_t current = m;  // the present ancestor tried: a's first `current` units
    std::size_t tail = m;     // the rest, a's units from `tail` to m
    std::size_t f = 0;        // the result's length, in b
    for (;;) {
        DWORD error = 0;
        f = final_path(a, current, b, error);
        if (f > 0) {
            f = paths::join(b, f, a.subspan(tail, m - tail));
            break;
        }
        const paths::Split split = paths::split(a.first(current));
        if (split.name == current) {
            f = paths::append(b, 0, a.first(split.head));
            f = paths::append(b, f, a.subspan(tail, m - tail));
            break;
        }
        tail = split.name;
        current = split.head;
        if (current == 0) {
            f = paths::append(b, 0, a.subspan(tail, m - tail));
            break;
        }
    }
    std::size_t from = 0;
    if (!had_prefix) {
        if (paths::starts(b.first(f), L"\\\\?\\UNC\\")) {
            b[6] = L'\\';
            b[7] = L'\\';
            from = 6;
        } else if (paths::starts(b.first(f), L"\\\\?\\")) {
            from = 4;
        }
    }
    const std::span<const wchar_t> result = b.subspan(from, f - from);
    narrow(Code::path, "resolve", result, out);
    if (!paths::on_letter(result)) {
        Detail detail("not on a drive letter once resolved: ");
        detail.add(std::string_view(out.data(), out.size()));
        out.clear();
        fail(Code::path, "resolve", detail);
    }
}

void WindowsPlatform::normcase(std::string_view path, String& out) {
    out.clear();
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    const std::span<wchar_t> b = all.subspan(units, units);
    const std::size_t m = wide("normcase", path, a);
    if (m == 0) {
        return;
    }
    std::ranges::replace(a.first(m), L'/', L'\\');
    const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, a.data(), static_cast<int>(m), b.data(),
                                static_cast<int>(b.size()), nullptr, nullptr, 0);
    if (n <= 0) {
        failed(Code::path, "normcase", "LCMapStringEx", GetLastError());
    }
    narrow(Code::path, "normcase", b.first(static_cast<std::size_t>(n)), out);
}

void WindowsPlatform::facts(std::string_view, Facts&) { not_yet("facts"); }

bool WindowsPlatform::synced(std::string_view, String& out) {
    out.clear();
    not_yet("synced");
}

void WindowsPlatform::replace(std::string_view path, std::string_view data, Seconds deadline) {
    files::replace(files_, path, data, deadline);
}

WindowsStaged WindowsPlatform::stage(std::string_view directory) { return files::stage(files_, directory); }

void WindowsPlatform::create(std::string_view path, bool directory) { files_.create(path, directory); }

bool WindowsPlatform::remove_directory(std::string_view path) { return files_.remove_directory(path); }

void WindowsPlatform::read(std::string_view path, String& out) { files_.read(path, out); }

WindowsLock WindowsPlatform::lock(std::string_view path, Seconds deadline) { return files::lock(files_, path, deadline); }

void WindowsPlatform::program(std::string_view, String& out) {
    out.clear();
    not_yet("program");
}

WindowsChild WindowsPlatform::start(const Command&) { not_yet("start"); }

void WindowsPlatform::run(const Command&, std::string_view, Ran&) { not_yet("run"); }

void WindowsPlatform::code_page(String& out) {
    UINT page = GetConsoleOutputCP();
    if (page == 0) {
        page = GetOEMCP();
    }
    out.assign(Detail("cp").number(page).view());
}

void WindowsPlatform::write_output(std::string_view utf8) {
    HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        fail(Code::io, "stdout", "no standard output handle");
    }
    DWORD mode = 0;
    const bool console = GetConsoleMode(handle, &mode) != 0;
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    const std::span<wchar_t> b = all.subspan(units, units);
    std::array<char, 1024> bytes{};
    std::size_t held = 0;
    const auto put = [handle](std::span<const char> chunk) {
        std::size_t done = 0;
        while (done < chunk.size()) {
            DWORD wrote = 0;
            const std::span<const char> rest = chunk.subspan(done);
            if (!WriteFile(handle, rest.data(), static_cast<DWORD>(rest.size()), &wrote, nullptr) || wrote == 0) {
                failed(Code::io, "stdout", "WriteFile", GetLastError());
            }
            done += wrote;
        }
    };
    // The text a chunk at a time, each cut at a code point, as UTF-16 by the
    // system's conversion, which writes U+FFFD for an invalid sequence.
    constexpr std::size_t chunk = units / 2;
    for (std::size_t at = 0; at < utf8.size();) {
        std::size_t end = std::min(utf8.size(), at + chunk);
        for (int k = 0; k < 3 && end < utf8.size() && end > at + 1 &&
                        (static_cast<unsigned char>(utf8[end]) & 0xC0U) == 0x80U;
             ++k) {
            --end;
        }
        const std::string_view piece = utf8.substr(at, end - at);
        at = end;
        const int n = MultiByteToWideChar(CP_UTF8, 0, piece.data(), static_cast<int>(piece.size()), a.data(),
                                          static_cast<int>(a.size()));
        const std::span<const wchar_t> text = a.first(n > 0 ? static_cast<std::size_t>(n) : 0);
        if (!console) {
            encode(text, std::span<char>(bytes), held, put);
            continue;
        }
        std::size_t j = 0;
        for (const wchar_t unit : text) {
            if (unit == L'\n') {
                b[j++] = L'\r';
            }
            b[j++] = unit;
        }
        for (std::size_t done = 0; done < j;) {
            const DWORD part = static_cast<DWORD>(std::min<std::size_t>(j - done, 8192));
            DWORD wrote = 0;
            if (!WriteConsoleW(handle, b.subspan(done).data(), part, &wrote, nullptr) || wrote == 0) {
                failed(Code::io, "stdout", "WriteConsoleW", GetLastError());
            }
            done += wrote;
        }
    }
    if (held > 0) {
        put(std::span<const char>(bytes).first(held));
    }
}

void WindowsPlatform::residual(std::span<const std::string_view>, Vector<Row>& out) {
    out.clear();
    not_yet("residual");
}

// The system's cryptographic generator, through a block on the stack that
// is zeroed after; a failure is io, and nothing falls back to a weaker
// source.
void WindowsPlatform::random_bytes(std::span<std::byte> into) {
    std::array<unsigned char, 256> block{};
    for (std::size_t done = 0; done < into.size();) {
        const std::size_t n = std::min(into.size() - done, block.size());
        const NTSTATUS status =
            BCryptGenRandom(nullptr, block.data(), static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!BCRYPT_SUCCESS(status)) {
            SecureZeroMemory(block.data(), block.size());
            fail(Code::io, "random_bytes",
                 Detail("BCryptGenRandom: NTSTATUS ").hex(static_cast<std::uint32_t>(status)));
        }
        std::memcpy(into.subspan(done, n).data(), block.data(), n);
        done += n;
    }
    SecureZeroMemory(block.data(), block.size());
}

void WindowsPlatform::wipe(std::span<std::byte> bytes) noexcept {
    if (!bytes.empty()) {
        SecureZeroMemory(bytes.data(), bytes.size());
    }
}

std::chrono::sys_time<std::chrono::milliseconds> WindowsPlatform::now() const {
    return std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
}

WindowsPages WindowsPlatform::pages(std::size_t n) { return take_pages(n); }

// n bytes rounded up to whole pages, reserved and committed at once, which
// the system gives zeroed.
WindowsPages take_pages(std::size_t n) {
    if (n == 0) {
        return WindowsPages();
    }
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::size_t page = info.dwPageSize;
    const std::optional<std::size_t> padded = memory::add(n, page - 1);
    if (!padded) {
        fail(Code::memory, "pages", Detail("a count that cannot be rounded to whole pages: ").number(n));
    }
    const std::size_t rounded = *padded / page * page;
    void* address = VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (address == nullptr) {
        const DWORD error = GetLastError();
        Detail detail("VirtualAlloc of ");
        detail.number(rounded).add(" bytes: ");
        add_error(detail, error);
        fail(Code::memory, "pages", detail);
    }
    return WindowsPages(address, rounded);
}

}  // namespace qiven3::platform::win
