"""The Windows drive's holders over real handles, for the files' cases
(contract/platform_files.py): a HeldDrive's hold(**times) arms them on
the next request that names a path, before it is sent, or for publish
the next publish or discard.

- target: a reader on the path, sharing reads alone: replace's rename is
  refused with error 5.
- temporary: a reader on replace's temporary file, sharing reads alone:
  its rename and its deletion are refused with error 32. Its name is
  random, so a watcher finds it beside the path; until the watcher holds
  it, a reader on the path holds the rename back.
- lock: the path's first byte locked through another handle: lock is
  refused with error 33.
- publish: a reader on the first file, by name, of the staging directory
  a publish or discard names by its id, sharing reads alone: publish's
  rename is refused with error 5, and its discard's deletion with 32. The
  drive records each staging directory a stage makes, by its id.

A count n above 0 holds for n retry intervals from when the handle is
taken, and to the request's end at the latest, as a holder's handle
released in time does; -1 holds until the next hold(); none given, none
held. Each handle is this process's, never the drive's, so the drive's
handle count is not changed.
"""

import functools
import os
import threading
from pathlib import Path

from tests.contract.platform_files import STAGING, TEMPORARY
from tests.drive.session import Drive

# files' retry interval, in seconds, and the watcher's.
INTERVAL = 0.05
WATCH = 0.001
KINDS = ("target", "temporary", "lock", "publish")

GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
FILE_SHARE_READ, FILE_SHARE_WRITE = 0x1, 0x2
OPEN_EXISTING, OPEN_ALWAYS = 3, 4
FILE_ATTRIBUTE_NORMAL = 0x80
LOCKFILE_FAIL_IMMEDIATELY, LOCKFILE_EXCLUSIVE_LOCK = 0x1, 0x2


@functools.cache
def kernel32():
    """kernel32's calls the holders make, declared once."""
    import ctypes
    from ctypes import wintypes

    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.CreateFileW.restype = wintypes.HANDLE
    k.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                              wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    k.LockFileEx.restype = wintypes.BOOL
    k.LockFileEx.argtypes = (wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                             wintypes.DWORD, wintypes.LPVOID)
    k.CloseHandle.restype = wintypes.BOOL
    k.CloseHandle.argtypes = (wintypes.HANDLE,)
    return k


def open_file(path, access, share, disposition):
    """A handle on path, or OSError with the system's error."""
    import ctypes

    k = kernel32()
    handle = k.CreateFileW(str(path), access, share, None, disposition, FILE_ATTRIBUTE_NORMAL, None)
    if handle is None or handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    return handle


def reader(path):
    """A reader on path sharing reads alone: no rename over it, no delete."""
    return open_file(path, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING)


def locker(path):
    """A handle on path holding its first byte locked."""
    import ctypes

    class Overlapped(ctypes.Structure):
        _fields_ = [("internal", ctypes.c_void_p), ("internal_high", ctypes.c_void_p),
                    ("offset", ctypes.c_uint32), ("offset_high", ctypes.c_uint32), ("event", ctypes.c_void_p)]

    handle = open_file(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_ALWAYS)
    at = Overlapped()
    if not kernel32().LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                                 ctypes.byref(at)):
        error = ctypes.WinError(ctypes.get_last_error())
        kernel32().CloseHandle(handle)
        raise error
    return handle


def staging(directory):
    """The staging directories' names in directory."""
    return [p.name for p in Path(directory).iterdir() if STAGING.fullmatch(p.name)]


class Holder:
    """One holder of a kind on a path, with its handles, its timer and
    its watcher."""

    def __init__(self, kind, times, path):
        self.kind, self.times, self.path = kind, times, Path(path)
        self.mutex = threading.Lock()
        self.handles = []
        self.done = threading.Event()
        self.timer = None
        self.watcher = None
        if kind == "temporary":
            self.gate = reader(self.path)
            self.handles.append(self.gate)
            self.watcher = threading.Thread(target=self.watch, daemon=True)
            self.watcher.start()
        else:
            self.handles.append(locker(self.path) if kind == "lock" else reader(self.path))
            self.timed()

    def timed(self):
        """A count above 0: released that many intervals from now."""
        if self.times > 0:
            self.timer = threading.Timer(self.times * INTERVAL, self.release)
            self.timer.daemon = True
            self.timer.start()

    def watch(self):
        """replace's temporary file beside the path held once found, then
        the reader on the path released."""
        while not self.done.wait(WATCH):
            with os.scandir(self.path.parent) as entries:
                names = [entry.name for entry in entries]
            for name in names:
                found = TEMPORARY.fullmatch(name)
                if not found or found["name"] != self.path.name:
                    continue
                try:
                    handle = reader(self.path.parent / name)
                except OSError:
                    continue  # still being written, or gone
                with self.mutex:
                    if self.done.is_set():
                        kernel32().CloseHandle(handle)
                        return
                    self.handles.append(handle)
                    self.handles.remove(self.gate)
                    kernel32().CloseHandle(self.gate)
                self.timed()
                return

    def release(self):
        """Every handle closed; idempotent, from any thread."""
        with self.mutex:
            self.done.set()
            handles, self.handles = self.handles, []
        for handle in handles:
            kernel32().CloseHandle(handle)

    def end(self):
        """Released, its timer and its watcher ended."""
        if self.timer is not None:
            self.timer.cancel()
        self.release()
        if self.watcher is not None:
            self.watcher.join()


class HeldDrive(Drive):
    """The Windows drive with its holders: hold(**times), each armed on
    the next request that names a path, publish's on the next publish or
    discard."""

    def __init__(self, *args, **kwargs):
        self.pending = {}
        self.holders = []
        self.staged = {}
        super().__init__(*args, **kwargs)

    def hold(self, **times):
        unknown = set(times) - set(KINDS)
        if unknown:
            raise ValueError(f"no Windows holder for {sorted(unknown)}: {KINDS} only")
        self.release(every=True)
        self.pending = {kind: n for kind, n in times.items() if n}

    def release(self, every):
        """The holders ended: every one, or those held for a count."""
        kept = []
        for holder in self.holders:
            if every or holder.times > 0:
                holder.end()
            else:
                kept.append(holder)
        self.holders = kept

    def first_file(self, staged):
        """The first file, by name, in the staging directory of id staged."""
        return sorted(p for p in self.staged[staged].iterdir() if p.is_file())[0]

    def call(self, op, **args):
        if "path" in args:
            armed = {kind: n for kind, n in self.pending.items() if kind != "publish"}
        else:
            armed = {kind: n for kind, n in self.pending.items() if kind == "publish" and op in ("publish", "discard")}
        for kind, times in armed.items():
            del self.pending[kind]
            path = self.first_file(args["id"]) if kind == "publish" else args["path"]
            self.holders.append(Holder(kind, times, path))
        before = set(staging(args["path"])) if op == "stage" and "path" in args else None
        try:
            answer = super().call(op, **args)
            if before is not None:
                (made,) = set(staging(args["path"])) - before
                self.staged[answer["id"]] = Path(args["path"]) / made
            return answer
        finally:
            self.release(every=False)

    def close(self, check=True):
        self.release(every=True)
        return super().close(check)
