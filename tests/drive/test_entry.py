"""K10: every binary of the build runs the diagnostic entry. Each executable
of CMake's records (executables.txt, each directory's executable targets,
which the root CMakeLists.txt reads from CMake after every directory) is
listed in the build's binaries.txt, which qiven_executable writes; no CMakeLists.txt or CMake file but build.cmake's
qiven_executable calls add_executable; and each listed binary, started
with QIVEN3_DIAG_PROBE=1, prints "diag: entry" from its entry, before main,
and exits 0. The controls: a planted add_executable, and a binary without
the entry, this interpreter, each caught.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive import binaries

ROOT = binaries.ROOT
PROBE = b"diag: entry\n"
CALL = re.compile(r"\badd_executable\s*\(")
# The one definition allowed: qiven_executable's own call.
OWN = ("foundation/devkit/build/build.cmake", "add_executable(${name} ")


def listed(build):
    """binaries.txt's files, as resolved paths."""
    text = (build / "binaries.txt").read_text(encoding="utf-8")
    return {Path(line).resolve() for line in text.splitlines() if line}


def executables(build):
    """CMake's executables: name and file, from executables.txt."""
    text = (build / "executables.txt").read_text(encoding="utf-8")
    return {name: Path(file).resolve() for name, file in (line.split("\t") for line in text.splitlines() if line)}


def stray_calls(root):
    """Each add_executable call outside qiven_executable: (file, line)."""
    found = []
    for path in sorted(root.rglob("*")):
        rel = path.relative_to(root).as_posix()
        if rel.startswith(("build/", ".git/")) or not path.is_file():
            continue
        if path.name != "CMakeLists.txt" and path.suffix != ".cmake":
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("#", 1)[0]
            if CALL.search(code) and not (rel == OWN[0] and code.strip().startswith(OWN[1])):
                found.append((rel, number))
    return found


def probe_problem(command):
    """None when the binary's entry answers its probe, else what it gave."""
    env = dict(os.environ, QIVEN3_DIAG_PROBE="1")
    # The test tree's library, which qiven3-fault-import imports, by the
    # loader's path on Linux: the binary holds no run path (K11).
    env["LD_LIBRARY_PATH"] = str(Path(command[0]).parent)
    done = subprocess.run(command, capture_output=True, env=env, stdin=subprocess.DEVNULL,
                          timeout=binaries.TIMEOUT, check=False)
    if done.returncode != 0 or done.stdout != PROBE:
        return f"{command[0]}: {binaries.Ran(done.returncode, b'', b'').named}, out {done.stdout[:80]!r}"
    return None


class Entry(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_K10_listed(self):
        build = binaries.directory("QIVEN3_BUILD")
        made = executables(build)
        self.assertTrue(made, "the file API recorded no executable")
        self.assertEqual(sorted(n for n, f in made.items() if f not in listed(build)), [])

    def test_K10_one_call(self):
        self.assertEqual(stray_calls(ROOT), [])

    def test_K10_probe(self):
        build = binaries.directory("QIVEN3_BUILD")
        problems = [probe_problem([str(f)]) for f in sorted(listed(build))]
        self.assertEqual([p for p in problems if p], [])

    def test_K10_controls(self):
        with tempfile.TemporaryDirectory() as d:
            planted = Path(d) / "tests" / "planted"
            planted.mkdir(parents=True)
            (planted / "CMakeLists.txt").write_text("add_executable(planted planted.cpp)\n", encoding="utf-8")
            (Path(d) / OWN[0]).parent.mkdir(parents=True)
            (Path(d) / OWN[0]).write_text("  add_executable(${name} x)\n", encoding="utf-8")
            self.assertEqual(stray_calls(Path(d)), [("tests/planted/CMakeLists.txt", 1)])
        self.assertIsNotNone(probe_problem([sys.executable, "-I", "-c", "pass"]))


if __name__ == "__main__":
    unittest.main()
