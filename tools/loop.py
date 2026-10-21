"""The local loop: one command per phase, on Windows or in WSL2's Linux.

Run from a checkout on Windows by the pinned interpreter:

    py -3.15 -I tools/loop.py PHASE SYSTEM [JOB ...] [options]

PHASE is build (configure and build) or test (every step of the job, the
cases after the build). SYSTEM is windows or linux. Each JOB is a job's
name or its preset's (windows-msvc or ci-windows); none names every job of
the system. The jobs and their steps are CI's (.github/workflows/ci.yml and
msan.yml), JOBS below, which a test checks against the workflows; macOS
stays with CI alone.

- windows: a shell set up by vcvars64.bat -vcvars_ver=14.44.35207 (found by
  vswhere, or QIVEN3_VCVARS), the tools of WINDOWS_PINS checked by SHA-256
  and version, CPython 3.15.0 this interpreter; each configure with
  QIVEN3_PIN_TOOLS on. The build runs in the checkout; the logs are kept in
  build/loop/JOB/.
- linux: reaches WSL2 (--distro, QIVEN3_WSL_DISTRO, default Ubuntu-24.04),
  checks tools/linux-pins.txt by tools/linux_pins.py under the toolchain's
  prefix (--prefix or QIVEN3_PREFIX, no default), then brings the working
  tree, uncommitted changes and new files included, to a clone on WSL's own
  file system (--clone, QIVEN3_LOOP_CLONE, default ~/qiven3-loop), one a
  job, by a commit object made from a copy of the index: the Windows tree
  and its index are not touched. The logs are kept in the clone's
  build/loop/. From Git Bash, set MSYS_NO_PATHCONV=1 before giving a POSIX
  path as an argument, or give it by the environment.

The answer is plain text: a line a step with its outcome and seconds, a
failing step's last 30 lines, at most 100 lines in all. Exit 0 when every
step passed, 1 when a step failed, 2 when the loop could not run (a tool
missing, a pin off, WSL absent, a bad argument).
"""

import argparse
import hashlib
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATUS = runpy.run_path(str(ROOT / "tools" / "status.py"))  # under -I this directory is off sys.path
LINES = 100
TAIL = 30
WIDTH = 400  # a tail line's characters at most
STEP_TIMEOUT = 45 * 60  # CI's job limit
DISTRO = "Ubuntu-24.04"
CLONE = "~/qiven3-loop"
VCVARS_VER = "14.44.35207"
SDK = "10.0.26100.0"
PYTHON = (3, 15, 0, "final")

# Windows' pinned tools: the name on vcvars' PATH, the version --version
# gives (None: the toolset's, by VCToolsVersion), the file's SHA-256.
WINDOWS_PINS = (
    ("cmake", "4.4.3", "ab8247ca4554871e5d75c0ee118ee8663727bd6fdf37ad76b17e9cc8d5f286a8"),
    ("ctest", "4.4.3", "083f482c8656c86932c28906445cb3c10c0c5ee649a90ff3e0080ee4447d3dfc"),
    ("ninja", "1.13.2", "e52a7ad9538d9618c67a0bd777964e2eec8a30f68b810a2f6adce1f2daf847b8"),
    ("cl", None, "88c8344236a27a6e727e0a8edc49aaa2690bdc7a9464b9d18cc7abe70a9f1c0d"),
    ("link", None, "ca11e6c45debd34bf652dfe984c5360a531a005ed78bf72852330c9c2590cf0d"),
)

# The environment CI's jobs set, by system (ci.yml's and msan.yml's env).
ENV = {
    "windows": {"CTEST_PARALLEL_LEVEL": "4"},
    "linux": {
        "CTEST_PARALLEL_LEVEL": "4",
        "ASAN_OPTIONS": "detect_leaks=1:detect_stack_use_after_return=1:abort_on_error=0",
        "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1",
        "TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1",
        "MSAN_OPTIONS": "halt_on_error=1",
    },
}

CLEAN = "clean-check"  # CI's clean-binaries check, its script read from ci.yml


class Step:
    """One step: its name, its command as CI writes it, what the loop adds,
    its phase, and the step that must have passed before it runs."""

    __slots__ = ("name", "ci", "local", "phase", "after")

    def __init__(self, name, ci, phase="test", local=(), after="build"):
        self.name, self.ci, self.phase, self.local, self.after = name, tuple(ci), phase, tuple(local), after


def windows_job(preset, clean=False, analysis=False):
    steps = [Step("configure", ("cmake", "--preset", preset), "build", ("--fresh", "-DQIVEN3_PIN_TOOLS=ON"), None),
             Step("build", ("cmake", "--build", "--preset", preset), "build", after="configure"),
             Step("fake", ("ctest", "--preset", "fake"))]
    if analysis:
        steps.append(Step("analysis controls", ("ctest", "--preset", "ci-analysis", "--no-tests=error")))
    if clean:
        steps.append(Step("clean binaries", (CLEAN, "build/ci")))
    if analysis:
        steps += [Step("structure configure", ("cmake", "--preset", "ci-windows", "-B", "build/ci-structure",
                                               "-DQIVEN3_STRUCTURE=ON"), local=("-DQIVEN3_PIN_TOOLS=ON",), after=None),
                  Step("structure build", ("cmake", "--build", "build/ci-structure"), after="structure configure"),
                  Step("structure tests", ("ctest", "--test-dir", "build/ci-structure", "--output-on-failure",
                                           "--no-tests=error", "-R", "^tests[.]structure[.]", "-LE", "^analysis$"),
                       after="structure build")]
    return ("windows", preset, steps)


def linux_job(preset, fuzz=False, clean=False, cxx=False):
    steps = [Step("configure", ("cmake", "--preset", preset), "build", ("--fresh",), None),
             Step("build", ("cmake", "--build", "--preset", preset, "--", "-k", "0"), "build", after="configure"),
             Step("fake", ("ctest", "--preset", "fake"))]
    if fuzz:
        steps.append(Step("fuzz", ("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=ignore",
                                   "-L", "^fuzz$")))
    if cxx:
        steps.append(Step("cxx", ("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=error",
                                  "-L", "^cxx$")))
    if clean:
        steps.append(Step("clean binaries", (CLEAN, "build/ci")))
    return ("linux", preset, steps)


# Every job the loop runs, as CI's workflows name it: (system, preset, steps).
JOBS = {
    "windows-msvc": windows_job("ci-windows", clean=True),
    "windows-asan": windows_job("ci-asan"),
    "windows-stack": windows_job("ci-stack"),
    "windows-analysis": windows_job("ci-analysis", analysis=True),
    "linux-gcc": linux_job("linux-gcc"),
    "linux-clang": linux_job("linux-clang"),
    "linux-tsan": linux_job("linux-tsan"),
    "linux-fuzz": linux_job("linux-fuzz", fuzz=True),
    "linux-plain": linux_job("linux-plain", clean=True),
    "linux-msan": linux_job("linux-msan", cxx=True),
}


class CannotRun(Exception):
    """The loop could not run: exit 2."""


class Report:
    """The answer, within its line bound: the last line always has its
    place, and a failing tail leaves room for the lines still planned."""

    def __init__(self, budget):
        self.left = budget - 1  # the last line's place kept
        self.planned = 0
        self.dropped = 0

    def plan(self, count):
        self.planned += count

    def line(self, text, planned=False):
        if planned:
            self.planned = max(0, self.planned - 1)
        if self.left > 0:
            print(text[:WIDTH], flush=True)
            self.left -= 1
        else:
            self.dropped += 1

    def last(self, text):
        if self.dropped:
            text += f" ({self.dropped} lines left out by the bound)"
        print(text[:WIDTH], flush=True)

    def tail(self, log):
        """A failing log's last lines, as many of TAIL as leave room."""
        room = min(TAIL, self.left - self.planned - 1)
        if room <= 0:
            return
        try:
            lines = log.read_bytes().decode("utf-8", "replace").splitlines()
        except OSError as e:
            self.line(f"  (the log cannot be read: {e})")
            return
        shown = lines[-room:]
        self.line(f"  its last {len(shown)} of {len(lines)} lines:")
        for text in shown:
            self.line("  | " + text)


def seconds(start):
    return f"{time.monotonic() - start:.1f} s"


def slug(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def chosen(steps, phase):
    return [s for s in steps if phase == "test" or s.phase == "build"]


def run_steps(report, job, steps, phase, command, logs, cwd, env):
    """Each step of the phase, its line and, on a failure, its tail. command
    maps a step to its argv. Returns (passed, failed, skipped)."""
    chosen_steps = chosen(steps, phase)
    outcome = {}
    counts = [0, 0, 0]
    logs.mkdir(parents=True, exist_ok=True)
    for old in logs.glob("*.log"):
        old.unlink()
    for index, step in enumerate(chosen_steps, 1):
        if step.after is not None and outcome.get(step.after) is not True:
            outcome[step.name] = False
            counts[2] += 1
            report.line(f"{job} {step.name}: skipped, {step.after} did not pass", True)
            continue
        log = logs / f"{index:02d}-{slug(step.name)}.log"
        start = time.monotonic()
        try:
            argv = command(step)
            with open(log, "wb") as out:
                out.write(("$ " + " ".join(argv) + "\n").encode("utf-8"))
                out.flush()
                status = subprocess.run(argv, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=out,
                                        stderr=subprocess.STDOUT, timeout=STEP_TIMEOUT).returncode
        except subprocess.TimeoutExpired:
            status = f"timed out after {STEP_TIMEOUT} s"
        except OSError as e:
            raise CannotRun(f"{job} {step.name}: cannot start: {e}") from e
        outcome[step.name] = status == 0
        if status == 0:
            counts[0] += 1
            report.line(f"{job} {step.name}: pass in {seconds(start)}", True)
        else:
            counts[1] += 1
            what = status if isinstance(status, str) else STATUS["line"](status)
            report.line(f"{job} {step.name}: FAIL, {what}, in {seconds(start)}; log {log}", True)
            report.tail(log)
    return counts


def clean_script(root, logs):
    """CI's clean-binaries check, ci.yml's CLEAN_CHECK, written beside the logs."""
    text = (root / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    match = re.search(r"^  CLEAN_CHECK: \|\n((?:    .*\n|\n)+)", text, re.MULTILINE)
    if not match:
        raise CannotRun("ci.yml holds no CLEAN_CHECK")
    script = logs / "clean_check.py"
    logs.mkdir(parents=True, exist_ok=True)
    script.write_text("".join(line[4:] + "\n" for line in match.group(1).splitlines()), encoding="utf-8")
    return script


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def capture(argv, **kw):
    """The command's status and output; CannotRun when it cannot start."""
    try:
        done = subprocess.run(argv, stdin=subprocess.DEVNULL, capture_output=True, timeout=300, **kw)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise CannotRun(f"{Path(argv[0]).name}: {e}") from e
    return done.returncode, done.stdout.decode("utf-8", "replace") + done.stderr.decode("utf-8", "replace")


def check_python(python):
    if tuple(sys.version_info[:4]) != PYTHON:
        raise CannotRun(f"the interpreter is {sys.version.split()[0]}; the pin is CPython 3.15.0")
    return python


def vcvars_env(logs):
    """The environment vcvars64.bat sets up, in a shell of its own."""
    vcvars = os.environ.get("QIVEN3_VCVARS")
    if not vcvars:
        where = Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        if not where.is_file():
            raise CannotRun("vswhere.exe is missing; name vcvars64.bat by QIVEN3_VCVARS")
        status, text = capture([str(where), "-latest", "-products", "*", "-requires",
                                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"])
        if status != 0 or not text.strip():
            raise CannotRun("vswhere finds no Visual Studio with the C++ tools")
        vcvars = str(Path(text.strip().splitlines()[0]) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat")
    if not Path(vcvars).is_file():
        raise CannotRun(f"vcvars64.bat is missing: {vcvars}")
    logs.mkdir(parents=True, exist_ok=True)
    script = logs / "vcvars.cmd"
    script.write_text(f'@call "{vcvars}" -vcvars_ver={VCVARS_VER} >nul || exit /b 1\r\n@set\r\n', encoding="ascii")
    status, text = capture(["cmd.exe", "/d", "/c", str(script)])
    if status != 0:
        raise CannotRun(f"vcvars64.bat -vcvars_ver={VCVARS_VER} failed ({status}): {text.strip()[-200:]}")
    env = {}
    for line in text.splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            env[key] = value
    return env


def check_windows_tools(env):
    """Each pinned tool on vcvars' PATH by SHA-256 and version, and the
    toolset and SDK vcvars chose; the tools' full paths."""
    found, bad = {}, []
    for name, version, digest in WINDOWS_PINS:
        path = shutil.which(name, path=env.get("Path") or env.get("PATH"))
        if not path:
            bad.append(f"{name}: missing from vcvars' PATH")
            continue
        found[name] = path
        got = sha256(path)
        if got != digest:
            bad.append(f"{name}: {path}'s SHA-256 is {got}; the pin {digest}")
        if version:
            status, text = capture([path, "--version"], env=env)
            if status != 0 or not re.search(r"(?<![\w.])" + re.escape(version) + r"(?![\w.])", text):
                bad.append(f"{name}: --version gives {text.strip()[:80]!r}; the pin is {version}")
    if env.get("VCToolsVersion", "").strip() != VCVARS_VER:
        bad.append(f"vcvars' toolset is {env.get('VCToolsVersion')!r}; the pin is {VCVARS_VER}")
    if env.get("WindowsSDKVersion", "").rstrip("\\/") != SDK:
        bad.append(f"vcvars' SDK is {env.get('WindowsSDKVersion')!r}; the pin is {SDK}")
    if bad:
        raise CannotRun("; ".join(bad))
    return found


def own_tree(report, tree, job):
    """Every Windows job builds in build/ci, as CI's do, each on a clean
    checkout: a tree another job left is emptied first, so that no file of
    its build stays (the clean-binaries check reads every file)."""
    marker = tree / ".qiven3-loop-job"
    held = marker.read_text(encoding="utf-8").strip() if marker.is_file() else None
    if tree.exists() and held != job:
        try:
            shutil.rmtree(tree)
        except OSError as e:
            raise CannotRun(f"{tree} cannot be emptied: {e}") from e
        report.line(f"{job} tree: {tree} emptied, it held {held or 'a build of no job of the loop'}", True)
    else:
        report.line(f"{job} tree: {tree} {'kept, its own' if held else 'new'}", True)
    tree.mkdir(parents=True, exist_ok=True)
    marker.write_bytes(job.encode("utf-8") + b"\n")


def windows(args, report):
    logs = ROOT / "build" / "loop"
    python = check_python(sys.executable)
    env = vcvars_env(logs)
    tools = check_windows_tools(env)
    env.update(ENV["windows"], QIVEN3_PYTHON=python)
    report.plan(1 + sum(1 + len(chosen(JOBS[job][2], args.phase)) for job in args.jobs))
    report.line(f"loop: windows tools hold: cmake, ctest {WINDOWS_PINS[0][1]}, ninja {WINDOWS_PINS[2][1]}, "
                f"toolset {VCVARS_VER}, SDK {SDK}, CPython 3.15.0, each by its SHA-256")
    extra = ("-DQIVEN3_CLANG_TIDY=" + os.environ["QIVEN3_CLANG_TIDY"],) if os.environ.get("QIVEN3_CLANG_TIDY") else ()

    def command(step):
        if step.ci[0] == CLEAN:
            return [python, "-I", str(clean_script(ROOT, logs)), step.ci[1], str(ROOT), tempfile.gettempdir()]
        tail = step.local + (extra if step.name == "configure" and "ci-analysis" in step.ci else ())
        return [tools[step.ci[0]], *step.ci[1:], *tail]

    counts = [0, 0, 0]
    for job in args.jobs:
        own_tree(report, ROOT / "build" / "ci", job)
        got = run_steps(report, job, JOBS[job][2], args.phase, command, logs / job, ROOT, env)
        counts = [a + b for a, b in zip(counts, got)]
    report.line(f"loop: logs in {logs}", True)
    return counts


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
    check, the snapshot, then the jobs inside WSL by this file's --inside."""
    begun = time.monotonic()
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
    inner = [wsl, "-d", args.distro, "-e", python, "-I", source + "/tools/loop.py", "--inside", args.phase, "linux",
             *args.jobs, "--source", source, "--prefix", args.prefix, "--clone", args.clone,
             "--lines", str(report.left)]
    pin_file = []
    if args.pins:
        status, pins = wsl_run(wsl, args.distro, "wslpath", "-a", "-u", str(Path(args.pins).resolve()))
        if status != 0:
            raise CannotRun(f"wslpath: {pins.strip()[-200:]}")
        pin_file = ["--pins", pins.strip()]
        inner += pin_file
    start = time.monotonic()
    status, text = wsl_run(wsl, args.distro, python, "-I", source + "/tools/linux_pins.py", "--prefix", args.prefix,
                           *pin_file)
    lines = text.strip().splitlines() or ["no output"]
    if status != 0:
        for line in lines[-20:]:
            report.line(line)
        raise CannotRun(f"the pin check exited {status}")
    report.line(f"loop: {lines[-1]} in {seconds(start)}; prefix {args.prefix}")
    start = time.monotonic()
    commit = snapshot(ROOT)
    report.line(f"loop: snapshot {commit[:12]} of the working tree in {seconds(start)}")
    inner += ["--commit", commit]
    try:
        child = subprocess.Popen(inner, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as e:
        raise CannotRun(f"wsl.exe: {e}") from e
    last = None
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
    """Inside WSL, the pins checked: each job in its clone."""
    git = shutil.which("git")
    if not git:
        raise CannotRun("git is missing from WSL's PATH")
    pins = Path(args.pins or ROOT / "tools" / "linux-pins.txt").read_text(encoding="utf-8")  # checked: its form holds
    libcxx = [f[4] for f in (x.split("#", 1)[0].split() for x in pins.splitlines()) if f[:1] == ["libcxx"]]
    prefix = args.prefix.rstrip("/")
    env = dict(os.environ, **ENV["linux"], QIVEN3_PYTHON=sys.executable)
    env["PATH"] = prefix + "/bin:" + env.get("PATH", "")
    if libcxx:
        env["QIVEN3_LIBCXX"] = prefix + "/" + libcxx[0].removeprefix("prefix/")
    base = Path(os.path.expanduser(args.clone))
    if not base.is_absolute() or str(base.resolve()).startswith("/mnt/"):
        raise CannotRun(f"the clone {base} must be an absolute path on WSL's own file system, never under /mnt/")
    report.plan(1 + sum(1 + len(chosen(JOBS[job][2], args.phase)) for job in args.jobs))
    counts = [0, 0, 0]
    for job in args.jobs:
        clone = base / job
        start = time.monotonic()
        sync(git, clone, args.source, args.commit)
        report.line(f"{job} sync: {args.commit[:12]} in {seconds(start)}; clone {clone}", True)
        # The job's own temporary directory, as CI's RUNNER_TEMP: /tmp's bare name would match any "tmp".
        temp = tempfile.mkdtemp(prefix=f"qiven3-loop-{job}-")

        def command(step):
            if step.ci[0] == CLEAN:
                return [sys.executable, "-I", str(clean_script(clone, clone / "build" / "loop" / job)), step.ci[1],
                        str(clone), temp]
            return [shutil.which(step.ci[0], path=env["PATH"]) or step.ci[0], *step.ci[1:], *step.local]

        try:
            got = run_steps(report, job, JOBS[job][2], args.phase, command, clone / "build" / "loop" / job, clone,
                            dict(env, TMPDIR=temp))
        finally:
            shutil.rmtree(temp, ignore_errors=True)
        counts = [a + b for a, b in zip(counts, got)]
    where = f"{base}/{args.jobs[0]}/build/loop/{args.jobs[0]}" if len(args.jobs) == 1 else f"{base}/JOB/build/loop/JOB"
    report.line(f"loop: logs in {where}", True)
    return counts


def sync(git, clone, source, commit):
    """The clone at the commit, fetched from the Windows checkout; build/ kept."""
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


class Parser(argparse.ArgumentParser):
    def error(self, message):
        print(f"loop: {message} (see --help)")
        sys.exit(2)


def parse(argv):
    parser = Parser(prog="loop.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("phase", choices=("build", "test"))
    parser.add_argument("system", choices=("windows", "linux"))
    parser.add_argument("jobs", nargs="*", metavar="JOB", help="a job or its preset; none: every job of the system")
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
    if unknown:
        parser.error(f"no {args.system} job {', '.join(unknown)}; the jobs: {', '.join(sorted(names))}")
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


def main(argv=None):
    args = parse(argv)
    report = Report(args.lines)
    start = time.monotonic()
    if not args.inside:
        report.line(f"loop: {args.phase} {args.system}: {', '.join(args.jobs)}")
    try:
        if args.inside:
            counts = inside(args, report)
        elif args.system == "windows":
            counts = windows(args, report)
        else:
            return linux(args, report)
    except CannotRun as e:
        report.last(f"loop: cannot run: {e}; exit 2")
        return 2
    except Exception as e:  # an error of the loop's own is not a failing step
        report.last(f"loop: internal error: {type(e).__name__}: {e}; exit 2")
        return 2
    code = 1 if counts[1] else 0
    report.last(f"loop: {counts[0]} passed, {counts[1]} failed, {counts[2]} skipped in {seconds(start)}; exit {code}")
    return code


if __name__ == "__main__":
    sys.exit(main())
