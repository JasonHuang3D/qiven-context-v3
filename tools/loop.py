"""The local loop: one command a way of running, on Windows or in WSL2's
Linux.

Run from a checkout on Windows by the pinned interpreter:

    py -3.15 -I tools/loop.py WAY SYSTEM [JOB ...] [options]

SYSTEM is windows or linux. WAY is a tier, 1, 2 or 3, each on the system's
jobs as below, or build or test of the jobs named:
- 1, the edit loop: one job, windows-msvc or linux-gcc, in its kept tree,
  configured without --fresh, so that Ninja builds only what changed; then
  its tests, then the label fast on its build, without the structure tests
  on Windows. Tier 1 builds no structure tree: the structure tests run in
  tier 2's windows-analysis and in tier 3. Workers run tier 1 alone.
- 2, a unit's gate: every job of the system, each in its own kept tree,
  configured without --fresh, running its tests.
- 3, the boundary run: every job of the system from emptied trees, each
  configure with --fresh, every test of each; then, once a system, the full
  suite and the fuzz targets: on Windows the structure and nohal presets'
  builds and ctest --preset full on release (windows-symbols' tree), then
  the fuzz preset's build and ctest --preset fuzz; on Linux ctest --preset
  full on linux-plain's tree, the fuzz label having run in linux-fuzz's own
  step. With --fuzz-time SECONDS each fuzz target runs for that time from
  its corpus instead, as a half's evidence has it (600). Emptying keeps the fuzz
  corpus.
- build or test: the jobs named, each a job's name or its preset's
  (windows-msvc or ci-windows), none naming every job of the system, in
  their kept trees without --fresh; build stops after the build, test runs
  every step of the job.
Tiers 1 and 2 narrow each ctest step to its affected tests, from the build
graph (tools/loop_select.py): the case binaries the build relinked, the
Python tests by the files they import and those their classes declare they
read, every test on a build file's, tools/', a fixture's or a vector's
change, or in a tree new or emptied; changed against the step's last
passing run in its tree, which the tree records. Each step's line names its selection: how
many of its tests, and why; a step with none affected does not run.

The jobs and their steps are CI's (.github/workflows/ci.yml and msan.yml),
with the local pins added: tools/loop_jobs.py, which a test checks against
the workflows. CI runs unpinned on its images' own tools; the loop runs each
job on this machine's pinned tools. Beyond CI's, the loop's own jobs and
steps, local alone: windows-symbols, the release preset keeping its PDBs,
running the fake and the debugger cases, and linux-plain's debugger step,
the debugger cases under gdb. macOS stays with CI alone.

- windows: a shell set up by vcvars64.bat -vcvars_ver=14.44.35207 (found by
  vswhere, or QIVEN3_VCVARS), the tools of WINDOWS_PINS checked by SHA-256
  and version, CPython 3.15.0 this interpreter; each configure with
  QIVEN3_PIN_TOOLS on; for windows-analysis, clang-tidy (QIVEN3_CLANG_TIDY,
  else LLVM's under ProgramFiles) by CLANG_TIDY's SHA-256 and version.
  vcvars' variables are read by name without regard to case, as Windows
  names them: a name the loop inherits keeps its case through vcvars, and
  a copy of Python's os.environ (the watcher's, for one) holds every name
  upper-cased. The build runs in the checkout, each job in its own trees
  under build/ (tools/loop_jobs.py's TREES: build/windows-msvc, ...;
  windows-symbols build/release), where CI's jobs share build/ci; a tree
  is marked by its job, and one another job left is emptied first. The
  logs are kept in build/loop/JOB/.
- linux: reaches WSL2 (--distro, QIVEN3_WSL_DISTRO, default Ubuntu-24.04),
  checks tools/linux-pins.txt by tools/linux_pins.py under the toolchain's
  prefix (--prefix or QIVEN3_PREFIX, no default), then brings the working
  tree, uncommitted changes and new files included, to one clone on WSL's
  own file system, clone/ under the clone root (--clone,
  QIVEN3_LOOP_CLONE, default ~/qiven3-loop), synced once a run by a commit
  object made from a copy of the index: the Windows tree and its index are
  not touched. Each job builds in its own tree within the clone,
  build/JOB, kept as on Windows; each has its own TMPDIR, the
  clean-binaries check's RUNNER_TEMP. The logs are kept in the clone's
  build/loop/. Each configure with QIVEN3_PIN_TOOLS on and the pinned
  compilers; each step under setarch -R, as CI's Linux shell. From Git
  Bash, set MSYS_NO_PATHCONV=1 before giving a POSIX path as an argument,
  or give it by the environment.

The answer is plain text: a line a tree and a step with its outcome and
seconds; for a failing step, its first error lines before its last 30,
up to 10, each with its line number in the log (a compiler's, a linker's
or clang-tidy's error, MSVC's warning, an error under /WX, ninja's failed
edge, CMake's error, ctest's failed tests, a Python traceback and its
exception, unittest's FAIL and ERROR, a sanitizer's report), then its
last 30 lines; at most 100 lines in all. Exit 0
when every step passed, 1 when a step failed, 2 when the loop could not
run (a tool missing, a pin off, WSL absent, a bad argument).

The modules: tools/loop_run.py (the steps, the trees, the answer),
loop_jobs.py (the table), loop_tiers.py (the ways and the commands),
loop_select.py (the affected tests), loop_windows.py, loop_linux.py, and
loop_fuzz.py (the timed fuzz targets).
"""

import argparse
import os
import runpy
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent


def load(name, given):
    """A module of the loop's by runpy (under -I this directory is off
    sys.path), given the names of those before it."""
    return runpy.run_path(str(TOOLS / name), init_globals=given)


# The core, the table, the ways, the affected tests, then each system's side,
# each given the names of those before it; this file's names are theirs for
# its callers.
RUN = load("loop_run.py", {})
TABLE = load("loop_jobs.py", {})
GIVEN = {k: v for k, v in {**RUN, **TABLE}.items() if not k.startswith("__")}
TIERS = load("loop_tiers.py", GIVEN)
GIVEN.update({k: v for k, v in TIERS.items() if not k.startswith("__")})
SELECT = load("loop_select.py", GIVEN)
GIVEN.update({k: v for k, v in SELECT.items() if not k.startswith("__")})
WINDOWS = load("loop_windows.py", GIVEN)
LINUX = load("loop_linux.py", GIVEN)

ROOT = RUN["ROOT"]
CannotRun, Report, LINES, TAIL, FIRST, ERROR_LINE, chosen, run_steps, own_tree, drive = (
    RUN[k] for k in ("CannotRun", "Report", "LINES", "TAIL", "FIRST", "ERROR_LINE", "chosen", "run_steps",
                     "own_tree", "drive"))
JOBS, LOCAL, LOCAL_STEPS, TREES, ENV, CLEAN, LINUX_SHELL, Step = (
    TABLE[k] for k in ("JOBS", "LOCAL", "LOCAL_STEPS", "TREES", "ENV", "CLEAN", "LINUX_SHELL", "Step"))
WAYS, plan, local_argv, binary_dirs = (TIERS[k] for k in ("WAYS", "plan", "local_argv", "binary_dirs"))
WINDOWS_PINS, VCVARS_VER, vcvars_env, check_windows_tools = (
    WINDOWS[k] for k in ("WINDOWS_PINS", "VCVARS_VER", "vcvars_env", "check_windows_tools"))
DISTRO = "Ubuntu-24.04"
CLONE = "~/qiven3-loop"
distros = LINUX["distros"]


class Parser(argparse.ArgumentParser):
    def error(self, message):
        print(f"loop: {message} (see --help)")
        sys.exit(2)


def parse(argv):
    parser = Parser(prog="loop.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("way", choices=WAYS, help="a tier, 1, 2 or 3, or build or test of the jobs named")
    parser.add_argument("system", choices=("windows", "linux"))
    parser.add_argument("jobs", nargs="*", metavar="JOB",
                        help="build and test: a job or its preset; none: every job of the system")
    parser.add_argument("--fuzz-time", type=int, metavar="SECONDS",
                        help="tier 3: each fuzz target for this time from its corpus, not the preset's")
    parser.add_argument("--distro", default=os.environ.get("QIVEN3_WSL_DISTRO", DISTRO))
    parser.add_argument("--prefix", default=os.environ.get("QIVEN3_PREFIX") or None)
    parser.add_argument("--clone", default=os.environ.get("QIVEN3_LOOP_CLONE", CLONE))
    parser.add_argument("--pins", help="the Linux pin file (default: tools/linux-pins.txt)")
    parser.add_argument("--inside", action="store_true", help=argparse.SUPPRESS)
    parser.add_argument("--source", help=argparse.SUPPRESS)
    parser.add_argument("--commit", help=argparse.SUPPRESS)
    parser.add_argument("--lines", type=int, default=LINES, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    names = {name: name for name, job in JOBS.items() if job[0] == args.system}
    names.update({job[1]: name for name, job in JOBS.items() if job[0] == args.system})
    unknown = [j for j in args.jobs if j not in names]
    if args.way in ("1", "2", "3") and args.jobs:
        parser.error(f"tier {args.way} runs its own jobs: name none")
    if unknown:
        parser.error(f"no {args.system} job {', '.join(unknown)}; the jobs: {', '.join(sorted(names))}")
    if args.fuzz_time is not None and (args.way != "3" or args.fuzz_time <= 0):
        parser.error("--fuzz-time is tier 3's, a positive number of seconds")
    args.jobs = list(dict.fromkeys(names[j] for j in args.jobs)) or [n for n, j in JOBS.items() if j[0] == args.system]
    if args.system == "linux" and not args.prefix:
        parser.error("linux needs the toolchain's prefix: give --prefix or set QIVEN3_PREFIX")
    if args.inside and not (args.source and args.commit):
        parser.error("--inside needs --source and --commit")
    if not args.inside and args.system == "windows" and sys.platform != "win32":
        parser.error("windows runs on Windows")
    if not args.inside and args.system == "linux" and sys.platform != "win32":
        parser.error("linux runs from Windows, reaching WSL")
    args.lines = max(5, min(args.lines, LINES))
    return args


def title(args):
    """The answer's first line: the way, the system and what it runs."""
    if args.way in ("build", "test"):
        return f"loop: {args.way} {args.system}: {', '.join(args.jobs)}"
    units = [u.name for u in plan(args.way, args.system, args.jobs, args.fuzz_time, ROOT)]
    trees = "emptied trees, --fresh" if args.way == "3" else "kept trees"
    fuzz = f", fuzz {args.fuzz_time} s a target" if args.fuzz_time else ""
    return f"loop: tier {args.way} {args.system}, {trees}{fuzz}: {', '.join(units)}"


def main(argv=None):
    args = parse(argv)
    report = Report(args.lines)
    start = time.monotonic()
    try:
        if not args.inside:
            report.line(title(args))
        if args.inside:
            counts = LINUX["inside"](args, report)
        elif args.system == "windows":
            counts = WINDOWS["windows"](args, report)
        else:
            return LINUX["linux"](args, report)
    except CannotRun as e:
        report.last(f"loop: cannot run: {e}; exit 2")
        return 2
    except Exception as e:  # an error of the loop's own is not a failing step
        report.last(f"loop: internal error: {type(e).__name__}: {e}; exit 2")
        return 2
    code = 1 if counts[1] else 0
    report.last(f"loop: {counts[0]} passed, {counts[1]} failed, {counts[2]} skipped in {RUN['seconds'](start)}; "
                f"exit {code}")
    return code


if __name__ == "__main__":
    sys.exit(main())
