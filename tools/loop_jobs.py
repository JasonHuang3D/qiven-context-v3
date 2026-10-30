"""The local loop's jobs (tools/loop.py): CI's jobs, presets and steps, with
the local pins.

Each job is a job of .github/workflows/ci.yml or msan.yml, by
the same preset, and each of its steps runs CI's command, in CI's order. CI
is unpinned and runs on the images' own tools; the pins are the loop's
alone, as each step's local additions, apart from CI's command, so that a
test reads one against the other (tests/drive/test_loop_ci.py):
- every configure adds QIVEN3_PIN_TOOLS, under which configure fails on
  any compiler, CMake, Ninja or CPython but the pinned (tier 3 adds a fresh
  cache, tools/loop_tiers.py);
- the Linux configures name the pinned compilers, gcc-13 and g++-13 or
  clang-18 and clang++-18, where CI's presets take the image's gcc, g++,
  clang and clang++ (tools/linux-pins.txt pins them); clang takes the
  system's only GCC, the pinned one, for its C++ library and start files;
- windows-analysis's configure names the pinned clang-tidy, which the loop
  checks by SHA-256 and version (tools/loop_windows.py's CLANG_TIDY).

Beyond CI's, the loop's own jobs, LOCAL, which CI never runs: on Windows
windows-symbols, the release preset keeping its PDB beside each binary,
running the fake cases and then the debugger cases (the debugger test
preset), so that K8, K16, K17 and the plants' names run inside the loop.
And the loop's own steps, LOCAL_STEPS, after CI's in a job of CI's: on
Linux, linux-plain's build, with its debug information, runs the debugger
cases under gdb.

The tiers' own (tools/loop_tiers.py): TIER1, the one job a system tier 1
runs, and TIER1_STEPS, the tests of fast's label it adds on that job's own
build, those its fake step does not run, with no structure build: the
structure tests run in windows-analysis's own steps (tier 2) and in tier 3's
full suite; FAST, CI's, the fast test preset's selection, which the tier's
own filters narrow; TIER2_STEPS, the step tier 2 adds on a job's build: the
Windows drive's contract files on windows-msvc's;
SUITES, tier 3's full suite and fuzz targets, once a system after its jobs;
TIMED_ONLY, the suites tier 3 runs only for a time, a job's own step
running the same tests without one.
TREES maps each job's directories, as CI's commands name them, to the job's
own trees, kept across runs: CI's jobs share build/ci, a clean runner each,
where the loop's keep a tree each on one machine, Windows' in the checkout,
Linux's in the one clone. READS maps a suite's directory to a job's tree,
which the suite reads and never owns.

Loaded by runpy.run_path: under -I this directory is off sys.path.
"""

# The environment CI's jobs set, by system (ci.yml's and msan.yml's env),
# then by job (JOB_ENV); the loop sets a job's both on each of its steps.
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
# A job's own, over its system's: ci.yml's matrix entry's. windows-asan runs
# ctest at 2, where four store_sqlite fault parts at once stalled it.
JOB_ENV = {"windows-asan": {"CTEST_PARALLEL_LEVEL": "2"}}

# The shell CI's Linux steps run in, before its bash: address-space
# randomisation off for the step's own processes (setarch -R), the
# system's setting left as it is.
LINUX_SHELL = ("setarch", "x86_64", "-R")

CLEAN = "clean-check"  # CI's clean-binaries check, its script read from ci.yml
PIN = "-DQIVEN3_PIN_TOOLS=ON"

# The pinned compilers of the Linux jobs, by the compiler CI's preset names.
LINUX_COMPILERS = {
    "gcc": ("-DCMAKE_C_COMPILER=gcc-13", "-DCMAKE_CXX_COMPILER=g++-13"),
    "clang": ("-DCMAKE_C_COMPILER=clang-18", "-DCMAKE_CXX_COMPILER=clang++-18"),
}


class Step:
    """One step: its name, its command as CI writes it, what the loop adds,
    its phase, the step or steps that must have passed before it runs,
    and its time limit."""

    __slots__ = ("name", "ci", "local", "phase", "after", "timeout")

    def __init__(self, name, ci, phase="test", local=(), after="build", timeout=None):
        self.name, self.ci, self.phase, self.local, self.after = name, tuple(ci), phase, tuple(local), after
        self.timeout = timeout  # seconds; None: the loop's step limit; 0: none, the step bounds itself


def structure_steps():
    """The structure tests on a build of ci-windows writing their records,
    windows-analysis's last steps, as ci.yml writes them."""
    return [Step("structure configure", ("cmake", "--preset", "ci-windows", "-B", "build/ci-structure",
                                         "-DQIVEN3_STRUCTURE=ON"), local=(PIN,), after=None),
            Step("structure build", ("cmake", "--build", "build/ci-structure"), after="structure configure"),
            Step("structure tests", ("ctest", "--test-dir", "build/ci-structure", "--output-on-failure",
                                     "--no-tests=error", "-R", "^tests[.]structure[.]", "-LE", "^analysis$"),
                 after="structure build")]


def windows_job(preset, clean=False, analysis=False):
    steps = [Step("configure", ("cmake", "--preset", preset), "build", (PIN,), None),
             Step("build", ("cmake", "--build", "--preset", preset), "build", after="configure"),
             Step("fake", ("ctest", "--preset", "fake"))]
    if analysis:
        steps.append(Step("analysis controls", ("ctest", "--preset", "ci-analysis", "--no-tests=error")))
    if clean:
        steps.append(Step("clean binaries", (CLEAN, "build/ci")))
    if analysis:
        steps += structure_steps()
    return ("windows", preset, steps)


def linux_job(preset, compiler, fuzz=False, clean=False, cxx=False):
    steps = [Step("configure", ("cmake", "--preset", preset), "build", (PIN, *LINUX_COMPILERS[compiler]), None),
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
    steps = [Step("configure", ("cmake", "--preset", "release"), "build", (PIN,), None),
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

# The tiers' own. Tier 1's job a system, and the step it adds on it: fast's
# label on the job's own build, and no structure build. FAST stays the fast
# test preset's selection, as CI's; the tier's filters are the loop's.
TIER1 = {"windows": "windows-msvc", "linux": "linux-gcc"}
FAST = Step("fast", ("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=error", "-L", "^fast$"))
# Tier 1's own filter: its fake step, on the same build, has run every test
# of both labels, so that fast runs only those fake does not, on both systems.
FAST_ONLY = Step("fast", (*FAST.ci, "-LE", "^fake$"))
# On Windows fast leaves out the structure tests: some read the records a
# structure build writes, which tier 1 does not build, so that they would
# read another tree's, stale after a change. They run in windows-analysis's
# own steps and in tier 3. On Linux the tests that read the records skip.
FAST_REST = Step("fast", (*FAST_ONLY.ci, "-E", "^tests[.]structure[.]"))
TIER1_STEPS = {"windows-msvc": (FAST_REST,), "linux-gcc": (FAST_ONLY,)}
# Tier 2's step a job, after the job's own: on Windows the Windows drive's
# contract files on windows-msvc's build, their cases on the adapter over
# real files and processes, so that a unit's change to the adapter meets them
# before tier 3. The rest of the label machine, the adapter's C++ cases
# that start processes or read the machine's facts, stays tier 3's.
DRIVE_WINDOWS = Step("drive", ("ctest", "--test-dir", "build/ci", "--output-on-failure", "--no-tests=error",
                               "-R", "^tests[.]machine[.]test_platform_"))
TIER2_STEPS = {"windows-msvc": (DRIVE_WINDOWS,)}
FUZZ_TIMED = "fuzz-timed"  # each fuzz target for a time: tools/loop_fuzz.py on the tree
# Tier 3's suites, once a system after its jobs: (system, steps). On
# Windows the full suite on release (windows-symbols' tree), with the
# structure preset's records and H6's nohal build beside it; and the fuzz
# targets in the fuzz preset's tree. On Linux the full suite on
# linux-plain's tree, built with no sanitizer as release is, and the fuzz
# targets on linux-fuzz's, each built by its job before (READS).
SUITES = {
    "windows-full": ("windows", [
        Step("structure configure", ("cmake", "--preset", "structure"), "build", (PIN,), None),
        Step("structure build", ("cmake", "--build", "--preset", "structure"), "build", after="structure configure"),
        Step("nohal configure", ("cmake", "--preset", "nohal"), "build", (PIN,), None),
        Step("nohal build", ("cmake", "--build", "--preset", "nohal"), "build", after="nohal configure"),
        Step("full", ("ctest", "--preset", "full"), after=("structure build", "nohal build"))]),
    "windows-fuzz": ("windows", [
        Step("configure", ("cmake", "--preset", "fuzz"), "build", (PIN,), None),
        Step("build", ("cmake", "--build", "--preset", "fuzz"), "build", after="configure"),
        Step("fuzz", ("ctest", "--preset", "fuzz"))]),
    "linux-full": ("linux", [Step("full", ("ctest", "--preset", "full"), after=None)]),
    "linux-fuzz-targets": ("linux", [Step("fuzz", ("ctest", "--preset", "fuzz"), after=None)]),
}
# A suite tier 3 runs only with --fuzz-time, by the job and step that run
# its tests without a time: on Linux linux-fuzz's own fuzz step, as CI has
# it, runs the fuzz label on the tree the suite reads, so that without a
# time the label runs once. Windows' fuzz targets run in no job of its.
TIMED_ONLY = {"linux-fuzz-targets": ("linux-fuzz", "fuzz")}
# Each job's or suite's own trees: CI's directory, as its commands name it
# or as its preset's binaryDir, mapped to the job's tree; a Linux job's
# build/ci to build/JOB in the one clone. A directory a step names outside
# its job's map is read as it is, and never owned (Windows' full suite
# reads windows-symbols' build/release).
TREES = {
    "windows-msvc": {"build/ci": "build/windows-msvc"},
    "windows-asan": {"build/ci": "build/windows-asan"},
    "windows-stack": {"build/ci": "build/windows-stack"},
    "windows-analysis": {"build/ci": "build/windows-analysis",
                         "build/ci-structure": "build/windows-analysis-structure"},
    "windows-symbols": {"build/release": "build/release"},
    "windows-full": {"build/structure": "build/structure", "build/nohal": "build/nohal"},
    "windows-fuzz": {"build/fuzz": "build/fuzz"},
    **{name: {"build/ci": "build/" + name} for name, job in JOBS.items() if job[0] == "linux"},
}
# A suite's directory mapped to a job's tree, read and never owned: the
# suite runs after the job built it, and never empties it.
READS = {
    "linux-full": {"build/release": "build/linux-plain"},
    "linux-fuzz-targets": {"build/fuzz": "build/linux-fuzz"},
}
