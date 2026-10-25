"""Drives a drive, qiven3-drive-fake or qiven3-drive-windows: a request a
line in, a reply a line out.

A request is the operation's name and its arguments; the reply's result
comes back, or its typed error is raised as DriveError. Bytes cross as
hexadecimal and come back as bytes here; a deadline in milliseconds.

The count: every reply's arena must have held nothing once the
operation's values were gone, and a drive's last reply must give 0 bytes
held, or the test fails. A drive counting handles (the machine cases)
holds as many after the test as before it. A fault is the reply's own
(sweep); facts' and residual's failed reads are unknown (unknown_read).
"""

import json
import os
import re
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

# The operations whose failed read is unknown, not an error: a platform
# call of theirs failed may answer. A fault fails its call with error 31,
# a COM call's as its HRESULT; a reading's reason then names the call.
UNKNOWN_READS = ("facts", "residual")
FAULTED_REASON = re.compile(r"[A-Za-z][\w:]*: (HRESULT 0x8007001f, )?ERROR_GEN_FAILURE \(31\)")
READINGS = ("protection", "file_history", "bitlocker")


def unknown_read(op, run, faulted, completed):
    """A fault's answer against the completed one. facts: the same volume,
    one reading unlike the completed one's, unknown, its reason naming the
    failed call and the fault's error. residual: the same rows, each as
    completed or unknown, at most one unlike; a row holds no reason, so a
    row read unknown either way is not told apart."""
    if op == "facts":
        unlike = [key for key in READINGS if faulted[key] != completed[key]]
        if (faulted["volume"] != completed["volume"] or len(unlike) != 1
                or faulted[unlike[0]]["state"] != "unknown"
                or not FAULTED_REASON.fullmatch(faulted[unlike[0]]["reason"])):
            raise AssertionError(f"{run}: not an unknown naming the failed call: {faulted}, completed {completed}")
        return
    names = [row["name"] for row in completed]
    unlike = [row for row, done in zip(faulted, completed, strict=False) if row != done]
    if ([row["name"] for row in faulted] != names or len(unlike) > 1
            or any(row["applies"] != "unknown" for row in unlike)):
        raise AssertionError(f"{run}: not the rows with one read unknown: {faulted}, completed {completed}")


class DriveError(Exception):
    """A reply's error: kind "platform", "memory" or "hal" with code,
    operation and detail; "store" with its class, operation, path, detail,
    its code where its kind has one and its kind's fields, all in error;
    "request" or "exception" with detail alone."""

    def __init__(self, error):
        super().__init__(json.dumps(error, sort_keys=True))
        self.error = error
        self.kind = error.get("kind")
        self.code = error.get("code")
        self.operation = error.get("operation")
        self.detail = error.get("detail", "")
        self.cls = error.get("class")


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
        holding no more than before, the drive answering; but a platform
        call of facts or residual, whose failed read is unknown: there an
        answer is a failed run's end too where unknown_read() holds it
        against the completed answer. Gives K at completion, so K - 1 runs
        failed, and that run's reply."""
        before = self.held or 0
        answered = []
        for k in range(1, limit + 1):
            reply = self.reply(op, fault={"k": k, "of": of}, **args)
            if not reply["fault"]["fired"]:
                for at, given in answered:
                    unknown_read(op, f"{op}, {of} {at}", given["result"], reply["result"])
                return k, reply
            if reply.get("ok") and of == "platform" and op in UNKNOWN_READS:
                answered.append((k, reply))
            elif reply.get("ok") or reply["error"]["kind"] not in ("platform", "memory", "hal"):
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

    # The store's requests (driver/store_ops.hpp), a drive started with
    # --store: the factory's give a StoreObject.
    def store_capabilities(self):
        return self.call("store_capabilities")

    def store_make(self, limits=None, heap=None):
        """A new store; heap, the in-memory state's bytes where given."""
        given = ({} if limits is None else {"limits": limits}) | ({} if heap is None else {"heap": heap})
        made = self.call("factory_make", **given)
        return StoreObject(self, made["store"], made["state"], made.get("path"), made.get("directory"))

    def store_again(self, state):
        return StoreObject(self, self.call("factory_again", state=state)["store"], state)

    def store_read_only(self, state):
        return StoreObject(self, self.call("factory_read_only", state=state)["store"], state)

    def store_reopen(self, state):
        return StoreObject(self, self.call("factory_reopen", state=state)["store"], state)

    def store_create(self, state):
        """A create over the SQLite state's file, which is there."""
        return StoreObject(self, self.call("factory_create", state=state)["store"], state)

    def store_open_at(self, path, create=False, read_only=False, refuse=()):
        """An open of the SQLite file at path, under the drive's Site root,
        its object closed at once: {"made": True} or {"error"}; refuse, each
        (of, k), countdowns armed for it (platform, arena, engine_call,
        engine_heap), given back as "refused" [{of, calls, fired}]."""
        given = [{"of": of, "k": k} for of, k in refuse]
        args = {"refuse": given} if given else {}
        return self.call("store_open_at", path=path, create=create, read_only=read_only, **args)

    def store_import_undeferred(self, state, source):
        """An import into the SQLite state's file through the seam whose
        import does not defer foreign keys; source as StoreObject.import_'s."""
        if isinstance(source, StoreExport):
            return self.call("store_import_undeferred", state=state, export=source.id)
        heads, lines = source
        return self.call("store_import_undeferred", state=state, heads=list(heads),
                         lines=[line_wire(x) for x in lines])

    def store_appender(self, state, batches, hold=0):
        """A thread of the drive's appending through a store object of its
        own over the state: batches, each (scope, lane, expected, entries),
        in order; after its first hold batches it holds, and this returns
        once it does, until release() or join()."""
        given = [{"scope": scope, "lane": lane, "entries": [entry_wire(e) for e in entries],
                  **StoreObject._expected(expected)} for scope, lane, expected, entries in batches]
        return StoreAppender(self, self.call("store_appender", state=state, batches=given, hold=hold)["appender"])

    def store_race(self, state, scope, threads, count, content, pause_ms=5):
        """Threads of the drive's, each with a store object of its own over
        the state, started together, each making count successful
        conditional appends on its lane's head: threads, (lane, writer)
        each; content with "{thread}" and "{number}" marked. Each thread's
        {"ids", "conflicts", "busy", "error"?}, in order."""
        given = [{"lane": lane, "writer": writer} for lane, writer in threads]
        return self.call("store_race", state=state, scope=scope, threads=given, count=count, content=content,
                         pause_ms=pause_ms)["threads"]

    def state_drop(self, state):
        self.call("factory_drop", state=state)

    def fixture_entry(self, name):
        """A fixture's stored line and body record (None for none), bytes."""
        got = self.call("fixture_entry", name=name)
        return got_bytes(got, "line"), got_bytes(got, "body")

    def fixture_made(self, like, fields=None, absent=(), content=None, key_of=None):
        """An entry made like a fixture, (line, body or None) as bytes: its
        header's fields set, those of `absent` removed ("content" for no
        content and no body), its content (text) the fixture's where None,
        under key_of's key; its digest and id computed, nothing checked."""
        args = {"like": like}
        if fields:
            args["fields"] = dict(fields)
        if absent:
            args["absent"] = list(absent)
        if content is not None:
            args["content"] = content
        if key_of is not None:
            args["key_of"] = key_of
        got = self.call("fixture_made", **args)
        return got_bytes(got, "line"), got_bytes(got, "body")


def got_bytes(result, key):
    """A result's hexadecimal member as bytes, or None when absent."""
    return bytes.fromhex(result[key]) if key in result else None


def entry_wire(entry):
    """An entry, (line, body or None) as bytes, on the wire."""
    line, body = entry
    return {"line": line.hex(), **({} if body is None else {"body": body.hex()})}


def line_wire(line):
    """An import's line, {"line", "body", "forgotten"} as bytes, on the wire."""
    return {**entry_wire((line["line"], line.get("body"))), "forgotten": line.get("forgotten", False)}


def line_of(result):
    """An export's line from the wire: line and body (or None) as bytes."""
    return {"line": got_bytes(result, "line"), "body": got_bytes(result, "body"), "forgotten": result["forgotten"]}


class StoreObject:
    """A store object of a drive, by its id; state, the id of its state;
    path and directory, the SQLite file's, None on the in-memory store.
    Each method is the contract's operation; an entry is (line, body or
    None), bytes; a typed error raises DriveError."""

    def __init__(self, drive, id, state, path=None, directory=None):
        self.drive, self.id, self.state, self.path, self.directory = drive, id, state, path, directory

    def call(self, op, **args):
        return self.drive.call(op, store=self.id, **args)

    @staticmethod
    def _expected(expected):
        return {} if expected is None else {"expected": expected}

    def append(self, scope, lane, expected, entries):
        return self.call("store_append", scope=scope, lane=lane, entries=[entry_wire(e) for e in entries],
                         **self._expected(expected))

    def head(self, scope, lane):
        return self.call("store_head", scope=scope, lane=lane)

    def heads(self, scopes=None):
        return self.call("store_heads", **({} if scopes is None else {"scopes": list(scopes)}))

    def header(self, id):
        return bytes.fromhex(self.call("store_header", id=id))

    def body(self, id):
        got = self.call("store_body", id=id)
        return None if got is None else bytes.fromhex(got)

    def forgotten(self, id):
        return self.call("store_forgotten", id=id)

    def scan(self, scope=None, lane=None, after=None):
        args = {k: v for k, v in (("scope", scope), ("lane", lane), ("after", after)) if v is not None}
        return StoreScan(self.drive, self.call("store_scan", **args)["scan"])

    def forget(self, scope, lane, expected, entries, guard):
        """guard: (scope, lane, id) of every head the store holds."""
        return self.call("store_forget", scope=scope, lane=lane, entries=[entry_wire(e) for e in entries],
                         guard=[{"scope": s, "lane": l, "id": i} for s, l, i in guard], **self._expected(expected))

    def purge(self):
        return self.call("store_purge")

    def export(self, scopes=None, after=None):
        """after: (scope, lane, seq) for each lane to start after."""
        args = {} if scopes is None else {"scopes": list(scopes)}
        if after is not None:
            args["after"] = [{"scope": s, "lane": l, "seq": q} for s, l, q in after]
        return StoreExport(self.drive, self.call("store_export", **args)["export"])

    def import_(self, source):
        """From a StoreExport of this drive, or from (heads, lines): heads
        as rows, lines as {"line", "body", "forgotten"} with bytes."""
        if isinstance(source, StoreExport):
            return self.call("store_import", export=source.id)
        heads, lines = source
        return self.call("store_import", heads=list(heads), lines=[line_wire(x) for x in lines])

    def size(self):
        return self.call("store_size")

    def verify(self):
        return self.call("store_verify")

    def plans(self):
        """The SQLite store's plans: [{"name", "lines"}, ...], each of the
        view's reads and the export's statements."""
        return self.call("store_plans")

    def fill(self, scope, lane, count, content, mark, writer, minute=0):
        """count claims like A made and appended in the drive after the
        lane's head, each content (text) with mark replaced by its seq:
        the last append's written, "appended" their count."""
        return self.call("store_fill", scope=scope, lane=lane, count=count, content=content, mark=mark,
                         writer=writer, minute=minute)

    def settings(self):
        """The SQLite store's SETTINGS, each {"name", "value", "when",
        "read"}: read, the value its connection reads back."""
        return self.call("store_settings")

    def seam_write(self, sql):
        """The seam's statement, run through the SQLite store's connection:
        the engine's code, 0 where it ran."""
        return self.call("store_seam_write", sql=sql)["code"]

    def close(self):
        return self.call("store_close")

    def read_only(self):
        return self.call("store_read_only")

    def limits(self):
        return self.call("store_limits")

    def drop(self):
        self.call("store_drop")


class StoreScan:
    """A scan of a drive's store, by its id: next() a header's line as
    bytes, None at the end; iterating reads to the end; close() ends it."""

    def __init__(self, drive, id):
        self.drive, self.id = drive, id

    def next(self):
        got = self.drive.call("scan_next", scan=self.id)
        return None if got is None else bytes.fromhex(got)

    def __iter__(self):
        while (line := self.next()) is not None:
            yield line

    def close(self):
        self.drive.call("scan_close", scan=self.id)


class StoreExport:
    """An export of a drive's store, by its id: heads() its snapshot's
    figures and rows; next() a line, bytes, None at the end; close() the
    contract's; drop() its end."""

    def __init__(self, drive, id):
        self.drive, self.id = drive, id

    def heads(self):
        return self.drive.call("export_heads", export=self.id)

    def next(self):
        got = self.drive.call("export_next", export=self.id)
        return None if got is None else line_of(got)

    def __iter__(self):
        while (line := self.next()) is not None:
            yield line

    def close(self):
        self.drive.call("export_close", export=self.id)

    def drain(self):
        """The export read whole in the drive: {"count", "peak"}, the
        greatest of its reads' arena peaks."""
        return self.drive.call("export_drain", export=self.id)

    def drop(self):
        self.drive.call("export_drop", export=self.id)


class StoreAppender:
    """A drive's appending thread, by its id: release() lets it go on
    from its hold; join() its end, {"appended"} and "error" where one
    ended it."""

    def __init__(self, drive, id):
        self.drive, self.id = drive, id

    def release(self):
        self.drive.call("appender_release", appender=self.id)

    def join(self):
        return self.drive.call("appender_join", appender=self.id)


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
