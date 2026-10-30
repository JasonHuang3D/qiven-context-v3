/* The counting shim on Windows (tools/loop_shim.py builds it with the
   toolset's cl and places it first on PATH under git's program name):
   each start appends one line, its command line, to the file
   QIVEN3_SHIM_LOG names, then runs the program QIVEN3_SHIM_REAL names with
   the same arguments and the same standard handles, and exits with its
   status. A start with either variable unset, or that cannot log, exits 125
   with a line on standard error, so that no start goes uncounted. Not a
   part of the product: the loop's own tool, never built by CMake. */
#include <windows.h>

static int fail(const char* text) {
    DWORD wrote = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, lstrlenA(text), &wrote, NULL);
    return 125;
}

int main(void) {
    static wchar_t log_path[32768], real[32768];
    if (!GetEnvironmentVariableW(L"QIVEN3_SHIM_LOG", log_path, 32768) ||
        !GetEnvironmentVariableW(L"QIVEN3_SHIM_REAL", real, 32768))
        return fail("shim: QIVEN3_SHIM_LOG or QIVEN3_SHIM_REAL unset\n");
    const wchar_t* line = GetCommandLineW();
    const wchar_t* rest = line;  /* the arguments: the command line past the program's name */
    if (*rest == L'"') {
        for (++rest; *rest && *rest != L'"'; ++rest) {
        }
        if (*rest) ++rest;
    } else {
        while (*rest && *rest != L' ' && *rest != L'\t') ++rest;
    }
    HANDLE log = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE) return fail("shim: the log cannot be opened\n");
    static char entry[65536 * 3];
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, entry, (int)sizeof entry - 1, NULL, NULL);
    DWORD wrote = 0;
    if (n <= 0) n = 1;
    for (int i = 0; i < n - 1; ++i)
        if (entry[i] == '\r' || entry[i] == '\n') entry[i] = ' ';
    entry[n - 1] = '\n';  /* one line a start, written by one append */
    BOOL logged = WriteFile(log, entry, (DWORD)n, &wrote, NULL) && wrote == (DWORD)n;
    CloseHandle(log);
    if (!logged) return fail("shim: the log cannot be written\n");
    static wchar_t command[32768 + 32770];
    lstrcpyW(command, L"\"");
    lstrcatW(command, real);
    lstrcatW(command, L"\"");
    if (lstrlenW(command) + lstrlenW(rest) >= 32767) return fail("shim: the command line is too long\n");
    lstrcatW(command, rest);
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        return fail("shim: the real program cannot start\n");
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}
