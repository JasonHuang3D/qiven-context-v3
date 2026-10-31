"""What a case of the loop's needs that a developer's machine holds and CI's
images lack, each read from the loop's own checks, never a second copy of
its pins: the interpreter tools/loop.py pins, the Windows tools it pins
(vcvars' toolset and SDK, CMake, CTest, Ninja, cl and link, each by its
SHA-256), WSL with its distribution, and the toolchain's prefix in WSL. A
case needing one skips, its reason in words, where it is absent or off its
pin, as on CI's images with their own tools; where each holds, it runs."""

import functools
import os
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive.test_loop_ci import load_loop

LOOP = load_loop()


def python():
    """The interpreter the loop's Windows side checks first: a case that
    runs that side skips under another."""
    try:
        LOOP.WINDOWS["check_python"](sys.executable)
    except LOOP.CannotRun as e:
        raise unittest.SkipTest(f"not the loop's pinned interpreter: {e}") from None


@functools.cache
def windows_tools_fault():
    """Why vcvars or a pinned Windows tool does not hold, as the loop
    checks them before a Windows run, its logs in a scratch directory;
    None when they hold."""
    logs = Path(tempfile.mkdtemp(prefix="qiven3-pinned-"))
    try:
        LOOP.check_windows_tools(LOOP.vcvars_env(logs))
    except LOOP.CannotRun as e:
        return str(e)
    finally:
        shutil.rmtree(logs, ignore_errors=True)
    return None


def windows_tools():
    """The loop's pinned Windows tools: a case that drives them skips
    where they do not hold."""
    fault = windows_tools_fault()
    if fault:
        raise unittest.SkipTest(f"the loop's pinned Windows tools do not hold: {fault}")


def wsl_ready():
    wsl = shutil.which("wsl.exe") if sys.platform == "win32" else None
    try:
        return bool(wsl) and LOOP.DISTRO in LOOP.distros(wsl)
    except LOOP.CannotRun:
        return False


def prefix():
    """The toolchain's prefix in WSL, from QIVEN3_PREFIX: a case needing it
    skips without it."""
    if not os.environ.get("QIVEN3_PREFIX"):
        raise unittest.SkipTest("QIVEN3_PREFIX is unset: the toolchain's prefix in WSL")
    return os.environ["QIVEN3_PREFIX"]
