"""The local loop on Windows (tools/loop.py): vcvars' shell, the pinned
tools, and the units of the way run in their own trees under the checkout's
build/.

Run by tools/loop.py, given tools/loop_run.py's, tools/loop_jobs.py's and
tools/loop_tiers.py's names (runpy's init_globals).
"""

import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
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
# windows-analysis's clang-tidy, LLVM's: its version and the file's SHA-256.
CLANG_TIDY = ("19.1.7", "231c16866d578941c325b7fffc792b66bc5bf0b5622258894fc89836e8d5d77a")


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


def named(env, name):
    """env's value of name, the name read without regard to case, as
    Windows reads it; None when it is not set."""
    return next((value for key, value in env.items() if key.upper() == name.upper()), None)


def check_windows_tools(env):
    """Each pinned tool on vcvars' PATH by SHA-256 and version, and the
    toolset and SDK vcvars chose; the tools' full paths."""
    found, bad = {}, []
    for name, version, digest in WINDOWS_PINS:
        path = shutil.which(name, path=named(env, "PATH"))
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
    toolset, sdk = named(env, "VCToolsVersion"), named(env, "WindowsSDKVersion")
    if (toolset or "").strip() != VCVARS_VER:
        bad.append(f"vcvars' toolset is {toolset!r}; the pin is {VCVARS_VER}")
    if (sdk or "").rstrip("\\/") != SDK:
        bad.append(f"vcvars' SDK is {sdk!r}; the pin is {SDK}")
    if bad:
        raise CannotRun("; ".join(bad))
    return found


def check_clang_tidy(env):
    """The pinned clang-tidy by SHA-256 and version; its full path."""
    path = Path(os.environ.get("QIVEN3_CLANG_TIDY") or Path(os.environ.get("ProgramFiles", "")) / "LLVM" / "bin"
                / "clang-tidy.exe")
    if not path.is_file():
        raise CannotRun(f"clang-tidy is missing: {path}; name it by QIVEN3_CLANG_TIDY")
    version, digest = CLANG_TIDY
    got = sha256(path)
    status, text = capture([str(path), "--version"], env=env)
    if got != digest or status != 0 or not re.search(r"LLVM version " + re.escape(version) + r"(?![\w.])", text):
        raise CannotRun(f"clang-tidy: {path}'s SHA-256 {got}, --version {text.strip()[-80:]!r}; "
                        f"the pin is {version}, {digest}")
    return str(path)


def windows_command(root, logs, python, tools, extra, presets):
    """command(unit, step): the step's argv, its tool by its pinned path."""

    def command(unit, step):
        argv = local_argv(unit, step, presets)
        if argv[0] == CLEAN:
            return [python, "-I", str(clean_script(root, logs / unit.name)), argv[1], str(root),
                    tempfile.gettempdir()]
        if argv[0] == FUZZ_TIMED:
            return [python, "-I", str(root / "tools" / "loop_fuzz.py"), *argv[1:]]
        tail = extra if step.name == "configure" and "ci-analysis" in step.ci else ()
        return [tools[argv[0]], *argv[1:], *tail]

    return command


def windows(args, report):
    logs = ROOT / "build" / "loop"
    python = check_python(sys.executable)
    env = vcvars_env(logs)
    tools = check_windows_tools(env)
    env.update(ENV["windows"], QIVEN3_PYTHON=python)
    presets = binary_dirs(ROOT)
    units = plan(args.way, "windows", args.jobs, args.fuzz_time, ROOT)
    phase = "build" if args.way == "build" else "test"
    report.plan(1 + planned_lines(units, phase))
    report.line(f"loop: windows tools hold: cmake, ctest {WINDOWS_PINS[0][1]}, ninja {WINDOWS_PINS[2][1]}, "
                f"toolset {VCVARS_VER}, SDK {SDK}, CPython 3.15.0, each by its SHA-256")
    extra = ("-DQIVEN3_CLANG_TIDY=" + check_clang_tidy(env),) if any(u.name == "windows-analysis" for u in units) \
        else ()
    counts = drive(report, units, ROOT, logs, windows_command(ROOT, logs, python, tools, extra, presets), env, phase)
    report.line(f"loop: logs in {logs}", True)
    return counts
