"""system's back end on Windows, where its calls by ctypes, winreg and _winapi
and its system programs stand. Imported by system.py when a member is first
asked; the contract is system.py's.

The DLLs are this module's own instances, so that the argument types set here
reach no other caller.
"""

import _winapi
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
DUP_HANDLE = 0x0040  # PROCESS_DUP_HANDLE
SAME_ACCESS = 0x2  # DUPLICATE_SAME_ACCESS
STILL_ACTIVE = 259
ACCESS_DENIED = 5  # ERROR_ACCESS_DENIED: the process stands, and it is another's
NO_SUCH_PROCESS = 87  # ERROR_INVALID_PARAMETER, OpenProcess's word for an id that names none
FAIL_FAST = 0xC0000409  # STATUS_STACK_BUFFER_OVERRUN, __fastfail's
MISSING_LIBRARY = 0xC0000135  # STATUS_DLL_NOT_FOUND
INVALID_ACCESS = 0xC0000005  # STATUS_ACCESS_VIOLATION
# GetDriveTypeW's answers, DRIVE_UNKNOWN to DRIVE_RAMDISK, in system.drives' words.
DRIVE_KINDS = ("unknown", "no root", "removable", "fixed", "remote", "optical", "memory")
# A system program's output to a pipe: the ANSI code page's.
OUTPUT_CODEC = "mbcs"
END_TREE_SECONDS = 30
SUBST_SECONDS = 30
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
_kernel32.GetProcessHandleCount.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
_kernel32.GetCurrentProcess.argtypes = ()
_kernel32.GetCurrentProcess.restype = wintypes.HANDLE
_kernel32.DuplicateHandle.argtypes = (wintypes.HANDLE, wintypes.HANDLE, wintypes.HANDLE,
                                      ctypes.POINTER(wintypes.HANDLE), wintypes.DWORD, wintypes.BOOL,
                                      wintypes.DWORD)
_kernel32.GetLogicalDrives.argtypes = ()
_kernel32.GetLogicalDrives.restype = wintypes.DWORD
_kernel32.GetDriveTypeW.argtypes = (wintypes.LPCWSTR,)
_kernel32.GetDriveTypeW.restype = wintypes.UINT


def _system_error(member, call):
    """Failed naming the member, its call and the system's reason."""
    return Failed(f"system.{member}: {call}: {ctypes.FormatError(ctypes.get_last_error())}")


def program_file(name):
    return name + ".exe"


def library_file(name):
    return name + ".dll"


def quiet_load_errors():
    _kernel32.SetErrorMode(ERROR_MODE)


def _run(argv, seconds, tree=True, **pipes):
    """(status, stdout, stderr) of a system program started with stdin closed.
    Past `seconds` its tree is ended, by end_tree where `tree`, else the
    program alone, before its output is collected, since a child holding the
    pipe would hold the caller past the limit; then TimeoutExpired, raised
    again past END_TREE_SECONDS more where a child holds the pipe still."""
    child = subprocess.Popen(argv, stdin=subprocess.DEVNULL, **pipes)
    try:
        out, err = child.communicate(timeout=seconds)
    except subprocess.TimeoutExpired:
        try:
            if tree:
                end_tree(child.pid)
        finally:
            child.kill()
            child.communicate(timeout=END_TREE_SECONDS)
        raise
    return child.returncode, out, err


def end_tree(pid):
    try:
        # taskkill starts no child: at its limit it is ended alone.
        _run(["taskkill", "/T", "/F", "/PID", str(pid)], END_TREE_SECONDS, tree=False,
             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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


def handle_count(pid):
    process = _kernel32.OpenProcess(QUERY_LIMITED, False, pid)
    if not process:
        raise _system_error("handle_count", f"OpenProcess of {pid}")
    try:
        count = wintypes.DWORD()
        if not _kernel32.GetProcessHandleCount(process, ctypes.byref(count)):
            raise _system_error("handle_count", f"GetProcessHandleCount of {pid}")
        return count.value
    finally:
        _kernel32.CloseHandle(process)


def plant_handle(pid):
    target = _kernel32.OpenProcess(DUP_HANDLE, False, pid)
    if not target:
        raise _system_error("plant_handle", f"OpenProcess of {pid}")
    try:
        out = wintypes.HANDLE()
        own = _kernel32.GetCurrentProcess()
        if not _kernel32.DuplicateHandle(own, own, target, ctypes.byref(out), 0, False, SAME_ACCESS):
            raise _system_error("plant_handle", f"DuplicateHandle into {pid}")
    finally:
        _kernel32.CloseHandle(target)


def fail_fast_status():
    return FAIL_FAST


def missing_library_status():
    return MISSING_LIBRARY


def invalid_access_status():
    return INVALID_ACCESS


def output_text(data, errors):
    return data.decode(OUTPUT_CODEC, errors)


def output_bytes(text, errors):
    return text.encode(OUTPUT_CODEC, errors)


def drives():
    mask = _kernel32.GetLogicalDrives()
    if not mask:
        raise _system_error("drives", "GetLogicalDrives")
    letters = [chr(ord("A") + i) for i in range(26) if mask >> i & 1]
    kinds = {letter: _kernel32.GetDriveTypeW(f"{letter}:\\") for letter in letters}
    return {letter: DRIVE_KINDS[kind] if kind < len(DRIVE_KINDS) else "unknown" for letter, kind in kinds.items()}


def make_junction(target, link):
    try:
        _winapi.CreateJunction(str(target), str(link))
    except OSError as e:
        raise Failed(f"system.make_junction: CreateJunction of {link} to {target}: {e}") from e


def _subst(member, argv):
    """subst with argv; Failed naming the member, by its output, where it
    fails."""
    try:
        status, out, err = _run(["subst", *argv], SUBST_SECONDS, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise Failed(f"system.{member}: subst {' '.join(argv)}: {e}") from e
    if status != 0:
        text = output_text(out + err, "replace").strip()
        raise Failed(f"system.{member}: subst {' '.join(argv)} gave {status}: {text}")


def map_drive(letter, target):
    _subst("map_drive", [f"{letter}:", str(target)])


def unmap_drive(letter):
    _subst("unmap_drive", [f"{letter}:", "/D"])


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
        status, out, _ = _run(argv, TOOLSET_SECONDS, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, errors="replace")
    except (OSError, subprocess.TimeoutExpired) as e:
        raise Failed(f"system.toolset_env: {Path(argv[0]).name}: {e}") from e
    return status, out


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
