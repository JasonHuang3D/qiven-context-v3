"""The one door the development loop starts git by: the loop's tools
(tools/loop_select.py, loop_linux.py, nav.py) and the loop's tests
(tests/drive/own_git.py) alike, so that a test can give them a fake git
and a run's git processes are counted where they start.

- which(): the git a call runs: the program QIVEN3_GIT_FAKE names, a fake
  for the loop's tests (tests/drive/fake_git.py), run by this
  interpreter; else PATH's git; None when there is none.
- run(git, argv, ...): git started with argv. A real git's start, a fake's
  not, leaves one file in the directory QIVEN3_GIT_COUNT names, where the
  environment the call runs under names one: one file a start, so that
  processes running at once count without a lock.
- verdict(directory): the starts counted there, and the fault when they
  pass LIMIT. The loop gives each step a directory of its own and names
  the count on the step's line (tools/loop_run.py's run_steps); a step
  whose tests start more than LIMIT fails. The product's tests start none:
  no test outside this door starts git (tests/drive/test_git_count.py).

Loaded by runpy.run_path (tools/loop_run.py, nav.py) or imported as
tools.loop_git (the loop's tests); it imports no other tool.
"""

import os
import shutil
import subprocess
import sys
import uuid
from pathlib import Path

FAKE = "QIVEN3_GIT_FAKE"
COUNT = "QIVEN3_GIT_COUNT"
LIMIT = 50  # a run of the loop's tests starts at most this many git processes (ruling 43.3 item 3)


def which():
    """The git a call runs: the fake QIVEN3_GIT_FAKE names, else PATH's
    git, else None."""
    return os.environ.get(FAKE) or shutil.which("git")


def argv_of(git, argv):
    """The command line of git, a program or a fake's script, with argv."""
    git = str(git)
    return [sys.executable, "-I", git, *argv] if git.endswith(".py") else [git, *argv]


def counted(git, env):
    """One file in the count directory env names, for a real git's start."""
    directory = (os.environ if env is None else env).get(COUNT)
    if directory and not str(git).endswith(".py"):
        Path(directory).mkdir(parents=True, exist_ok=True)
        (Path(directory) / f"{os.getpid()}-{uuid.uuid4().hex}").write_bytes(b"")


def run(git, argv, cwd=None, env=None, data=None, timeout=300, check=False):
    """subprocess.run of git with argv, its output captured, stdin data or
    none; the start counted (counted())."""
    counted(git, env)
    return subprocess.run(argv_of(git, argv), cwd=None if cwd is None else str(cwd), env=env, input=data,
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
