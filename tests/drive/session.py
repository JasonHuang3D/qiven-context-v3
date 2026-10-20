"""Drives a drive, qiven3-drive-fake or qiven3-drive-windows: a request a
line in, a reply a line out.

A request is the operation's name and its arguments; the reply's result
comes back, or its typed error is raised as DriveError. Bytes cross as
hexadecimal and come back as bytes here; a deadline in milliseconds.

The count: every reply's arena must have held nothing once the
operation's values were gone, and a drive's last reply must give 0 bytes
held, or the test fails. A drive counting handles (the machine cases)
holds as many after the test as before it. A fault is the reply's own
(sweep).
"""

import json
import os
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

from tests.drive import binaries

# The two drives: the fake's, built on every system, and the Windows
# adapter's, built on Windows alone.
FAKE = "qiven3-drive-fake"
WINDOWS = "qiven3-drive-windows"


class DriveError(Exception):
    """A reply's error: kind "platform", "memory" or "hal" with code,
    operation and detail; "request" or "exception" with detail alone."""

    def __init__(self, error):
        super().__init__(json.dumps(error, sort_keys=True))
        self.kind = error.get("kind")
        self.code = error.get("code")
        self.operation = error.get("operation")
        self.detail = error.get("detail", "")


def wire(request):
    """A request's canonical line."""
    return (json.dumps(request, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def build_option(name, build=None):
    """A cache variable of a build directory (QIVEN3_BUILD's by default), or
    None when the build's cache does not set it."""
    build = Path(build) if build else binaries.directory("QIVEN3_BUILD")
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(name + ":"):
            return line.split("=", 1)[1].strip()
    return None


def has_faults(build=None):
    """Whether the build has QIVEN3_FAULTS on, which is off unless set."""
    value = build_option("QIVEN3_FAULTS", build)
    return value is not None and value.upper() in ("ON", "1", "TRUE", "YES", "Y")


def debug_library(build=None):
    """Whether the build uses MSVC's debug library, whose debug iterators
    give each container a proxy that its constructor from an allocator,
    noexcept, takes from the allocator: a budget or a fault met there ends
    the process, so the budgets' plants and the fault sweeps do not apply."""
    return sys.platform == "win32" and (build_option("CMAKE_BUILD_TYPE", build) or "") == "Debug"


def handle_count(pid):
    """The process's handle count, by GetProcessHandleCount; Windows alone."""
    import ctypes
    from ctypes import wintypes

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel32.GetProcessHandleCount.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
    kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
    process = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not process:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        count = wintypes.DWORD()
        if not kernel32.GetProcessHandleCount(process, ctypes.byref(count)):
            raise ctypes.WinError(ctypes.get_last_error())
        return count.value
    finally:
        kernel32.CloseHandle(process)


# The requests that make the adapter and the system's lazily made state
# (a provider's handle, the shell's for the known folder) before a handle
# count is taken: each member a case calls without starting a process or
# writing, twice, on the temporary directory, then pages taken and freed.
def warm_requests():
    where = tempfile.gettempdir()
    once = [{"op": "name"}, {"op": "now"}, {"op": "code_page"}, {"op": "random_bytes", "n": 32},
            {"op": "normcase", "path": where}, {"op": "config_dir"}, {"op": "resolve", "path": where},
            {"op": "facts", "path": where}, {"op": "synced", "path": where},
            {"op": "program", "name": "cmd"}, {"op": "residual", "paths": [where]}]
    return once + once + [{"op": "pages", "n": 1}]


def has_hal(build=None):
    """Whether the build has the HAL: QIVEN3_HAL is on unless set off."""
    value = build_option("QIVEN3_HAL", build)
    return value is None or value.upper() not in ("OFF", "0", "FALSE", "NO", "N")


class Drive:
    """One drive process, `drive` the binary's name; a context manager that
    ends it."""

    def __init__(self, *args, drive=FAKE, env=None, cwd=None, program=None, timeout=binaries.TIMEOUT,
                 count_handles=False):
        self.name = drive
        self.held = None  # the last reply's bytes held
        self.handles = None  # the handle count after warm_requests()
        self.program = Path(program) if program else binaries.path(drive)
        self.process = subprocess.Popen(
            [str(self.program), *map(str, args)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env, cwd=cwd)
        # A drive that hangs is killed, so the read below ends.
        self.watchdog = threading.Timer(timeout, self.process.kill)
        self.watchdog.daemon = True
        self.watchdog.start()
        if count_handles:
            for request in warm_requests():
                reply = self.reply(**request)
                if reply.get("ok") and request["op"] == "pages":
                    self.call("pages_free", id=reply["result"]["id"])
            self.handles = handle_count(self.process.pid)

    def __enter__(self):
        return self

    def __exit__(self, kind, *exc):
        self.close(check=kind is None)
        return False

    def close(self, check=True):
        """Ends the drive: its exit status and standard error. With check,
        a drive still running must hold no bytes and, counting handles, as
        many handles as after warm_requests(), or AssertionError."""
        problem = None
        if check and self.process.poll() is None:
            if self.held:
                problem = f"{self.name} holds {self.held} bytes at the test's end"
            elif self.handles is not None:
                now = handle_count(self.process.pid)
                if now != self.handles:
                    problem = f"{self.name} holds {now} handles at the test's end, {self.handles} before"
        try:
            if self.process.stdin and not self.process.stdin.closed:
                self.process.stdin.close()
            err = self.process.stderr.read() if self.process.stderr else b""
            status = self.process.wait(timeout=binaries.TIMEOUT)
        finally:
            self.watchdog.cancel()
            for stream in (self.process.stdout, self.process.stderr):
                if stream:
                    stream.close()
        if problem:
            raise AssertionError(problem)
        return status, err

    def reply(self, op, **args):
        """The whole reply to one request."""
        self.process.stdin.write(wire({"op": op, **args}))
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line.endswith(b"\n"):
            raise RuntimeError(f"{self.name} ended without a reply to {op}: {line!r}")
        reply = json.loads(line)
        memory = reply.get("memory")
        if memory and memory["left"] != 0:
            raise AssertionError(f"{self.name}: {op} left {memory['left']} bytes held on its arena")
        self.held = reply.get("held", self.held)
        return reply

    def call(self, op, **args):
        """The result, or None when the operation gives none; DriveError."""
        reply = self.reply(op, **args)
        if not reply.get("ok"):
            raise DriveError(reply["error"])
        return reply.get("result")

    def sweep(self, op, of, limit=1000, **args):
        """The fault sweep of `op`: its K-th allocation (`of` "allocation")
        or platform call ("platform") failed for K from 1 until a run makes
        fewer and completes. Each failed run must end in a typed error
        holding no more than before, the drive answering. Gives K at
        completion, so K - 1 runs failed, and that run's reply."""
        before = self.held or 0
        for k in range(1, limit + 1):
            reply = self.reply(op, fault={"k": k, "of": of}, **args)
            if not reply["fault"]["fired"]:
                return k, reply
            if reply.get("ok") or reply["error"]["kind"] not in ("platform", "memory", "hal"):
                raise AssertionError(f"{op}, {of} {k}: not a typed error: {reply}")
            if reply["held"] != before:
                raise AssertionError(f"{op}, {of} {k}: {reply['held']} bytes held, {before} before")
        raise AssertionError(f"{op}: {of} faults past {limit}")

    # The platform's operations used by the cases, bytes as bytes.
    def config_dir(self, create=False):
        return self.call("config_dir", create=create)

    def random_bytes(self, n):
        return bytes.fromhex(self.call("random_bytes", n=n))

    def now(self):
        return self.call("now")

    def code_page(self):
        return self.call("code_page")


def environment(**changes):
    """This process's environment with each change: a value set, None removed."""
    env = dict(os.environ)
    for name, value in changes.items():
        if value is None:
            env.pop(name, None)
        else:
            env[name] = value
    return env


def once(args, *requests, drive=FAKE, env=None):
    """Runs the drive on these requests alone: its status and raw bytes."""
    data = b"".join(wire(r) for r in requests)
    done = subprocess.run([str(binaries.path(drive)), *map(str, args)], input=data,
                          capture_output=True, env=env, timeout=binaries.TIMEOUT, check=False)
    return binaries.Ran(done.returncode, done.stdout, done.stderr)
