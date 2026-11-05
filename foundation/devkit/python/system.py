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


def loader_dirs(binary):
    """The directories, as Paths, in which the system's loader looks for the
    libraries of the file `binary`, a Path."""
    return _back("loader_dirs").loader_dirs(binary)


def sdk_root():
    """The root of the system's SDK as a Path, or None when none is
    installed."""
    return _back("sdk_root").sdk_root()


def toolset_env(version, setup=None):
    """The environment of the native toolset at `version`, a dictionary:
    `setup` names the toolset's own setup script, or None to find it. Failed,
    naming why, when there is none or it refuses the version."""
    return _back("toolset_env").toolset_env(version, setup)
