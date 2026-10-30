"""One Python test run behind the counting shim, so that every start of
git by a name PATH resolves is counted: the shim (tools/git_shim.c, built
by the toolset's cl, on Windows; tools/git_shim.sh on Linux) stands first
on PATH under git's program name, logs each start and runs the real git,
PATH's before the shim. tests/runner/ctest_tests.cmake runs every Python
test so on the build QIVEN3_LOOP_TESTS names, one a system, whose fake
step is the loop's one step a run that starts git.

Usage: python -I tools/loop_shim.py [--git] -- COMMAND...

The test's door counts in a directory of the run's own; then its starts,
the shim's or the door's, the more, are added to the count the
environment names (tools/loop_git.py's carried), the enclosing step's, so
that the step's count is its real starts. The run fails (status 1) when
the shim's starts differ from the door's, a start past the door, by a name
built at run time or a shell's line; or, without --git, when the test
starts git at all: a test that starts git is the loop's own (a class's
ctest_git, tests/runner/list_tests.py), listed on that build alone, so
that no test that starts git runs in a second step of a run. Else its
status is the test's. A start by an absolute path passes the shim: the
guard's, by the source (tests/drive/test_git_count.py), the stated limit.
A run within another's (tests/drive/test_git_shim.py's) runs the enclosing
shim's real git, never a shim, and adds its log's lines to that shim's.
A real git in any git-shim directory, whichever run's or build's, is a
shim, which would run itself: the run is refused before it starts any.

On Windows the shim is built once a build, under QIVEN3_BUILD's git-shim
directory by its source's digest, else in the run's own directory; a
build made at once by two runs keeps the first one renamed into place.
"""

import hashlib
import os
import runpy
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
DOOR = runpy.run_path(str(HERE / "loop_git.py"))
PROGRAM = "git.exe" if sys.platform == "win32" else "git"
SHIM_LOG, SHIM_REAL = "QIVEN3_SHIM_LOG", "QIVEN3_SHIM_REAL"
SHOWN = 40  # the shim's log lines a fault shows at most
PLACE = "git-shim"  # the name of each directory a shim stands in, every run's


def built(top):
    """The directory holding the Windows shim, built by cl: a build's own
    cache when QIVEN3_BUILD names one, else under top."""
    source = HERE / "git_shim.c"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()[:16]
    build = os.environ.get("QIVEN3_BUILD")
    home = Path(build) / PLACE if build else top / PLACE
    final = home / digest
    if (final / PROGRAM).is_file():
        return final
    cl = shutil.which("cl")
    if not cl:
        raise SystemExit("loop_shim: no cl on PATH: the shim is built by the toolset's cl, under vcvars")
    compiled = top / "cl"  # its object, which holds the checkout's path, never in the build
    compiled.mkdir()
    done = subprocess.run([cl, "/nologo", "/W4", "/WX", "/O1", str(source), f"/Fe{compiled / PROGRAM}",
                           f"/Fo{compiled}\\", "kernel32.lib"], cwd=compiled, capture_output=True,
                          stdin=subprocess.DEVNULL, timeout=300)
    if done.returncode != 0 or not (compiled / PROGRAM).is_file():
        raise SystemExit("loop_shim: the shim did not build: " + done.stdout.decode("utf-8", "replace")[-2000:])
    home.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="making-", dir=home))
    shutil.copyfile(compiled / PROGRAM, work / PROGRAM)
    try:
        work.rename(final)
    except OSError:
        shutil.rmtree(work, ignore_errors=True)  # another run's build is in place
        if not (final / PROGRAM).is_file():
            raise
    return final


def placed(top):
    """The shim's directory, its program there under git's name."""
    if sys.platform == "win32":
        return built(top)
    shim = top / PLACE
    shim.mkdir()
    shutil.copyfile(HERE / "git_shim.sh", shim / PROGRAM)
    (shim / PROGRAM).chmod(0o755)
    return shim


def main(argv):
    git = argv[:1] == ["--git"]
    rest = argv[1:] if git else argv
    if rest[:1] != ["--"] or len(rest) < 2:
        print(f"usage: loop_shim.py [--git] -- COMMAND..., not {argv}", file=sys.stderr)
        return 2
    command = rest[1:]
    # Within an enclosing shim's run, its real git, and its log gains this
    # one's lines; else PATH's git, before the shim stands there.
    outer = os.environ.get(SHIM_LOG)
    real = os.environ.get(SHIM_REAL) if outer else shutil.which(PROGRAM)
    if real is None:
        return subprocess.run(command).returncode  # no git a name resolves: none to count
    if any(part.lower() == PLACE for part in Path(real).resolve().parent.parts):
        raise SystemExit(f"loop_shim: the real git is a shim, under a {PLACE} directory: {real}")
    top = Path(tempfile.mkdtemp(prefix="qiven3-shim-"))
    try:
        shim = placed(top)
        log, count = top / "shim.log", top / "count"
        log.write_bytes(b"")
        env = dict(os.environ)
        env.update({"PATH": str(shim) + os.pathsep + env.get("PATH", ""), DOOR["COUNT"]: str(count),
                    SHIM_LOG: str(log), SHIM_REAL: real})
        status = subprocess.run(command, env=env).returncode
        lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
        if outer and lines:
            with open(outer, "a", encoding="utf-8") as f:
                f.write("".join(line + "\n" for line in lines))
        door = DOOR["verdict"](count)[0]
        DOOR["carried"](max(len(lines), door), None)
    finally:
        shutil.rmtree(top, ignore_errors=True)
    faults = []
    if len(lines) != door:
        faults.append(f"the shim's {len(lines)} starts against the door's {door}: a start past the door")
    if lines and not git:
        faults.append(f"{len(lines)} starts of git by a test that is not the loop's own (ctest_git), which "
                      "every build lists")
    if faults:
        print("loop_shim: " + "; ".join(faults) + "\n" + "\n".join(lines[-SHOWN:]), file=sys.stderr)
        return 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
