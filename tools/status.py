"""Exit statuses by name: the one table the watcher (tools/watch.py), the
drives (tests/drive/binaries.py) and the loop (tools/loop.py) name a
status by, never as a bare number.

A status is named in one line, "status: <value> <name>: <meaning>". A
process's status is as subprocess gives it: on Windows the exit code, a
system status such as 0xC0000005 printed in hexadecimal; on Linux a
negative value for a death by a signal, printed as "signal <number>" and
named by the signal. Any other status is its number with "unnamed".

A ctest step's status is ctest's own, named by CTEST in one line, "ctest's
status: <value> <name>: <meaning>": ctest exits 8 when a test failed or
timed out, or none ran under --no-tests=error, and 1 when it could not
run. A death by a signal, or a Windows system status, stays the process's.

Run as a script, it names each status its arguments give, in decimal or
0x hexadecimal, a signal as -<number>:

    python -I tools/status.py 0xC0000135 -11
"""

import sys

# value: (name, meaning).
TABLE = {
    0: ("passed", ""),
    1: ("failed", "a case failed, or a main returned its error status"),
    3: ("abort", "Windows: the C runtime's abort"),
    124: ("hang", "the watcher's: past its limit, every stack written, killed"),
    125: ("skipped", "the runner's skip code"),
    127: ("loader", "Linux: the loader could not load a library"),
    0x80000003: ("STATUS_BREAKPOINT", "a breakpoint instruction, with no debugger"),
    0xC0000005: ("STATUS_ACCESS_VIOLATION", "a read, write or call at an address not mapped or not allowed"),
    0xC000001D: ("STATUS_ILLEGAL_INSTRUCTION", "an instruction the processor refuses"),
    0xC000007B: ("STATUS_INVALID_IMAGE_FORMAT", "a library built for another machine"),
    0xC0000094: ("STATUS_INTEGER_DIVIDE_BY_ZERO", "an integer division by zero"),
    0xC00000FD: ("STATUS_STACK_OVERFLOW", "the stack overflow"),
    0xC0000135: ("STATUS_DLL_NOT_FOUND", "a library the binary imports was not found"),
    0xC0000139: ("STATUS_ENTRYPOINT_NOT_FOUND", "a function it imports is not in its library"),
    0xC0000142: ("STATUS_DLL_INIT_FAILED", "a library's initialization failed"),
    0xC000013A: ("STATUS_CONTROL_C_EXIT", "ended by Ctrl+C"),
    0xC0000374: ("STATUS_HEAP_CORRUPTION", "the system's heap found itself corrupted"),
    0xC0000409: ("STATUS_STACK_BUFFER_OVERRUN", "the fail-fast, __fastfail: v3's, or the C runtime's"),
    0xC0000417: ("STATUS_INVALID_CRUNTIME_PARAMETER", "the end of the entry's invalid-parameter handler"),
    0xE06D7363: ("the C++ exception", "a C++ exception unhandled, with no handler of the entry's"),
}
# Linux's signals on x86-64, by number, named on every system.
SIGNALS = {11: "SIGSEGV", 7: "SIGBUS", 8: "SIGFPE", 4: "SIGILL", 6: "SIGABRT", 5: "SIGTRAP", 9: "SIGKILL",
           15: "SIGTERM", 13: "SIGPIPE"}
SIGNAL_MEANING = "Linux: a death by that signal, read from the wait status"
UNNAMED = "unnamed"
# ctest's exit statuses, as ctest 4.4.3 gives them: value: (name, meaning).
CTEST = {
    1: ("error", "ctest could not run: an argument, a preset or a test directory it cannot take"),
    8: ("tests failed", "a test failed or timed out, or none ran under --no-tests=error"),
}


def value_text(status):
    """The status as the line prints it."""
    if status < 0:
        return f"signal {-status}"
    if status > 0xFFFF:
        return f"0x{status:08X}"
    return str(status)


def named(status):
    """(name, meaning) of the table, or None for a status off it."""
    if status < 0:
        name = SIGNALS.get(-status)
        return None if name is None else (name, SIGNAL_MEANING)
    return TABLE.get(status)


def line(status):
    """The status's one line: "status: <value> <name>: <meaning>", or
    "status: <value> unnamed" off the table."""
    found = named(status)
    if found is None:
        return f"status: {value_text(status)} {UNNAMED}"
    name, meaning = found
    return f"status: {value_text(status)} {name}" + (f": {meaning}" if meaning else "")


def ctest_line(status):
    """A ctest step's status in one line: "ctest's status: <value> <name>:
    <meaning>" by CTEST, "ctest's status: <value> unnamed" off it; a death
    by a signal or a system status by line, as the process's."""
    if status < 0 or status > 0xFFFF:
        return line(status)
    name, meaning = CTEST.get(status, (UNNAMED, ""))
    return f"ctest's status: {status} {name}" + (f": {meaning}" if meaning else "")


def parse(text):
    """A status from its decimal or 0x hexadecimal text."""
    return int(text, 16) if text.lower().startswith("0x") else int(text)


def main(argv):
    if not argv:
        print("usage: status.py STATUS...", file=sys.stderr)
        return 2
    try:
        statuses = [parse(a) for a in argv]
    except ValueError as e:
        print(f"status.py: {e}", file=sys.stderr)
        return 2
    for status in statuses:
        print(line(status))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
