"""A Windows job object, holding a program started suspended and every process
it starts: the one place wrap ends a tree. Only documented kernel32 calls.

subprocess as Python ships it closes the thread handle CreateProcess gives, so
a program started with CREATE_SUSPENDED is resumed through a thread snapshot:
each thread the snapshot names as the program's is opened and resumed.
"""

import ctypes
import time
from ctypes import wintypes

CREATE_SUSPENDED = 0x00000004
CREATE_NO_WINDOW = 0x08000000

_PROCESS_TERMINATE = 0x0001
_PROCESS_SET_QUOTA = 0x0100
_THREAD_SUSPEND_RESUME = 0x0002
_SNAP_THREAD = 0x00000004
_BASIC_ACCOUNTING = 1  # JobObjectBasicAccountingInformation
_INVALID_HANDLE = wintypes.HANDLE(-1).value
_POLL_SECONDS = 0.05

_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)


class _ThreadEntry(ctypes.Structure):  # THREADENTRY32
    _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("thread", wintypes.DWORD),
                ("owner", wintypes.DWORD), ("base_priority", wintypes.LONG),
                ("delta_priority", wintypes.LONG), ("flags", wintypes.DWORD)]


class _Accounting(ctypes.Structure):  # JOBOBJECT_BASIC_ACCOUNTING_INFORMATION
    _fields_ = [("user", ctypes.c_int64), ("kernel", ctypes.c_int64), ("period_user", ctypes.c_int64),
                ("period_kernel", ctypes.c_int64), ("page_faults", wintypes.DWORD),
                ("processes", wintypes.DWORD), ("active", wintypes.DWORD), ("terminated", wintypes.DWORD)]


def _declare(name, result, *arguments):
    function = getattr(_kernel32, name)
    function.restype, function.argtypes = result, arguments
    return function


_CreateJobObjectW = _declare("CreateJobObjectW", wintypes.HANDLE, wintypes.LPVOID, wintypes.LPCWSTR)
_AssignProcessToJobObject = _declare("AssignProcessToJobObject", wintypes.BOOL, wintypes.HANDLE, wintypes.HANDLE)
_TerminateJobObject = _declare("TerminateJobObject", wintypes.BOOL, wintypes.HANDLE, wintypes.UINT)
_QueryInformationJobObject = _declare("QueryInformationJobObject", wintypes.BOOL, wintypes.HANDLE, ctypes.c_int,
                                      wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD))
_OpenProcess = _declare("OpenProcess", wintypes.HANDLE, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_OpenThread = _declare("OpenThread", wintypes.HANDLE, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_ResumeThread = _declare("ResumeThread", wintypes.DWORD, wintypes.HANDLE)
_CreateToolhelp32Snapshot = _declare("CreateToolhelp32Snapshot", wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD)
_Thread32First = _declare("Thread32First", wintypes.BOOL, wintypes.HANDLE, ctypes.POINTER(_ThreadEntry))
_Thread32Next = _declare("Thread32Next", wintypes.BOOL, wintypes.HANDLE, ctypes.POINTER(_ThreadEntry))
_CloseHandle = _declare("CloseHandle", wintypes.BOOL, wintypes.HANDLE)


def _error(call):
    """OSError naming the kernel32 call and the system's reason."""
    code = ctypes.get_last_error()
    return OSError(code, f"{call}: {ctypes.FormatError(code).strip()}")


class Job:
    """A job object of its own; take() puts a suspended program in it and runs
    it, end() ends every process in it, close() lets go of it."""

    def __init__(self):
        self._handle = _CreateJobObjectW(None, None)
        if not self._handle:
            raise _error("CreateJobObjectW")

    def take(self, pid):
        """Puts the suspended program `pid` into the job, then resumes it."""
        process = _OpenProcess(_PROCESS_SET_QUOTA | _PROCESS_TERMINATE, False, pid)
        if not process:
            raise _error("OpenProcess")
        try:
            if not _AssignProcessToJobObject(self._handle, process):
                raise _error("AssignProcessToJobObject")
        finally:
            _CloseHandle(process)
        _resume(pid)

    def end(self):
        """Ends every process in the job, by force; their ending is the
        system's, and wait_empty waits for it."""
        _TerminateJobObject(self._handle, 1)

    def alive(self):
        """The number of processes in the job that have not ended."""
        accounting = _Accounting()
        if not _QueryInformationJobObject(self._handle, _BASIC_ACCOUNTING, ctypes.byref(accounting),
                                          ctypes.sizeof(accounting), None):
            raise _error("QueryInformationJobObject")
        return accounting.active

    def wait_empty(self, seconds):
        """The number of its processes still alive once all have ended or
        `seconds` have passed."""
        deadline = time.monotonic() + seconds
        while (left := self.alive()) and time.monotonic() < deadline:
            time.sleep(_POLL_SECONDS)
        return left

    def close(self):
        if self._handle:
            _CloseHandle(self._handle)
            self._handle = None


def _resume(pid):
    """Resumes each thread of the process `pid`; OSError where none is."""
    snapshot = _CreateToolhelp32Snapshot(_SNAP_THREAD, 0)
    if snapshot == _INVALID_HANDLE:
        raise _error("CreateToolhelp32Snapshot")
    resumed = 0
    try:
        entry = _ThreadEntry()
        entry.size = ctypes.sizeof(_ThreadEntry)
        more = _Thread32First(snapshot, ctypes.byref(entry))
        while more:
            if entry.owner == pid:
                thread = _OpenThread(_THREAD_SUSPEND_RESUME, False, entry.thread)
                if not thread:
                    raise _error("OpenThread")
                try:
                    if _ResumeThread(thread) == 0xFFFFFFFF:
                        raise _error("ResumeThread")
                finally:
                    _CloseHandle(thread)
                resumed += 1
            more = _Thread32Next(snapshot, ctypes.byref(entry))
    finally:
        _CloseHandle(snapshot)
    if not resumed:
        raise OSError(f"no thread of process {pid} to resume")
