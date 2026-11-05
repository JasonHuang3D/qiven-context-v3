"""The one door the tools and the tests start git by: the runner
(tools/changed.py), the table's reader (tools/loop_modules.py) and the
tests (foundation/devkit/structure/tests/test_line_endings.py) alike.

- which(): PATH's git; None when there is none.
- run(git, argv, ...): git started with argv.

Loaded by runpy.run_path (tools/changed.py, loop_modules.py, loop_base.py)
or imported as tools.loop_git (the tests); it imports no other tool.
"""

import shutil
import subprocess


def which():
    """PATH's git, else None."""
    return shutil.which("git")


def run(git, argv, cwd=None, env=None, data=None, timeout=300, check=False):
    """subprocess.run of git with argv, its output captured, stdin data or
    none."""
    return subprocess.run([str(git), *argv], cwd=None if cwd is None else str(cwd), env=env, input=data,
                          stdin=None if data is not None else subprocess.DEVNULL, capture_output=True,
                          timeout=timeout, check=check)
