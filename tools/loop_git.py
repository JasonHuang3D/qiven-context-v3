"""The one door the tools and the tests start git by: the runner
(tools/changed.py), the table's reader (tools/loop_modules.py) and the
tests (foundation/devkit/structure/tests/test_line_endings.py) alike, so
that a run's git processes are counted where they start.

- which(): PATH's git; None when there is none.
- run(git, argv, ...): git started with argv. Its start leaves one file in
  the directory QIVEN3_GIT_COUNT names, where the environment the call
  runs under names one: one file a start, so that processes running at
  once count without a lock.
- verdict(directory): the starts counted there, and the fault when they
  pass LIMIT. The counting shim before every Python test of the git tests'
  build gives each test a directory of its own and checks the count
  (tools/loop_shim.py).
- carried(starts, env): an inner count's starts added to the enclosing
  count's directory, so that a test's starts count where it runs too.

Loaded by runpy.run_path (tools/changed.py, loop_modules.py,
loop_shim.py) or imported as tools.loop_git (the tests); it imports no
other tool.
"""

import os
import shutil
import subprocess
import uuid
from pathlib import Path

COUNT = "QIVEN3_GIT_COUNT"
LIMIT = 50  # a test starts at most this many git processes


def which():
    """PATH's git, else None."""
    return shutil.which("git")


def counted(env):
    """One file in the count directory env (os.environ when None) names, for a start."""
    directory = (os.environ if env is None else env).get(COUNT)
    if directory:
        Path(directory).mkdir(parents=True, exist_ok=True)
        (Path(directory) / f"{os.getpid()}-{uuid.uuid4().hex}").write_bytes(b"")


def carried(starts, env):
    """starts more files in the count directory env (os.environ when None)
    names, if any: an inner count's starts (the shim's child) counted in
    the enclosing one as well."""
    directory = (os.environ if env is None else env).get(COUNT)
    if directory and starts:
        Path(directory).mkdir(parents=True, exist_ok=True)
        for _ in range(starts):
            (Path(directory) / f"{os.getpid()}-{uuid.uuid4().hex}").write_bytes(b"")


def run(git, argv, cwd=None, env=None, data=None, timeout=300, check=False):
    """subprocess.run of git with argv, its output captured, stdin data or
    none; the start counted (counted())."""
    counted(env)
    return subprocess.run([str(git), *argv], cwd=None if cwd is None else str(cwd), env=env, input=data,
                          stdin=None if data is not None else subprocess.DEVNULL, capture_output=True,
                          timeout=timeout, check=check)


def verdict(directory):
    """(the starts counted in directory, the fault or None): a fault when
    more than LIMIT."""
    try:
        count = sum(1 for p in Path(directory).iterdir() if p.is_file())
    except OSError:
        count = 0
    fault = f"{count} git processes started, over {LIMIT}" if count > LIMIT else None
    return count, fault
