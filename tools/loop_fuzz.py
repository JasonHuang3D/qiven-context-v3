"""Each fuzz target of a build for a time, from its corpus: the loop's tier 3
with a fuzzing time (tools/loop.py --fuzz-time).

    loop_fuzz.py <tree> <seconds>

The targets are the build's own list, <tree>/tests/fuzz_targets.txt (a
line each: the target, its binary, its corpus, its artifacts; written by
tests/fuzz/CMakeLists.txt). Each runs as

    <binary> -max_total_time=<seconds> -timeout=10 -artifact_prefix=<artifacts>/ <corpus>

libFuzzer stopping at the time or at its first finding, whose input it
writes under the artifacts. A line a target with its outcome and seconds,
by the loop's clock, tools/loop_run.py's now(): in WSL2 the raw clock,
which the guest's time daemon never slews.
Exit 0 when every target ran its time with no finding, 1 when one found or
failed, 2 when it could not run (no list, no target, a bad argument).
"""

import runpy
import subprocess
import sys
from pathlib import Path

MARGIN = 120  # seconds past the fuzzing time before a target counts as hung
INPUT_TIMEOUT = 10  # libFuzzer's -timeout: seconds an input
# The loop's clock, by runpy: under -I this directory is off sys.path.
now = runpy.run_path(str(Path(__file__).resolve().with_name("loop_run.py")))["now"]


def targets(tree):
    """[(target, binary, corpus, artifacts)] of the build's list."""
    listing = tree / "tests" / "fuzz_targets.txt"
    if not listing.is_file():
        raise ValueError(f"no fuzz targets listed: {listing} is missing; build the fuzz preset first")
    out = []
    for line in listing.read_text(encoding="utf-8").splitlines():
        if line.strip():
            fields = line.split("\t")
            if len(fields) != 4:
                raise ValueError(f"a malformed line of {listing}: {line!r}")
            out.append(tuple(fields))
    if not out:
        raise ValueError(f"{listing} lists no target")
    return out


def main(argv):
    if len(argv) != 2 or not argv[1].isdigit() or int(argv[1]) <= 0:
        print("loop_fuzz: give <tree> <seconds>, seconds a positive integer")
        return 2
    tree, seconds = Path(argv[0]), int(argv[1])
    try:
        listed = targets(tree)
    except (OSError, ValueError) as e:
        print(f"loop_fuzz: {e}")
        return 2
    failed = 0
    for name, binary, corpus, artifacts in listed:
        start = now()
        argv = [binary, f"-max_total_time={seconds}", f"-timeout={INPUT_TIMEOUT}", f"-artifact_prefix={artifacts}/",
                corpus]
        print("$ " + " ".join(argv), flush=True)
        try:
            status = subprocess.run(argv, stdin=subprocess.DEVNULL, timeout=seconds + MARGIN).returncode
            what = "pass" if status == 0 else f"FAIL, exit {status}"
        except subprocess.TimeoutExpired:
            status, what = 1, f"FAIL, hung past {seconds + MARGIN} s"
        except OSError as e:
            status, what = 1, f"FAIL, cannot start: {e}"
        failed += status != 0
        print(f"fuzz {name}: {what} in {now() - start:.1f} s", flush=True)
    print(f"loop_fuzz: {len(listed) - failed} of {len(listed)} targets ran {seconds} s with no finding")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
