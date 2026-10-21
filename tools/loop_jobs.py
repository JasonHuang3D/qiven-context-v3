"""The local loop's jobs (tools/loop.py): CI's jobs, presets and steps, with
the local pins.

Each job is a job of .github/workflows/ci.yml or msan.yml but macOS's, by
the same preset, and each of its steps runs CI's command, in CI's order. CI
is unpinned and runs on the images' own tools; the pins are the loop's
alone, as each step's local additions, apart from CI's command, so that a
test reads one against the other (tests/drive/test_loop_ci.py):
- every configure adds a fresh cache and QIVEN3_PIN_TOOLS, under which
  configure fails on any compiler, CMake, Ninja or CPython but the pinned;
- the Linux configures name the pinned compilers, gcc-14 and g++-14 or
  clang-18 and clang++-18, where CI's presets take the image's gcc, g++,
  clang and clang++ (tools/linux-pins.txt pins them);
- windows-analysis's configure names the pinned clang-tidy, which the loop
  checks by SHA-256 and version (tools/loop.py's CLANG_TIDY).

Beyond CI's, the loop's own jobs, LOCAL, which CI never runs: on Windows
windows-symbols, the release preset keeping its PDB beside each binary,
running the fake cases and then the debugger cases (the debugger test
preset), so that K8, K16, K17 and the plants' names run inside the loop.
Each Windows job builds in its own tree, TREES', build/ci by default. And
the loop's own steps, LOCAL_STEPS, after CI's in a job of CI's: on Linux,
linux-plain's build, with its debug information, runs the debugger cases
under gdb.

Loaded by runpy.run_path: under -I this directory is off sys.path.
"""

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

# The shell CI's Linux steps run in, before its bash: address-space
# randomisation off for the step's own processes (setarch -R), the
# system's setting left as it is.
LINUX_SHELL = ("setarch", "x86_64", "-R")

CLEAN = "clean-check"  # CI's clean-binaries check, its script read from ci.yml
PIN = "-DQIVEN3_PIN_TOOLS=ON"

# The pinned compilers of the Linux jobs, by the compiler CI's preset names.
LINUX_COMPILERS = {
    "gcc": ("-DCMAKE_C_COMPILER=gcc-14", "-DCMAKE_CXX_COMPILER=g++-14"),
    "clang": ("-DCMAKE_C_COMPILER=clang-18", "-DCMAKE_CXX_COMPILER=clang++-18"),
}


class Step:
    """One step: its name, its command as CI writes it, what the loop adds,
    its phase, and the step that must have passed before it runs."""

    __slots__ = ("name", "ci", "local", "phase", "after")

    def __init__(self, name, ci, phase="test", local=(), after="build"):
        self.name, self.ci, self.phase, self.local, self.after = name, tuple(ci), phase, tuple(local), after


def windows_job(preset, clean=False, analysis=False):
    steps = [Step("configure", ("cmake", "--preset", preset), "build", ("--fresh", PIN), None),
             Step("build", ("cmake", "--build", "--preset", preset), "build", after="configure"),
             Step("fake", ("ctest", "--preset", "fake"))]
    if analysis:
        steps.append(Step("analysis controls", ("ctest", "--preset", "ci-analysis", "--no-tests=error")))
    if clean:
        steps.append(Step("clean binaries", (CLEAN, "build/ci")))
    if analysis:
        steps += [Step("structure configure", ("cmake", "--preset", "ci-windows", "-B", "build/ci-structure",
                                               "-DQIVEN3_STRUCTURE=ON"), local=(PIN,), after=None),
                  Step("structure build", ("cmake", "--build", "build/ci-structure"), after="structure configure"),
                  Step("structure tests", ("ctest", "--test-dir", "build/ci-structure", "--output-on-failure",
                                           "--no-tests=error", "-R", "^tests[.]structure[.]", "-LE", "^analysis$"),
                       after="structure build")]
    return ("windows", preset, steps)


def linux_job(preset, compiler, fuzz=False, clean=False, cxx=False):
    steps = [Step("configure", ("cmake", "--preset", preset), "build", ("--fresh", PIN, *LINUX_COMPILERS[compiler]),
                  None),
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


def symbols_job():
    """windows-symbols: release, its PDBs kept; fake's selection on its
    tree, then the debugger cases."""
    steps = [Step("configure", ("cmake", "--preset", "release"), "build", ("--fresh", PIN), None),
             Step("build", ("cmake", "--build", "--preset", "release"), "build", after="configure"),
             Step("fake", ("ctest", "--test-dir", "build/release", "--output-on-failure", "--no-tests=error",
                           "-L", "^fake$", "-LE", "^debugger$")),
             Step("debugger", ("ctest", "--preset", "debugger"))]
    return ("windows", "release", steps)


# Every job of CI's the loop runs, as CI's workflows name it: (system, preset, steps).
CI_JOBS = {
    "windows-msvc": windows_job("ci-windows", clean=True),
    "windows-asan": windows_job("ci-asan"),
    "windows-stack": windows_job("ci-stack"),
    "windows-analysis": windows_job("ci-analysis", analysis=True),
    "linux-gcc": linux_job("linux-gcc", "gcc"),
    "linux-clang": linux_job("linux-clang", "clang"),
    "linux-tsan": linux_job("linux-tsan", "clang"),
    "linux-fuzz": linux_job("linux-fuzz", "clang", fuzz=True),
    "linux-plain": linux_job("linux-plain", "gcc", clean=True),
    "linux-msan": linux_job("linux-msan", "clang", cxx=True),
}

# The loop's own steps, local alone, after CI's steps of a job of CI's.
LOCAL_STEPS = {
    "linux-plain": (Step("debugger", ("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=error",
                                      "-L", "^debugger$")),),
}
# The loop's own jobs, local alone (CI never runs them), and every job.
LOCAL = {"windows-symbols": symbols_job()}
JOBS = {**{name: (system, preset, [*steps, *LOCAL_STEPS.get(name, ())])
           for name, (system, preset, steps) in CI_JOBS.items()}, **LOCAL}
# Each Windows job's tree, build/ci when it is not named here.
TREES = {"windows-symbols": "build/release"}
