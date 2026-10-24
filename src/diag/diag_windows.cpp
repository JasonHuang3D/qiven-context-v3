// diag on Windows: the second stage of the entry (entry.cpp holds the
// first), the handlers, and the report: the faulting thread writes the
// cause, then hands over to the dump thread, which walks the stack by
// dbghelp, writes the minidump where QIVEN3_DUMP_DIR names, and ends the
// report; the faulting thread waits at most 5 s and ends the process.

#include <windows.h>

#include <crtdbg.h>
#include <dbghelp.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <exception>
#include <span>

#include "diag/diag.hpp"
#include "diag/events.hpp"
#include "memory/fail.hpp"

#if !defined(_M_X64)
#error "diag_windows.cpp walks x64 stacks alone"
#endif

namespace qiven3::diag {

namespace {

using detail::End;
using detail::Line;

constexpr ULONG stack_guarantee = 64 * 1024;
constexpr DWORD hand_over_ms = 5000;
// The dump thread's wait, renewed for the life of the process.
constexpr DWORD idle_ms = 10000;
constexpr std::size_t frames_max = 32;
constexpr ULONG64 dump_max = 64ULL * 1024 * 1024;
constexpr DWORD status_abort = 3;
constexpr DWORD status_parameter = 0xC0000417;  // STATUS_INVALID_CRUNTIME_PARAMETER
constexpr DWORD status_fail_fast = 0xC0000409;  // STATUS_STACK_BUFFER_OVERRUN
constexpr DWORD cpp_exception = 0xE06D7363;
constexpr std::size_t path_max = 512;

// A status and how it is reached, packed so that one exchange claims the
// report: never 0.
constexpr std::uint64_t pack(DWORD status, End end) {
    return (static_cast<std::uint64_t>(status) << 8U) | (end == End::fail_fast ? 2U : 1U);
}

// The handlers' state, at namespace scope: a handler allocates nothing.
struct State {
    std::atomic<std::uint64_t> first{0};
    LPTOP_LEVEL_EXCEPTION_FILTER previous = nullptr;
    HANDLE request = nullptr;
    HANDLE done = nullptr;
    // What the faulting thread hands over.
    HANDLE thread = nullptr;
    DWORD thread_id = 0;
    DWORD status = 0;
    EXCEPTION_RECORD record{};
    CONTEXT context{};
};
State state;

// The kept rings, null where a slot is free.
detail::Rings rings{};

void write(std::string_view bytes) noexcept {
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (err == nullptr || err == INVALID_HANDLE_VALUE) {
        return;
    }
    while (!bytes.empty()) {
        DWORD wrote = 0;
        const auto size = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1U << 16U));
        if (WriteFile(err, bytes.data(), size, &wrote, nullptr) == FALSE || wrote == 0) {
            return;
        }
        bytes.remove_prefix(wrote);
    }
}

void write(Line& line) noexcept { write(line.line()); }

[[noreturn]] void end_process(std::uint64_t packed) noexcept {
    if ((packed & 0xFFU) == 2U) {
        __fastfail(FAST_FAIL_FATAL_APP_EXIT);
    }
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(packed >> 8U));
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

// A fixed field's text, up to its first NUL.
std::string_view bounded(std::span<const char> text) noexcept {
    std::size_t n = 0;
    while (n < text.size() && text[n] != '\0') {
        ++n;
    }
    return {text.data(), n};
}

// A symbol's record with room for its name, which SYMBOL_INFO ends with.
struct Symbol {
    SYMBOL_INFO info{};
    std::array<char, 256> more{};
};

void write_frame(HANDLE process, std::size_t index, DWORD64 pc) noexcept {
    // A return address's call is the byte before it.
    const DWORD64 at = index == 0 ? pc : pc - 1;
    Line line;
    line.text("  #").number(index).text(" ");
    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof(module);
    const bool known = SymGetModuleInfo64(process, at, &module) != FALSE;
    line.text(known ? bounded(module.ModuleName) : std::string_view("?"));
    Symbol symbol{};
    symbol.info.SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol.info.MaxNameLen = static_cast<ULONG>(symbol.more.size());
    DWORD64 displacement = 0;
    if (SymFromAddr(process, at, &displacement, &symbol.info) != FALSE) {
        const std::size_t size = std::min<std::size_t>(symbol.info.NameLen, symbol.more.size());
        line.text("!").text({std::data(symbol.info.Name), size}).text("+").hex(pc - symbol.info.Address);
    } else {
        line.text("+").hex(known ? pc - module.BaseOfImage : pc);
    }
    IMAGEHLP_LINE64 where{};
    where.SizeOfStruct = sizeof(where);
    DWORD column = 0;
    if (SymGetLineFromAddr64(process, at, &column, &where) != FALSE && where.FileName != nullptr) {
        line.text(" ").text(where.FileName).text(":").number(where.LineNumber);
    }
    write(line);
}

void write_stack() noexcept {
    HANDLE process = GetCurrentProcess();
    CONTEXT context = state.context;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rsp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    std::array<DWORD64, frames_max> pcs{};
    std::size_t n = 0;
    while (n < frames_max && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, state.thread, &frame, &context,
                                         nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) != FALSE) {
        if (frame.AddrPC.Offset == 0) {
            break;
        }
        pcs[n] = frame.AddrPC.Offset;
        ++n;
    }
    Line head;
    write(head.text("fatal: stack ").number(n));
    for (std::size_t i = 0; i < n; ++i) {
        write_frame(process, i, pcs[i]);
    }
}

// The dump's path, <dir>\<binary>-<pid>.dmp, and its file name's start.
struct DumpPath {
    std::array<wchar_t, 2 * path_max> path{};
    std::size_t size = 0;
    std::size_t name = 0;
    bool add(std::wstring_view part) noexcept {
        if (part.size() >= path.size() - size) {
            return false;
        }
        for (wchar_t c : part) {
            path[size] = c;
            ++size;
        }
        return true;
    }
};

bool dump_path(DumpPath& out) noexcept {
    // The directory straight into the path, so that one frame holds two
    // buffers at most.
    const DWORD got = GetEnvironmentVariableW(L"QIVEN3_DUMP_DIR", out.path.data(), static_cast<DWORD>(path_max));
    std::array<wchar_t, path_max> self{};
    const DWORD length = GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size()));
    if (got == 0 || got >= path_max || length == 0 || length >= self.size()) {
        return false;
    }
    out.size = got;
    std::wstring_view binary(self.data(), length);
    binary.remove_prefix(binary.find_last_of(L"\\/") + 1);
    if (binary.size() > 4 && binary.substr(binary.size() - 4) == L".exe") {
        binary.remove_suffix(4);
    }
    std::array<wchar_t, 12> pid{};
    std::size_t digits = 0;
    for (DWORD id = GetCurrentProcessId(); digits == 0 || id != 0; id /= 10) {
        pid[pid.size() - 1 - digits] = static_cast<wchar_t>(L'0' + id % 10);
        ++digits;
    }
    if (!out.add(L"\\")) {
        return false;
    }
    out.name = out.size;
    return out.add(binary) && out.add(L"-")
           && out.add(std::wstring_view(pid.data(), pid.size()).substr(pid.size() - digits)) && out.add(L".dmp")
           && out.add(std::wstring_view(L"\0", 1));
}

void write_dump() noexcept {
    DumpPath path;
    Line line;
    line.text("fatal: dump ");
    HANDLE file = dump_path(path) ? CreateFileW(path.path.data(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)
                                  : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE) {
        write(line.text("none"));
        return;
    }
    EXCEPTION_POINTERS pointers{&state.record, &state.context};
    MINIDUMP_EXCEPTION_INFORMATION exception{state.thread_id, &pointers, FALSE};
    // dbghelp's flags are an int enum's: combined as unsigned.
    auto type = static_cast<MINIDUMP_TYPE>(static_cast<unsigned>(MiniDumpWithThreadInfo) |
                                           static_cast<unsigned>(MiniDumpWithIndirectlyReferencedMemory));
    BOOL wrote = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &exception, nullptr,
                                   nullptr);
    LARGE_INTEGER size{};
    if (wrote != FALSE && GetFileSizeEx(file, &size) != FALSE && static_cast<ULONG64>(size.QuadPart) > dump_max) {
        // Past the bound: the threads and their stacks alone.
        const LARGE_INTEGER start{};
        wrote = SetFilePointerEx(file, start, nullptr, FILE_BEGIN) != FALSE && SetEndOfFile(file) != FALSE
                && MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpWithThreadInfo,
                                     &exception, nullptr, nullptr) != FALSE;
    }
    CloseHandle(file);
    const std::wstring_view name = std::wstring_view(path.path.data(), path.size - 1).substr(path.name);
    write(wrote != FALSE ? line.wide(name) : line.text("none"));
}

void write_rest(bool walked) noexcept {
    if (walked) {
        write_stack();
    } else {
        write("fatal: stack 0\n");
    }
    detail::events(
        rings,
        [](std::uint64_t n) noexcept {
            Line head;
            head.text("fatal: events ").number(n);
            write(head);
        },
        [](std::string_view line) noexcept {
            write(line);
            write("\n");
        });
    if (walked) {
        write_dump();
    } else {
        write("fatal: dump none\n");
    }
    Line end;
    end.text("fatal: end ");
    if (state.status == status_abort) {
        end.number(status_abort);
    } else {
        end.hex(state.status, 8);
    }
    write(end);
}

DWORD WINAPI dump_thread(LPVOID /*unused*/) {
    DWORD waited = WAIT_TIMEOUT;
    while (waited == WAIT_TIMEOUT) {
        waited = WaitForSingleObject(state.request, idle_ms);
    }
    if (waited == WAIT_OBJECT_0) {
        write_rest(true);
        SetEvent(state.done);
    }
    return 0;
}

// The report, from the faulting thread: `record` and `context` the fault's,
// or none for a report the code asks for, whose context is this frame's.
[[noreturn]] void report(std::string_view cause, DWORD status, End end, const EXCEPTION_RECORD* record,
                         const CONTEXT* context) noexcept {
    std::uint64_t first = 0;
    if (!state.first.compare_exchange_strong(first, pack(status, end))) {
        end_process(first);
    }
    Line line;
    write(line.text("fatal: ").text(cause));
    if (context != nullptr) {
        state.context = *context;
    } else {
        RtlCaptureContext(&state.context);
    }
    if (record != nullptr) {
        state.record = *record;
    } else {
        state.record.ExceptionCode = status;
    }
    state.status = status;
    state.thread_id = GetCurrentThreadId();
    state.thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, state.thread_id);
    if (state.request == nullptr || SetEvent(state.request) == FALSE) {
        write_rest(false);
    } else {
        WaitForSingleObject(state.done, hand_over_ms);
    }
    end_process(pack(status, end));
}

void access_cause(Line& cause, const EXCEPTION_RECORD& record) noexcept {
    cause.text("access violation");
    if (record.NumberParameters >= 2) {
        const ULONG_PTR kind = record.ExceptionInformation[0];
        cause.text(kind == 0 ? " reading " : kind == 1 ? " writing " : " executing ");
        cause.hex(record.ExceptionInformation[1], 16);
    }
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* pointers) {
    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    // The C runtime's own filter sends a C++ exception to std::terminate,
    // whose handler names it.
    if (record.ExceptionCode == cpp_exception && state.previous != nullptr) {
        return state.previous(pointers);
    }
    Line cause;
    switch (record.ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
        access_cause(cause, record);
        break;
    case EXCEPTION_STACK_OVERFLOW:
        cause.text("stack overflow");
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        cause.text("integer divide by zero");
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        cause.text("illegal instruction");
        break;
    case EXCEPTION_BREAKPOINT:
        cause.text("breakpoint");
        break;
    default:
        cause.text("exception ").hex(record.ExceptionCode, 8);
        break;
    }
    report(cause.view(), record.ExceptionCode, End::abort, &record, pointers->ContextRecord);
}

void on_abort(int /*signal*/) { report("abort", status_abort, End::abort, nullptr, nullptr); }

void on_parameter(const wchar_t* expression, const wchar_t* function, const wchar_t* /*file*/, unsigned int line,
                  uintptr_t /*reserved*/) {
    Line cause;
    cause.text("invalid parameter");
    // The debug runtime names them; the release one passes none.
    if (expression != nullptr && function != nullptr) {
        cause.text(": ").wide(expression).text(", ").wide(function).text(", line ").number(line);
    }
    report(cause.view(), status_parameter, End::abort, nullptr, nullptr);
}

#ifdef _DEBUG
int __cdecl on_report(int type, char* message, int* /*result*/) {
    if (type == _CRT_WARN) {
        return FALSE;
    }
    Line cause;
    std::string_view text = message != nullptr ? std::string_view(message) : std::string_view();
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    // An assertion's report reads "<file>(<line>) : Assertion failed: <expression>".
    constexpr std::string_view marker = ") : Assertion failed: ";
    const std::size_t at = text.find(marker);
    const std::size_t open = at == std::string_view::npos ? at : text.rfind('(', at);
    if (type == _CRT_ASSERT && open != std::string_view::npos) {
        cause.text("assertion failed: ").text(text.substr(at + marker.size())).text(", ");
        cause.text(text.substr(0, open)).text(":").text(text.substr(open + 1, at - open - 1));
    } else {
        cause.text(type == _CRT_ASSERT ? "assertion failed: " : "C runtime error: ").text(text);
    }
    report(cause.view(), status_abort, End::abort, nullptr, nullptr);
}
#endif

void start_dump_thread() noexcept {
    HANDLE process = GetCurrentProcess();
    // The binary's own directory, the one symbol path: never a server.
    std::array<wchar_t, path_max> dir{};
    const DWORD length = GetModuleFileNameW(nullptr, dir.data(), static_cast<DWORD>(dir.size()));
    if (length == 0 || length >= dir.size()) {
        return;
    }
    const std::size_t cut = std::wstring_view(dir.data(), length).find_last_of(L"\\/");
    dir[cut == std::wstring_view::npos ? 0 : cut] = L'\0';
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS
                  | SYMOPT_NO_PROMPTS | SYMOPT_IGNORE_NT_SYMPATH);
    if (SymInitializeW(process, dir.data(), TRUE) == FALSE) {
        return;
    }
    state.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE request = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (state.done == nullptr || request == nullptr) {
        return;
    }
    // The thread lives as long as the process; its handle is not kept.
    HANDLE thread = CreateThread(nullptr, 0, dump_thread, nullptr, 0, nullptr);
    if (thread != nullptr) {
        CloseHandle(thread);
        state.request = request;
    }
}

}  // namespace

void keep(const log::Ring& ring) noexcept {
    if (!detail::hold(rings, ring)) {
        memory::fail_fast("diag::keep: a ring kept twice, or past 8 rings");
    }
}

void drop(const log::Ring& ring) noexcept {
    if (!detail::release(rings, ring)) {
        memory::fail_fast("diag::drop: a ring not kept");
    }
}

void thread_start() noexcept {
#ifndef QIVEN3_SANITIZED
    ULONG guarantee = stack_guarantee;
    SetThreadStackGuarantee(&guarantee);
#endif
}

void install() noexcept {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, on_report);
#endif
    _set_invalid_parameter_handler(on_parameter);
    std::set_terminate(detail::on_terminate);
#ifndef QIVEN3_SANITIZED
    std::signal(SIGABRT, on_abort);
    state.previous = SetUnhandledExceptionFilter(on_exception);
    thread_start();
    start_dump_thread();
#endif
}

namespace detail {

void fatal(std::string_view cause, End end) noexcept {
    report(cause, end == End::fail_fast ? status_fail_fast : status_abort, end, nullptr, nullptr);
}

}  // namespace detail

}  // namespace qiven3::diag
