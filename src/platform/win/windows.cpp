#include "platform/win/windows.hpp"

#include <windows.h>

#include <bcrypt.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <algorithm>
#include <climits>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

namespace qiven3::platform::win {
namespace {

constexpr wchar_t kSep = L'\\';

bool is_sep(wchar_t c) { return c == L'\\' || c == L'/'; }

std::string hex(std::uint32_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text = "0x";
    for (int shift = 28; shift >= 0; shift -= 4) {
        text += digits[(value >> shift) & 0xFu];
    }
    return text;
}

[[noreturn]] void fail(Code code, const char* operation, std::string detail) {
    throw PlatformError(code, operation, std::move(detail));
}

[[noreturn]] void not_yet(const char* operation) {
    fail(Code::unsupported, operation, "not implemented by the Windows adapter yet");
}

// UTF-8 to UTF-16, strictly: a refusal is path.
std::wstring wide(const char* operation, std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        fail(Code::path, operation, "longer than the conversion takes");
    }
    const int size = static_cast<int>(text.size());
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, nullptr, 0);
    if (n <= 0) {
        fail(Code::path, operation, "not UTF-8: " + error_name(GetLastError()));
    }
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, out.data(), n);
    return out;
}

// UTF-16 to UTF-8, strictly: a refusal is code.
std::string narrow(Code code, const char* operation, std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = static_cast<int>(text.size());
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, nullptr, 0,
                                      nullptr, nullptr);
    if (n <= 0) {
        fail(code, operation, "not valid UTF-16: " + error_name(GetLastError()));
    }
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), size, out.data(), n, nullptr,
                        nullptr);
    return out;
}

bool control(wchar_t c) { return c < 0x20 || (c >= 0x7F && c <= 0x9F); }

bool starts(std::wstring_view text, std::wstring_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

// v3's absolute rule: the first three characters, '/' as '\', start "\\"
// or hold ":\" at the second.
bool absolute(std::wstring_view path) {
    std::wstring head(path.substr(0, 3));
    std::replace(head.begin(), head.end(), L'/', kSep);
    return starts(head, L"\\\\") || (head.size() == 3 && head[1] == L':' && head[2] == kSep);
}

// The drive, the root and the rest, as ntpath's splitroot gives them.
std::tuple<std::wstring, std::wstring, std::wstring> split_root(const std::wstring& p) {
    std::wstring n = p;
    std::replace(n.begin(), n.end(), L'/', kSep);
    if (n.size() >= 1 && n[0] == kSep) {
        if (n.size() >= 2 && n[1] == kSep) {
            std::wstring upper = n.substr(0, 8);
            std::transform(upper.begin(), upper.end(), upper.begin(),
                           [](wchar_t c) { return (c >= L'a' && c <= L'z') ? wchar_t(c - 32) : c; });
            const std::size_t from = upper == L"\\\\?\\UNC\\" ? 8 : 2;
            const std::size_t index = n.find(kSep, from);
            if (index == std::wstring::npos) {
                return {p, L"", L""};
            }
            const std::size_t index2 = n.find(kSep, index + 1);
            if (index2 == std::wstring::npos) {
                return {p, L"", L""};
            }
            return {p.substr(0, index2), p.substr(index2, 1), p.substr(index2 + 1)};
        }
        return {L"", p.substr(0, 1), p.substr(1)};
    }
    if (n.size() >= 2 && n[1] == L':') {
        if (n.size() >= 3 && n[2] == kSep) {
            return {p.substr(0, 2), p.substr(2, 1), p.substr(3)};
        }
        return {p.substr(0, 2), L"", p.substr(2)};
    }
    return {L"", L"", p};
}

// ntpath's normpath: '/' as '\', empty and '.' components dropped, '..'
// folded, a '..' at the root dropped.
std::wstring normal(std::wstring path) {
    std::replace(path.begin(), path.end(), L'/', kSep);
    auto [drive, root, rest] = split_root(path);
    std::vector<std::wstring> parts;
    std::size_t at = 0;
    while (at <= rest.size()) {
        std::size_t end = rest.find(kSep, at);
        if (end == std::wstring::npos) {
            end = rest.size();
        }
        std::wstring part = rest.substr(at, end - at);
        at = end + 1;
        if (part.empty() || part == L".") {
            continue;
        }
        if (part == L".." && !parts.empty() && parts.back() != L"..") {
            parts.pop_back();
        } else if (part != L".." || !parts.empty() || root.empty()) {
            parts.push_back(part);
        }
    }
    std::wstring out = drive + root;
    if (out.empty() && parts.empty()) {
        return L".";
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += kSep;
        }
        out += parts[i];
    }
    return out;
}

std::wstring join(const std::wstring& head, const std::wstring& tail) {
    if (head.empty()) {
        return tail;
    }
    if (is_sep(head.back()) || (head.size() == 2 && head[1] == L':')) {
        return head + tail;
    }
    return head + kSep + tail;
}

// ntpath's split: the head, its trailing separators stripped, and the last
// component.
std::pair<std::wstring, std::wstring> split(const std::wstring& p) {
    auto [drive, root, rest] = split_root(p);
    std::size_t i = rest.size();
    while (i > 0 && !is_sep(rest[i - 1])) {
        --i;
    }
    std::wstring head = rest.substr(0, i);
    std::wstring name = rest.substr(i);
    while (!head.empty() && is_sep(head.back())) {
        head.pop_back();
    }
    return {drive + root + head, name};
}

// The final path of a present file or directory, by a handle opened with no
// access and backup semantics; the error number, 0 on success.
DWORD final_path(const std::wstring& path, std::wstring& result) {
    HANDLE handle = CreateFileW(path.c_str(), 0, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD error = 0;
    for (;;) {
        const DWORD n = GetFinalPathNameByHandleW(handle, buffer.data(),
                                                  static_cast<DWORD>(buffer.size()),
                                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n == 0) {
            error = GetLastError();
            break;
        }
        if (n < buffer.size()) {
            result.assign(buffer.data(), n);
            break;
        }
        buffer.assign(n + 1, L'\0');
    }
    CloseHandle(handle);
    return error;
}

bool on_letter(const std::wstring& path) {
    return path.size() >= 3 &&
           ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
           path[1] == L':' && path[2] == kSep;
}

// The UTF-8 text as UTF-16, by the system's conversion, which writes U+FFFD
// for an invalid sequence.
std::wstring lenient(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        fail(Code::io, "stdout", "longer than the conversion takes");
    }
    const int size = static_cast<int>(text.size());
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), size, out.data(), n);
    }
    return out;
}

// Each code point converted alone, since the call writes one '?' per UTF-16
// unit, into a buffer of the code page's widest character per unit: a first
// call's size is short for a character whose best fit is two bytes.
std::string encode(const std::wstring& text) {
    const UINT acp = GetACP();
    CPINFO info{};
    const std::size_t widest = GetCPInfo(acp, &info) && info.MaxCharSize > 0 ? info.MaxCharSize : 4;
    const bool utf8 = acp == CP_UTF8;  // takes no flag and no default character
    std::string out;
    std::string buffer(2 * widest, '\0');
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t n = (text[i] >= 0xD800 && text[i] <= 0xDBFF && i + 1 < text.size()) ? 2 : 1;
        const wchar_t unit = text[i];
        if (unit == L'\n') {
            out += "\r\n";
        } else if (n == 1 && unit < 0x80) {
            out += static_cast<char>(unit);
        } else {
            BOOL used = FALSE;
            const int wrote = WideCharToMultiByte(acp, utf8 ? 0 : WC_NO_BEST_FIT_CHARS, text.data() + i,
                                                  static_cast<int>(n), buffer.data(),
                                                  static_cast<int>(buffer.size()), utf8 ? nullptr : "?",
                                                  utf8 ? nullptr : &used);
            if (wrote <= 0 || used) {
                out += '?';
            } else {
                out.append(buffer.data(), static_cast<std::size_t>(wrote));
            }
        }
        i += n;
    }
    return out;
}

// v3's standard output: UTF-16 through WriteConsoleW on a console; on a pipe
// or a file the ANSI code page, a character it lacks one '?', never a
// look-alike; each LF as CRLF.
class StandardOutput final : public Output {
public:
    void write(std::string_view utf8) override {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
            fail(Code::io, "stdout", "no standard output handle");
        }
        const std::wstring text = lenient(utf8);
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode)) {
            std::wstring units;
            for (wchar_t unit : text) {
                if (unit == L'\n') {
                    units += L'\r';
                }
                units += unit;
            }
            std::size_t done = 0;
            while (done < units.size()) {
                const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(units.size() - done, 8192));
                DWORD wrote = 0;
                if (!WriteConsoleW(handle, units.data() + done, chunk, &wrote, nullptr) || wrote == 0) {
                    fail(Code::io, "stdout", "WriteConsoleW: " + error_name(GetLastError()));
                }
                done += wrote;
            }
            return;
        }
        const std::string bytes = encode(text);
        std::size_t done = 0;
        while (done < bytes.size()) {
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 20));
            DWORD wrote = 0;
            if (!WriteFile(handle, bytes.data() + done, chunk, &wrote, nullptr) || wrote == 0) {
                fail(Code::io, "stdout", "WriteFile: " + error_name(GetLastError()));
            }
            done += wrote;
        }
    }
};

}  // namespace

std::string error_name(unsigned long error) {
    static constexpr std::pair<unsigned long, const char*> names[] = {
        {1, "ERROR_INVALID_FUNCTION"},     {2, "ERROR_FILE_NOT_FOUND"},
        {3, "ERROR_PATH_NOT_FOUND"},       {5, "ERROR_ACCESS_DENIED"},
        {6, "ERROR_INVALID_HANDLE"},       {21, "ERROR_NOT_READY"},
        {32, "ERROR_SHARING_VIOLATION"},   {33, "ERROR_LOCK_VIOLATION"},
        {50, "ERROR_NOT_SUPPORTED"},       {53, "ERROR_BAD_NETPATH"},
        {67, "ERROR_BAD_NET_NAME"},        {80, "ERROR_FILE_EXISTS"},
        {87, "ERROR_INVALID_PARAMETER"},   {109, "ERROR_BROKEN_PIPE"},
        {122, "ERROR_INSUFFICIENT_BUFFER"}, {123, "ERROR_INVALID_NAME"},
        {161, "ERROR_BAD_PATHNAME"},       {183, "ERROR_ALREADY_EXISTS"},
        {206, "ERROR_FILENAME_EXCED_RANGE"}, {232, "ERROR_NO_DATA"},
        {1113, "ERROR_NO_UNICODE_TRANSLATION"}, {1920, "ERROR_CANT_ACCESS_FILE"},
        {1921, "ERROR_CANT_RESOLVE_FILENAME"},
    };
    for (const auto& [number, name] : names) {
        if (number == error) {
            return std::string(name) + " (" + std::to_string(error) + ")";
        }
    }
    return "Win32 error " + std::to_string(error);
}

WindowsPlatform::WindowsPlatform(std::optional<std::string> config_dir)
    : config_dir_(std::move(config_dir)), output_(std::make_unique<StandardOutput>()) {}

WindowsPlatform::~WindowsPlatform() = default;

std::string WindowsPlatform::name() const { return "windows"; }

std::string WindowsPlatform::config_dir(bool create) {
    std::wstring directory;
    std::string result;
    if (config_dir_) {
        result = *config_dir_;
        directory = wide("config_dir", result);
    } else {
        PWSTR folder = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder);
        if (FAILED(hr) || folder == nullptr) {
            CoTaskMemFree(folder);
            fail(Code::config, "config_dir",
                 "SHGetKnownFolderPath: HRESULT " + hex(static_cast<std::uint32_t>(hr)));
        }
        directory = std::wstring(folder) + L"\\qiven3";
        CoTaskMemFree(folder);
        result = narrow(Code::config, "config_dir", directory);
    }
    if (create && !CreateDirectoryW(directory.c_str(), nullptr)) {
        const DWORD error = GetLastError();
        if (error == ERROR_ALREADY_EXISTS) {
            const DWORD attributes = GetFileAttributesW(directory.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                return result;
            }
            fail(Code::exists, "config_dir", "not a directory: " + error_name(error));
        }
        fail(Code::io, "config_dir", "CreateDirectoryW: " + error_name(error));
    }
    return result;
}

// The one form: the path made normal; the final path of it, or of its
// longest present ancestor with the rest joined; "\\?\" stripped unless the
// path began with it, "\\?\UNC\" read as "\\"; on a drive letter, or path.
std::string WindowsPlatform::resolve(const std::string& path) {
    const std::wstring given = wide("resolve", path);
    if (std::any_of(given.begin(), given.end(), control)) {
        fail(Code::path, "resolve", "holds a control character");
    }
    if (!absolute(given)) {
        fail(Code::path, "resolve", "not absolute");
    }
    const std::wstring normed = normal(given);
    const bool had_prefix = starts(normed, L"\\\\?\\");
    std::wstring current = normed;
    std::wstring tail;
    std::wstring result;
    for (;;) {
        std::wstring found;
        if (final_path(current, found) == 0) {
            result = tail.empty() ? found : join(found, tail);
            break;
        }
        auto [head, name] = split(current);
        if (name.empty()) {
            result = head + tail;
            break;
        }
        tail = tail.empty() ? name : join(name, tail);
        current = head;
        if (current.empty()) {
            result = tail;
            break;
        }
    }
    if (!had_prefix) {
        if (starts(result, L"\\\\?\\UNC\\")) {
            result = L"\\\\" + result.substr(8);
        } else if (starts(result, L"\\\\?\\")) {
            result = result.substr(4);
        }
    }
    const std::string text = narrow(Code::path, "resolve", result);
    if (!on_letter(result)) {
        fail(Code::path, "resolve", "not on a drive letter once resolved: " + text);
    }
    return text;
}

Facts WindowsPlatform::facts(const std::string&) { not_yet("facts"); }

std::optional<std::string> WindowsPlatform::synced(const std::string&) { not_yet("synced"); }

void WindowsPlatform::replace(const std::string&, std::string_view, Seconds) { not_yet("replace"); }

std::unique_ptr<Staged> WindowsPlatform::stage(const std::string&) { not_yet("stage"); }

void WindowsPlatform::create(const std::string&, bool) { not_yet("create"); }

std::string WindowsPlatform::read(const std::string&) { not_yet("read"); }

std::unique_ptr<Lock> WindowsPlatform::lock(const std::string&, Seconds) { not_yet("lock"); }

std::string WindowsPlatform::program(const std::string&) { not_yet("program"); }

std::unique_ptr<Child> WindowsPlatform::start(const Command&) { not_yet("start"); }

Ran WindowsPlatform::run(const Command&, std::string_view) { not_yet("run"); }

std::string WindowsPlatform::code_page() {
    UINT page = GetConsoleOutputCP();
    if (page == 0) {
        page = GetOEMCP();
    }
    return "cp" + std::to_string(page);
}

Output& WindowsPlatform::standard_output() { return *output_; }

std::vector<Row> WindowsPlatform::residual(const std::vector<std::string>&) { not_yet("residual"); }

// The system's cryptographic generator; a failure is io, and nothing falls
// back to a weaker source.
std::string WindowsPlatform::random_bytes(std::size_t n) {
    std::string out(n, '\0');
    std::size_t done = 0;
    while (done < n) {
        const ULONG chunk = static_cast<ULONG>(std::min<std::size_t>(n - done, 1u << 30));
        const NTSTATUS status = BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data() + done),
                                                chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!BCRYPT_SUCCESS(status)) {
            fail(Code::io, "random_bytes",
                 "BCryptGenRandom: NTSTATUS " + hex(static_cast<std::uint32_t>(status)));
        }
        done += chunk;
    }
    return out;
}

std::chrono::sys_time<std::chrono::milliseconds> WindowsPlatform::now() {
    return std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
}

}  // namespace qiven3::platform::win
