"""The operating system, for the repository's Python: the one conditional on
the system, WINDOWS, and each member a tool or a test needs of it, in words
no system owns. This directory's README states each member's call on each
system.

A member's work stands in the system's back end, _windows.py here, which
is loaded when a member is first asked, so that reading WINDOWS costs no
import. On any other system a member raises NotBuilt, and importing this
module still succeeds. A system call that fails raises Failed, in this
module's words.
"""

import sys

WINDOWS = sys.platform == "win32"


class NotBuilt(Exception):
    """A member asked on a system whose back end is not built."""


class Failed(Exception):
    """A member's system call failed: the member, the call and the reason."""


def _back(member):
    """The system's back end, or NotBuilt naming the member."""
    if not WINDOWS:
        raise NotBuilt(f"system.{member}: not built on {sys.platform}: Windows alone has a back end")
    from foundation.devkit.python import _windows
    return _windows


def program_file(name):
    """The file name of the program `name`."""
    return _back("program_file").program_file(name)


def library_file(name):
    """The file name of the shared library `name`."""
    return _back("library_file").library_file(name)


def quiet_load_errors():
    """A library missing at load opens no window, in this process and in
    each process it starts afterwards."""
    _back("quiet_load_errors").quiet_load_errors()


def end_tree(pid):
    """Ends process `pid` and every process it started, by force. A process
    gone already is no error, and neither is one it could not end: the caller
    ends its own child next. Failed when the means itself cannot run."""
    _back("end_tree").end_tree(pid)


def children(pid):
    """The ids of the processes `pid` started that still run, as a list."""
    return _back("children").children(pid)


def visible_windows(pids):
    """The visible top-level windows of the processes `pids`, as a list."""
    return _back("visible_windows").visible_windows(pids)


def alive(pid):
    """Whether process `pid` still runs."""
    return _back("alive").alive(pid)


def handle_count(pid):
    """The number of handles process `pid` holds."""
    return _back("handle_count").handle_count(pid)


def plant_handle(pid):
    """A handle of this process put into process `pid`, which then holds one
    handle more, never closed: a leak planted, for a test of a count."""
    _back("plant_handle").plant_handle(pid)


def fail_fast_status():
    """The status, as subprocess gives it, of a process ended by its own
    fail-fast."""
    return _back("fail_fast_status").fail_fast_status()


def missing_library_status():
    """The status, as subprocess gives it, of a process whose binary's
    library is missing at load."""
    return _back("missing_library_status").missing_library_status()


def invalid_access_status():
    """The status, as subprocess gives it, of a process ended by a read or
    write of memory it may not touch."""
    return _back("invalid_access_status").invalid_access_status()


def output_text(data, errors="strict"):
    """A system program's output to a pipe, bytes, as text; `errors` as
    bytes.decode takes it."""
    return _back("output_text").output_text(data, errors)


def output_bytes(text, errors="strict"):
    """`text` as a system program's output to a pipe carries it; `errors` as
    str.encode takes it."""
    return _back("output_bytes").output_bytes(text, errors)


def drives():
    """This machine's drives, a dictionary: each letter, upper case, to its
    kind: "fixed", "removable", "remote", "optical", "memory", "no root" or
    "unknown"."""
    return _back("drives").drives()


def make_junction(target, link):
    """A directory junction at `link` to the directory `target`."""
    _back("make_junction").make_junction(target, link)


def map_drive(letter, target):
    """The drive `letter`, a letter no drive holds, mapped to the directory
    `target` until unmap_drive."""
    _back("map_drive").map_drive(letter, target)


def unmap_drive(letter):
    """The drive `letter`'s mapping by map_drive removed."""
    _back("unmap_drive").unmap_drive(letter)


def loader_dirs(binary):
    """The directories, as Paths, in which the system's loader looks for the
    libraries of the file `binary`, a Path."""
    return _back("loader_dirs").loader_dirs(binary)


def api_set(name):
    """Whether the library name `name`, a bare name and no path, is one of
    the system's API sets, contract names its loader maps to its own
    libraries; case aside."""
    return _back("api_set").api_set(name)


def sdk_root():
    """The root of the system's SDK as a Path, or None when none is
    installed."""
    return _back("sdk_root").sdk_root()


def kill_on_close():
    """This process put in a job that the system ends whole when this process
    ends, however it ends: each process it starts afterwards, and theirs,
    starts inside it. Once a process; a later call does nothing."""
    _back("kill_on_close").kill_on_close()


def lock_byte(fd, offset):
    """The system's lock on the one byte at `offset` of the open file `fd`,
    taken without waiting: True where taken, False where another holds it.
    It moves the file's position to `offset`."""
    return _back("lock_byte").lock_byte(fd, offset)


def unlock_byte(fd, offset):
    """The lock lock_byte took on the byte at `offset` of `fd` released. It
    moves the file's position to `offset`."""
    _back("unlock_byte").unlock_byte(fd, offset)


def toolset_env(version, setup=None):
    """The environment of the native toolset at `version`, a dictionary:
    `setup` names the toolset's own setup script, or None to find it. Failed,
    naming why, when there is none or it refuses the version."""
    return _back("toolset_env").toolset_env(version, setup)
