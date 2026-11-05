"""system's back end on Windows, where its calls by ctypes and winreg and its
system programs stand. Imported by system.py when a member is first asked; the
contract is system.py's.

The DLLs are this module's own instances, so that the argument types set here
reach no other caller.
"""

import ctypes
import os
import subprocess
import tempfile
import winreg
from ctypes import wintypes
from pathlib import Path

from foundation.devkit.python.system import Failed

# SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX, as the diagnostic entry sets.
ERROR_MODE = 0x0001 | 0x0002 | 0x8000
SNAP_PROCESS = 0x2  # TH32CS_SNAPPROCESS
INVALID_HANDLE = wintypes.HANDLE(-1).value
QUERY_LIMITED = 0x1000  # PROCESS_QUERY_LIMITED_INFORMATION
STILL_ACTIVE = 259
ACCESS_DENIED = 5  # ERROR_ACCESS_DENIED: the process stands, and it is another's
NO_SUCH_PROCESS = 87  # ERROR_INVALID_PARAMETER, OpenProcess's word for an id that names none
END_TREE_SECONDS = 30
TOOLSET_SECONDS = 120
KITS = r"SOFTWARE\Microsoft\Windows Kits\Installed Roots"
VC_COMPONENT = "Microsoft.VisualStudio.Component.VC.Tools.x86.x64"


class _ProcessEntry(ctypes.Structure):  # PROCESSENTRY32W
    _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
                ("heap", ctypes.c_size_t), ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                ("parent", wintypes.DWORD), ("priority", ctypes.c_long), ("flags", wintypes.DWORD),
                ("file", wintypes.WCHAR * 260)]


_kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
_kernel32.SetErrorMode.argtypes = (wintypes.UINT,)
_kernel32.SetErrorMode.restype = wintypes.UINT
_kernel32.CloseHandle.argtypes = (wintypes.HANDLE,)
_kernel32.CreateToolhelp32Snapshot.argtypes = (wintypes.DWORD, wintypes.DWORD)
_kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
_kernel32.Process32FirstW.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry))
_kernel32.Process32NextW.argtypes = (wintypes.HANDLE, ctypes.POINTER(_ProcessEntry))
_kernel32.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
_kernel32.OpenProcess.restype = wintypes.HANDLE
_kernel32.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))


def _system_error(member, call):
    """Failed naming the member, its call and the system's reason."""
    return Failed(f"system.{member}: {call}: {ctypes.FormatError(ctypes.get_last_error())}")


def program_file(name):
    return name + ".exe"


def library_file(name):
    return name + ".dll"


def quiet_load_errors():
    _kernel32.SetErrorMode(ERROR_MODE)


def end_tree(pid):
    try:
        subprocess.run(["taskkill", "/T", "/F", "/PID", str(pid)], capture_output=True, stdin=subprocess.DEVNULL,
                       timeout=END_TREE_SECONDS, check=False)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise Failed(f"system.end_tree: taskkill of {pid}: {e}") from e


def children(pid):
    snapshot = _kernel32.CreateToolhelp32Snapshot(SNAP_PROCESS, 0)
    if snapshot == INVALID_HANDLE:
        raise _system_error("children", "CreateToolhelp32Snapshot")
    try:
        found, entry = [], _ProcessEntry()
        entry.size = ctypes.sizeof(_ProcessEntry)
        more = _kernel32.Process32FirstW(snapshot, ctypes.byref(entry))
        while more:
            if entry.parent == pid:
                found.append(entry.pid)
            more = _kernel32.Process32NextW(snapshot, ctypes.byref(entry))
        return found
    finally:
        _kernel32.CloseHandle(snapshot)


def visible_windows(pids):
    # user32 is loaded here, not above: the watcher, in front of every test, asks for no window.
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    proc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = (proc, wintypes.LPARAM)
    user32.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.IsWindowVisible.argtypes = (wintypes.HWND,)
    found = []

    def each(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value in pids and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    if not user32.EnumWindows(proc(each), 0) and ctypes.get_last_error():
        raise _system_error("visible_windows", "EnumWindows")
    return found


def alive(pid):
    process = _kernel32.OpenProcess(QUERY_LIMITED, False, pid)
    if not process:
        error = ctypes.get_last_error()
        if error == ACCESS_DENIED:
            return True
        if error == NO_SUCH_PROCESS:
            return False
        raise _system_error("alive", f"OpenProcess of {pid}")
    try:
        code = wintypes.DWORD()
        if not _kernel32.GetExitCodeProcess(process, ctypes.byref(code)):
            raise _system_error("alive", f"GetExitCodeProcess of {pid}")
        return code.value == STILL_ACTIVE
    finally:
        _kernel32.CloseHandle(process)


def loader_dirs(binary):
    root = os.environ.get("SystemRoot")
    dirs = [binary.parent, *((Path(root) / "System32", Path(root)) if root else ())]
    return dirs + [Path(p) for p in os.environ.get("PATH", "").split(os.pathsep) if p]


def sdk_root():
    for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, KITS, 0, winreg.KEY_READ | view) as key:
                return Path(winreg.QueryValueEx(key, "KitsRoot10")[0])
        except OSError:
            continue
    return None


def _toolset_output(argv):
    """(status, its output, stderr merged) of a toolset's program."""
    try:
        done = subprocess.run(argv, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace", timeout=TOOLSET_SECONDS, check=False)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise Failed(f"system.toolset_env: {Path(argv[0]).name}: {e}") from e
    return done.returncode, done.stdout


def _find_setup():
    """vcvars64.bat of the latest Visual Studio with the C++ tools, by vswhere."""
    where = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not where.is_file():
        raise Failed("system.toolset_env: vswhere.exe is missing, and no vcvars64.bat is named")
    status, text = _toolset_output([str(where), "-latest", "-products", "*", "-requires", VC_COMPONENT,
                                    "-property", "installationPath"])
    if status != 0 or not text.strip():
        raise Failed("system.toolset_env: vswhere finds no Visual Studio with the C++ tools")
    return str(Path(text.strip().splitlines()[0]) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat")


def toolset_env(version, setup):
    setup = setup or _find_setup()
    if not Path(setup).is_file():
        raise Failed(f"system.toolset_env: vcvars64.bat is missing: {setup}")
    with tempfile.TemporaryDirectory(prefix="qiven-toolset-") as scratch:
        script = Path(scratch) / "vcvars.cmd"
        script.write_text(f'@call "{setup}" -vcvars_ver={version} >nul || exit /b 1\r\n@set\r\n', encoding="ascii")
        status, text = _toolset_output(["cmd.exe", "/d", "/c", str(script)])
    if status != 0:
        raise Failed(f"system.toolset_env: vcvars64.bat -vcvars_ver={version} failed ({status}): {text.strip()[-200:]}")
    env = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            env[key] = value
    # A version it has not, vcvars64.bat reports and still exits 0: its environment names no such toolset.
    if not env.get("VCToolsVersion", "").startswith(version):
        raise Failed(f"system.toolset_env: vcvars64.bat -vcvars_ver={version} gives no such toolset")
    return env
