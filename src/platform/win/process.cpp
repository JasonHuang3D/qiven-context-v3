// win/process (platform/win/windows.hpp): program, start and the Child's
// process tree on the Windows adapter, the one module that starts a
// process. start builds its inputs on the adapter's scratch, and takes
// the Child's pages for its state and streams before any pipe (child.cpp):
// the attribute list on the scratch's first bytes, then
// in the first path buffer argv[0], the working directory and the
// environment block, and in the second the command line. The child is
// created suspended, in a new Job Object from its creation (the attribute
// list's job list), with no window and a Unicode environment, inheriting
// its three pipe ends alone; then its thread resumed and the Child's
// threads started. The Child's members, which end the tree by its Job
// Object, and run are child.cpp's.
//
// Each Win32 call asks the adapter's countdown first and, when it fires,
// fails as the call would with error 31 (ERROR_GEN_FAILURE); a handle the
// operation opened is closed first, a job so closed killing its tree.
//
// thread: one (the adapter's)

#include "platform/win/windows.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

#include "memory/fail.hpp"
#include "platform/win/paths.hpp"

namespace qiven3::platform::win {
namespace {

using paths::units;

// The attribute list's room, the scratch's first bytes, before the first
// path buffer's other units; a list of two attributes takes 72 bytes on
// x64 here, one 48.
constexpr std::size_t attribute_bytes = 128;
constexpr std::size_t attribute_units = attribute_bytes / sizeof(wchar_t);
// Each pipe's buffer, a hint the system takes.
constexpr DWORD pipe_bytes = 65536;
// The names v3's own environment gives every child, the caller's pair of
// the same name taking its place.
constexpr std::array<const wchar_t*, 4> allowlist{L"SystemRoot", L"SystemDrive", L"TEMP", L"TMP"};

[[noreturn]] void refuse(std::string_view operation, std::string_view detail) {
    throw PlatformError(Code::start, operation, detail);
}

[[noreturn]] void failed(Code code, std::string_view operation, std::string_view call, unsigned long error) {
    Detail detail(call);
    detail.add(": ");
    add_error(detail, error);
    throw PlatformError(code, operation, detail);
}

// An argument's refusal, naming its index.
[[noreturn]] void refuse_argument(std::size_t index, std::string_view what) {
    Detail detail("argv[");
    detail.number(index).add("] ").add(what);
    throw PlatformError(Code::start, "start", detail);
}

// A handle closed at its scope's end unless released.
class Owned {
public:
    Owned() noexcept = default;
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    Owned(Owned&&) = delete;
    Owned& operator=(Owned&&) = delete;
    ~Owned() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    HANDLE* put() noexcept { return &handle_; }
    HANDLE release() noexcept { return std::exchange(handle_, nullptr); }

private:
    HANDLE handle_ = nullptr;
};

// An attribute list deleted at its scope's end once initialized.
class Attributes {
public:
    Attributes() noexcept = default;
    Attributes(const Attributes&) = delete;
    Attributes& operator=(const Attributes&) = delete;
    Attributes(Attributes&&) = delete;
    Attributes& operator=(Attributes&&) = delete;
    ~Attributes() {
        if (list_ != nullptr) {
            DeleteProcThreadAttributeList(list_);
        }
    }
    void held(LPPROC_THREAD_ATTRIBUTE_LIST list) noexcept { list_ = list; }

private:
    LPPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

enum class Put : std::uint8_t { done, full, bad };

// c at `at`, keeping `into`'s last unit for a null: false past it.
bool put(std::span<wchar_t> into, std::size_t& at, wchar_t c) noexcept {
    if (at + 1 >= into.size()) {
        return false;
    }
    into[at++] = c;
    return true;
}

bool put(std::span<wchar_t> into, std::size_t& at, std::span<const wchar_t> text) noexcept {
    return std::ranges::all_of(text, [&](wchar_t c) { return put(into, at, c); });
}

bool put_slashes(std::span<wchar_t> into, std::size_t& at, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        if (!put(into, at, L'\\')) {
            return false;
        }
    }
    return true;
}

// UTF-8 as UTF-16 at `at`, strictly: full past `into`'s last unit, bad
// where the text is not UTF-8.
Put put(std::span<wchar_t> into, std::size_t& at, std::string_view text) noexcept {
    if (text.empty()) {
        return Put::done;
    }
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        return Put::full;
    }
    const int n =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (n <= 0) {
        return Put::bad;
    }
    if (at + static_cast<std::size_t>(n) >= into.size()) {
        return Put::full;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        into.subspan(at).data(), n);
    at += static_cast<std::size_t>(n);
    return Put::done;
}

wchar_t fold(wchar_t c) noexcept { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c; }

bool ends_with(std::span<const wchar_t> text, std::wstring_view end) noexcept {
    return text.size() >= end.size() &&
           std::ranges::equal(text.last(end.size()), end, [](wchar_t x, wchar_t y) { return fold(x) == fold(y); });
}

// An environment name the caller gives, against an allowlist name, ASCII
// case folded.
bool same_name(std::string_view given, std::wstring_view name) noexcept {
    return given.size() == name.size() && std::ranges::equal(given, name, [](char x, wchar_t y) {
               return fold(static_cast<wchar_t>(static_cast<unsigned char>(x))) == fold(y);
           });
}

// argv quoted by the C runtime's rules, as Python's list2cmdline quotes
// it, into `into`, ended by a null. An argument holding a null, or not
// UTF-8, or a line past 32,767 units is start.
void command_line(std::span<const std::string_view> argv, std::span<wchar_t> into) {
    std::size_t at = 0;
    bool fits = true;
    for (std::size_t i = 0; fits && i < argv.size(); ++i) {
        const std::string_view arg = argv[i];
        if (arg.find('\0') != std::string_view::npos) {
            refuse_argument(i, "holds a null");
        }
        const bool quote = arg.empty() || arg.find_first_of(" \t") != std::string_view::npos;
        fits = (i == 0 || put(into, at, L' ')) && (!quote || put(into, at, L'"'));
        std::size_t slashes = 0;
        for (std::size_t k = 0; fits && k < arg.size();) {
            if (arg[k] == '\\') {
                ++slashes;
                ++k;
            } else if (arg[k] == '"') {
                fits = put_slashes(into, at, 2 * slashes + 1) && put(into, at, L'"');
                slashes = 0;
                ++k;
            } else {
                // A run up to the next backslash or quote: ASCII, so never
                // inside a character's UTF-8.
                const std::size_t end = std::min(arg.find_first_of("\\\"", k), arg.size());
                fits = put_slashes(into, at, slashes);
                slashes = 0;
                const Put done = fits ? put(into, at, arg.substr(k, end - k)) : Put::full;
                if (done == Put::bad) {
                    refuse_argument(i, "is not UTF-8");
                }
                fits = done == Put::done;
                k = end;
            }
        }
        fits = fits && put_slashes(into, at, quote ? 2 * slashes : slashes) && (!quote || put(into, at, L'"'));
    }
    if (!fits) {
        refuse("start", "the command line is past 32,767 UTF-16 units");
    }
    into[at] = L'\0';
}

[[noreturn]] void past_scratch() {
    refuse("start", "argv[0], cwd and the environment past the adapter's 32,703 UTF-16 units");
}

// The environment block at `at`: each allowlist name v3's own environment
// holds and the caller does not give, then the caller's pairs, each
// "name=value" and a null, then a null; an empty block two nulls.
void environment(WindowsFiles& files, std::span<const std::pair<std::string_view, std::string_view>> env,
                 std::span<wchar_t> into, std::size_t at) {
    const std::size_t first = at;
    for (const wchar_t* const allowed : allowlist) {
        const std::wstring_view name(allowed);
        if (std::ranges::any_of(env, [&](const auto& pair) { return same_name(pair.first, name); })) {
            continue;
        }
        const std::size_t from = at;
        if (!put(into, at, std::span<const wchar_t>(name)) || !put(into, at, L'=') || at + 2 >= into.size()) {
            past_scratch();
        }
        const auto room = static_cast<DWORD>(std::min<std::size_t>(into.size() - at - 1, MAXDWORD));
        const bool fault = files.faulted();
        SetLastError(ERROR_SUCCESS);
        const DWORD n = fault ? 0 : GetEnvironmentVariableW(allowed, into.subspan(at).data(), room);
        const DWORD error = fault ? ERROR_GEN_FAILURE : (n == 0 ? GetLastError() : 0);
        if (error == ERROR_ENVVAR_NOT_FOUND) {
            at = from;
            continue;
        }
        if (error != 0) {
            failed(Code::start, "start", "GetEnvironmentVariableW", error);
        }
        at += n;
        if (n >= room || !put(into, at, L'\0')) {
            past_scratch();
        }
    }
    for (const auto& pair : env) {
        const std::string_view name = pair.first;
        const std::string_view value = pair.second;
        if (name.empty() || name.find_first_of(std::string_view("=\0", 2)) != std::string_view::npos ||
            value.find('\0') != std::string_view::npos) {
            refuse("start", "an environment name empty or holding '=' or a null, or a value holding a null");
        }
        const Put a = put(into, at, name);
        const Put b = a == Put::done && put(into, at, L'=') ? put(into, at, value) : Put::full;
        if (a == Put::bad || b == Put::bad) {
            refuse("start", "an environment pair is not UTF-8");
        }
        if (b != Put::done || !put(into, at, L'\0')) {
            past_scratch();
        }
    }
    if (at == first && !put(into, at, L'\0')) {
        past_scratch();
    }
    into[at] = L'\0';  // put keeps the last unit for it
}

void pipe(WindowsFiles& files, Owned& read, Owned& write) {
    if (files.faulted()) {
        failed(Code::start, "start", "CreatePipe", ERROR_GEN_FAILURE);
    }
    if (CreatePipe(read.put(), write.put(), nullptr, pipe_bytes) == 0) {
        failed(Code::start, "start", "CreatePipe", GetLastError());
    }
}

// The child's end of a pipe, inheritable, as the handle list names it.
void inherit(WindowsFiles& files, const Owned& end) {
    const bool fault = files.faulted();
    if (fault || SetHandleInformation(end.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) == 0) {
        failed(Code::start, "start", "SetHandleInformation", fault ? ERROR_GEN_FAILURE : GetLastError());
    }
}

// An attribute naming handles, which last until CreateProcessW.
void update(WindowsFiles& files, LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD_PTR attribute, std::span<HANDLE> handles) {
    const bool fault = files.faulted();
    if (fault || UpdateProcThreadAttribute(list, 0, attribute, std::as_writable_bytes(handles).data(),
                                           handles.size_bytes(), nullptr, nullptr) == 0) {
        failed(Code::start, "start", "UpdateProcThreadAttribute", fault ? ERROR_GEN_FAILURE : GetLastError());
    }
}

}  // namespace

// --- program ---

// <name>.exe in the first absolute entry of v3's own PATH that holds it as
// a file, resolved: PATH in the second path buffer, the name's units after
// it, each candidate in the first.
void WindowsPlatform::program(std::string_view name, String& out) {
    out.clear();
    if (name.empty() || name.find_first_of("\\/:") != std::string_view::npos ||
        std::ranges::any_of(name, [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7F; })) {
        refuse("program", "not a program's name");
    }
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units);
    const std::span<wchar_t> b = all.subspan(units, units);
    const bool fault = files_.faulted();
    SetLastError(ERROR_SUCCESS);
    const std::size_t n = fault ? 0 : GetEnvironmentVariableW(L"PATH", b.data(), static_cast<DWORD>(units));
    const DWORD error = fault ? ERROR_GEN_FAILURE : (n == 0 ? GetLastError() : 0);
    if (error != 0 && error != ERROR_ENVVAR_NOT_FOUND) {
        failed(Code::start, "program", "GetEnvironmentVariableW", error);
    }
    if (n >= units) {
        refuse("program", "PATH past 32,767 UTF-16 units");
    }
    std::size_t at = n + 1;
    const Put given = put(b, at, name);
    if (given == Put::bad) {
        refuse("program", "not a program's name: not UTF-8");
    }
    if (given != Put::done || !put(b, at, std::span<const wchar_t>(std::wstring_view(L".exe")))) {
        refuse("program", "PATH and the name past 32,767 UTF-16 units");
    }
    const std::span<const wchar_t> file = b.subspan(n + 1, at - n - 1);
    for (std::size_t from = 0; from < n;) {
        std::size_t end = from;
        while (end < n && b[end] != L';') {
            ++end;
        }
        const std::span<const wchar_t> entry = b.subspan(from, end - from);
        from = end + 1;
        if (entry.empty() || !paths::absolute(entry)) {
            continue;
        }
        std::size_t m = 0;
        const bool separated = entry.back() == L'\\' || entry.back() == L'/';
        if (!put(a, m, entry) || (!separated && !put(a, m, L'\\')) || !put(a, m, file)) {
            continue;
        }
        a[m] = L'\0';
        const DWORD attributes = files_.faulted() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(a.data());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        try {
            resolve_units(m, out);
        } catch (const PlatformError& refused) {
            throw PlatformError(Code::start, "program", refused.detail.view());
        }
        return;
    }
    Detail detail("no ");
    detail.add(name).add(".exe on an absolute entry of PATH");
    throw PlatformError(Code::start, "program", detail);
}

// --- start ---

WindowsChild WindowsPlatform::start(const Command& command) {
    const std::span<const std::string_view> argv = command.argv;
    if (argv.empty()) {
        refuse("start", "an empty argv");
    }
    const std::span<wchar_t> all = paths::scratch(scratch_);
    const std::span<wchar_t> a = all.first(units).subspan(attribute_units);
    const std::span<wchar_t> b = all.subspan(units, units);

    // argv[0], the application's name: absolute, no shell's.
    std::size_t at = 0;
    const Put first = put(a, at, argv[0]);
    if (first == Put::bad) {
        refuse("start", "argv[0] is not UTF-8");
    }
    if (first == Put::full) {
        past_scratch();
    }
    a[at] = L'\0';
    const std::span<const wchar_t> application = a.first(at);
    if (!paths::absolute(application)) {
        refuse("start", "argv[0] is not absolute");
    }
    if (ends_with(application, L".cmd") || ends_with(application, L".bat")) {
        refuse("start", "argv[0] is a .cmd or a .bat: no shell runs it");
    }
    ++at;
    // The working directory, absolute; none given, v3's own.
    const std::size_t directory = at;
    if (!command.cwd.empty()) {
        const Put cwd = put(a, at, command.cwd);
        if (cwd == Put::bad) {
            refuse("start", "cwd is not UTF-8");
        }
        if (cwd == Put::full) {
            past_scratch();
        }
        a[at] = L'\0';
        if (!paths::absolute(a.subspan(directory, at - directory))) {
            refuse("start", "cwd is not absolute");
        }
        ++at;
    }
    const std::size_t block = at;
    environment(files_, command.env, a, at);
    command_line(argv, b);
    WindowsChild child(command.caps, files_);

    Owned in_read;
    Owned in_write;
    Owned out_read;
    Owned out_write;
    Owned err_read;
    Owned err_write;
    pipe(files_, in_read, in_write);
    pipe(files_, out_read, out_write);
    pipe(files_, err_read, err_write);
    inherit(files_, in_read);
    inherit(files_, out_write);
    inherit(files_, err_write);

    Owned job;
    if (files_.faulted()) {
        failed(Code::start, "start", "CreateJobObjectW", ERROR_GEN_FAILURE);
    }
    *job.put() = CreateJobObjectW(nullptr, nullptr);
    if (job.get() == nullptr) {
        failed(Code::start, "start", "CreateJobObjectW", GetLastError());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const bool limit_fault = files_.faulted();
    if (limit_fault ||
        SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) == 0) {
        failed(Code::start, "start", "SetInformationJobObject", limit_fault ? ERROR_GEN_FAILURE : GetLastError());
    }

    // The attribute list: the three pipe ends alone inherited, the job the
    // child's from its creation.
    SIZE_T size = 0;
    if (files_.faulted()) {
        failed(Code::start, "start", "InitializeProcThreadAttributeList", ERROR_GEN_FAILURE);
    }
    InitializeProcThreadAttributeList(nullptr, 2, 0, &size);  // the size alone: 122 by design
    if (size == 0 || size > attribute_bytes) {
        refuse("start", "InitializeProcThreadAttributeList: a list past 128 bytes");
    }
    const auto list = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(scratch_.address());
    Attributes attributes;
    const bool list_fault = files_.faulted();
    if (list_fault || InitializeProcThreadAttributeList(list, 2, 0, &size) == 0) {
        failed(Code::start, "start", "InitializeProcThreadAttributeList",
               list_fault ? ERROR_GEN_FAILURE : GetLastError());
    }
    attributes.held(list);
    std::array<HANDLE, 3> inherited{in_read.get(), out_write.get(), err_write.get()};
    std::array<HANDLE, 1> jobs{job.get()};
    update(files_, list, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited);
    update(files_, list, PROC_THREAD_ATTRIBUTE_JOB_LIST, jobs);

    if (!error_mode_) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        error_mode_ = true;
    }
    STARTUPINFOEXW info{};
    info.StartupInfo.cb = sizeof(info);
    info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    info.StartupInfo.hStdInput = in_read.get();
    info.StartupInfo.hStdOutput = out_write.get();
    info.StartupInfo.hStdError = err_write.get();
    info.lpAttributeList = list;
    PROCESS_INFORMATION started{};
    constexpr DWORD flags =
        CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT;
    const bool create_fault = files_.faulted();
    if (create_fault ||
        CreateProcessW(a.data(), b.data(), nullptr, nullptr, TRUE, flags, a.subspan(block).data(),
                       command.cwd.empty() ? nullptr : a.subspan(directory).data(), &info.StartupInfo,
                       &started) == 0) {
        failed(Code::start, "start", "CreateProcessW", create_fault ? ERROR_GEN_FAILURE : GetLastError());
    }
    Owned process;
    Owned thread;
    *process.put() = started.hProcess;
    *thread.put() = started.hThread;
    // On a failure here the job's handle closes and so kills the child.
    const bool resume_fault = files_.faulted();
    if (resume_fault || ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        failed(Code::start, "start", "ResumeThread", resume_fault ? ERROR_GEN_FAILURE : GetLastError());
    }
    // On a failure here the Child's destructor ends the tree.
    child.begin(process.release(), job.release(), in_write.release(), out_read.release(), err_read.release(),
                command.deadline);
    return child;
}

}  // namespace qiven3::platform::win
