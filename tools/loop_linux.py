"""The local loop on Linux (tools/loop.py): from Windows, WSL2 reached, the
pins checked and the working tree's snapshot made; inside WSL, by
loop.py's --inside, the one clone synced once a run, then the units of the
way, each job in its own build tree within the clone, build/JOB, kept
across runs (tools/loop_run.py's own_tree; tier 3 empties it, and so does
a change of the toolchain's pins: the pin file's, CMakeLists.txt's or the
prefix).

The clone is CLONE under the clone root (--clone): the loop writes nothing
else there, and deletes nothing outside the clone's untracked files, which
the sync removes, and its build trees, which tier 3 empties. The sync keeps
build/, and leaves a file whose content is the same untouched, so that a
sync that changes nothing leaves Ninja nothing to build. The clones an
earlier loop kept a job under the clone root are no longer read; the sync
names them, for removal by hand.

Run by tools/loop.py, given tools/loop_run.py's, tools/loop_jobs.py's,
tools/loop_tiers.py's and tools/loop_select.py's names (runpy's
init_globals).
"""

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLONE = "clone"  # the one clone, under the clone root
# The sanitizer runtimes, each reading its symbolizer from NAME_SYMBOLIZER_PATH.
SANITIZER_RUNTIMES = ("ASAN", "LSAN", "MSAN", "TSAN", "UBSAN")


def wsl_run(wsl, distro, *argv):
    return capture([wsl, "-d", distro, "-e", *argv])


def distros(wsl):
    try:
        done = subprocess.run([wsl, "-l", "-q"], stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise CannotRun(f"wsl.exe: {e}") from e
    text = done.stdout.decode("utf-16-le", "replace") if b"\x00" in done.stdout else done.stdout.decode("utf-8", "replace")
    return [name.strip() for name in text.replace("\x00", "").splitlines() if name.strip()]


def snapshot(root):
    """A commit object of the working tree, untracked files included and
    ignored ones not, made from a copy of the index: the tree and its index
    are not touched. HEAD itself when nothing differs."""
    git = shutil.which("git")
    if not git:
        raise CannotRun("git is missing from PATH")

    def ask(*argv, env=None):
        status, text = capture([git, *argv], cwd=root, env=env)
        if status != 0:
            raise CannotRun(f"git {argv[0]}: {text.strip()[-200:]}")
        return text.strip()

    gitdir = Path(ask("rev-parse", "--absolute-git-dir"))
    head = ask("rev-parse", "--verify", "HEAD")
    with tempfile.TemporaryDirectory(prefix="qiven3-loop-") as tmp:
        index = Path(tmp) / "index"
        if (gitdir / "index").is_file():
            shutil.copyfile(gitdir / "index", index)
        env = dict(os.environ, GIT_INDEX_FILE=str(index))
        ask("add", "-A", env=env)
        tree = ask("write-tree", env=env)
    if tree == ask("rev-parse", "HEAD^{tree}"):
        return head
    who = {"GIT_AUTHOR_NAME": "qiven3 loop", "GIT_AUTHOR_EMAIL": "loop@localhost",
           "GIT_COMMITTER_NAME": "qiven3 loop", "GIT_COMMITTER_EMAIL": "loop@localhost"}
    return ask("commit-tree", tree, "-p", head, "-m", "the loop's snapshot of the working tree",
               env=dict(os.environ, **who))


def linux(args, report):
    """From Windows: WSL present, the interpreter under the prefix, the pin
    check, the snapshot, then the units inside WSL by loop.py's --inside."""
    begun = now()
    if re.match(r"^[A-Za-z]:", args.prefix) or re.match(r"^[A-Za-z]:", args.clone):
        raise CannotRun("the prefix and the clone are WSL's paths; from Git Bash set MSYS_NO_PATHCONV=1")
    wsl = shutil.which("wsl.exe") or shutil.which("wsl")
    if not wsl:
        raise CannotRun("WSL is absent: no wsl.exe")
    if args.distro not in distros(wsl):
        raise CannotRun(f"WSL has no distribution {args.distro}")
    python = args.prefix.rstrip("/") + "/bin/python3.15"
    if wsl_run(wsl, args.distro, "test", "-x", python)[0] != 0:
        raise CannotRun(f"the interpreter is missing from the prefix: {python}")
    status, source = wsl_run(wsl, args.distro, "wslpath", "-a", "-u", str(ROOT))
    if status != 0:
        raise CannotRun(f"wslpath: {source.strip()[-200:]}")
    source = source.strip()
    named = args.jobs if args.way in ("build", "test") else []
    inner = [wsl, "-d", args.distro, "-e", python, "-I", source + "/tools/loop.py", "--inside", args.way, "linux",
             *named, "--source", source, "--prefix", args.prefix, "--clone", args.clone]
    if args.fuzz_time is not None:
        inner += ["--fuzz-time", str(args.fuzz_time)]
    pin_file = []
    if args.pins:
        status, pins = wsl_run(wsl, args.distro, "wslpath", "-a", "-u", str(Path(args.pins).resolve()))
        if status != 0:
            raise CannotRun(f"wslpath: {pins.strip()[-200:]}")
        pin_file = ["--pins", pins.strip()]
        inner += pin_file
    start = now()
    status, text = wsl_run(wsl, args.distro, python, "-I", source + "/tools/linux_pins.py", "--prefix", args.prefix,
                           *pin_file)
    lines = text.strip().splitlines() or ["no output"]
    if status != 0:
        for line in lines[-20:]:
            report.line(line)
        raise CannotRun(f"the pin check exited {status}")
    report.line(f"loop: {lines[-1]} in {seconds(start)}; prefix {args.prefix}")
    start = now()
    commit = snapshot(ROOT)
    report.line(f"loop: snapshot {commit[:12]} of the working tree in {seconds(start)}")
    return relay(report, [*inner, "--commit", commit], begun)


def relay(report, inner, begun):
    """The inner run's lines, given the lines left after this side's own
    (--lines), its last line this answer's, with the seconds in all since
    begun on this side's clock, the inner run's within them. Returns its
    status."""
    try:
        child = subprocess.Popen([*inner, "--lines", str(report.left + 1)], stdin=subprocess.DEVNULL,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        raise CannotRun(f"wsl.exe: {e}") from e
    last = None
    with child:  # its pipe closed, the child waited for
        for raw in child.stdout:
            if last is not None:
                report.line(last)
            last = raw.decode("utf-8", "replace").rstrip("\r\n")
    status = child.wait()
    if last is not None and status in (0, 1, 2):
        report.last(f"{last}; {seconds(begun)} in all")  # the inner summary keeps its place
    elif last is not None:
        report.line(last)
    if status not in (0, 1, 2):
        raise CannotRun(f"WSL ended, {STATUS['line'](status)}")
    return status


def inside(args, report):
    """Inside WSL, the pins checked: the one clone synced, then each unit
    in its own build tree within it, under its own TMPDIR. From the prefix
    and the pins: QIVEN3_LIBCXX, the pinned g++'s installation for clang's
    jobs (GCC_HOME), and each sanitizer runtime's symbolizer, the pinned
    llvm-symbolizer, which no runtime finds by its versioned name."""
    git = shutil.which("git")
    if not git:
        raise CannotRun("git is missing from WSL's PATH")
    pins = Path(args.pins or ROOT / "tools" / "linux-pins.txt").read_text(encoding="utf-8")  # checked: its form holds
    lines = [x.split("#", 1)[0].split() for x in pins.splitlines()]
    libcxx = [f[4] for f in lines if f[:1] == ["libcxx"]]
    gxx = LINUX_COMPILERS["gcc"][1].split("=", 1)[1]
    gcc = [f[4] for f in lines if f[:2] == ["command", gxx] and f[4].startswith("prefix/")]
    symbolizer = [f[4] for f in lines if f[:1] == ["command"] and f[1].startswith("llvm-symbolizer")
                  and f[4].startswith("prefix/")]
    prefix = args.prefix.rstrip("/")
    # The toolchain's pins each tree records (tools/loop_run.py's own_tree): the pin file's, the pins
    # CMakeLists.txt sets, and the prefix the compilers are found under.
    toolchain = {**file_pins(pins), **cmake_pins(ROOT), "prefix": prefix}
    env = dict(os.environ, **ENV["linux"], QIVEN3_PYTHON=sys.executable)
    env["PATH"] = prefix + "/bin:" + env.get("PATH", "")
    if libcxx:
        env["QIVEN3_LIBCXX"] = prefix + "/" + libcxx[0].removeprefix("prefix/")
    if gcc:  # the driver's link, prefix/bin/g++-N, resolved: INSTALL/bin/g++
        env["QIVEN3_GCC"] = str(Path(prefix + "/" + gcc[0].removeprefix("prefix/")).resolve().parents[1])
    for runtime in SANITIZER_RUNTIMES if symbolizer else ():
        env[runtime + "_SYMBOLIZER_PATH"] = prefix + "/" + symbolizer[0].removeprefix("prefix/")
    base = Path(os.path.expanduser(args.clone))
    if not base.is_absolute() or str(base.resolve()).startswith("/mnt/"):
        raise CannotRun(f"the clone {base} must be an absolute path on WSL's own file system, never under /mnt/")
    clone = base / CLONE
    phase = "build" if args.way == "build" else "test"
    units = plan(args.way, "linux", args.jobs, args.fuzz_time, ROOT)
    presets = binary_dirs(ROOT)
    report.plan(2 + planned_lines(units, phase))
    start = now()
    sync(git, clone, args.source, args.commit)
    old = earlier_clones(base)
    unused = f"; unused beside it, an earlier loop's clones: {', '.join(old)}" if old else ""
    report.line(f"loop: sync {args.commit[:12]} in {seconds(start)}; clone {clone}{unused}", True)
    logs = clone / "build" / "loop"
    if phase == "test":  # the affected tests, and each passing step's record (tools/loop_select.py)
        select = Selection(clone, shutil.which("ctest", path=env["PATH"]) or "ctest", env, git)
        for unit in units:
            unit.select = select
    counts = [0, 0, 0]
    for unit in units:
        # The job's own temporary directory, as CI's RUNNER_TEMP: /tmp's bare name would match any "tmp".
        temp = tempfile.mkdtemp(prefix=f"qiven3-loop-{unit.name}-")
        try:
            got = drive(report, [unit], clone, logs, linux_command(clone, logs, env, presets, temp),
                        dict(env, TMPDIR=temp), phase, toolchain)
        finally:
            shutil.rmtree(temp, ignore_errors=True)
        counts = [a + b for a, b in zip(counts, got)]
    report.line(f"loop: logs in {logs}", True)
    return counts


def linux_command(clone, logs, env, presets, temp):
    """command(unit, step): the step's argv in the clone, under setarch -R,
    its tool by the prefix's PATH, GCC_HOME by QIVEN3_GCC; the clean check
    given the job's TMPDIR."""

    def command(unit, step):
        argv = local_argv(unit, step, presets)
        if argv is None:
            return None
        if argv[0] == CLEAN:
            script = clean_script(clone, logs / unit.name)
            return [*LINUX_SHELL, sys.executable, "-I", str(script), argv[1], str(clone), temp]
        if argv[0] == FUZZ_TIMED:
            return [*LINUX_SHELL, sys.executable, "-I", str(clone / "tools" / "loop_fuzz.py"), *argv[1:]]
        rest = [a.replace(GCC_HOME, env.get("QIVEN3_GCC", GCC_HOME)) for a in argv[1:]]
        return [*LINUX_SHELL, shutil.which(argv[0], path=env["PATH"]) or argv[0], *rest]

    return command


def earlier_clones(base):
    """The clones an earlier loop kept a Linux job under the clone root,
    named and never read nor deleted."""
    return [name for name, job in JOBS.items() if job[0] == "linux" and (base / name / ".git").is_dir()]


def sync(git, clone, source, commit):
    """The clone at the commit, fetched from the Windows checkout: build/
    kept, untracked files removed, a file whose content is the same left
    untouched (git's checkout rewrites only what differs)."""
    clone.mkdir(parents=True, exist_ok=True)
    steps = [] if (clone / ".git").is_dir() else [["init", "-q"]]
    # Protocol v2 serves an unadvertised commit; the option asks it of v0.
    steps += [["-c", "safe.directory=*", "-c", "uploadpack.allowAnySHA1InWant=true", "fetch", "-q", "--no-tags",
               source, commit],
              ["checkout", "-q", "--force", "--detach", commit],
              ["clean", "-q", "-ffdx", "--exclude=/build/"]]
    for argv in steps:
        status, text = capture([git, *argv], cwd=clone)
        if status != 0:
            raise CannotRun(f"git {argv[0] if argv[0] != '-c' else 'fetch'} in {clone}: {text.strip()[-200:]}")
