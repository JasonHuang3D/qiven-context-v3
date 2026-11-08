"""K10: every executable of the build is made by qiven_executable. Each
executable of CMake's records (executables.txt, each directory's executable
targets, which foundation/devkit/build/top.cmake reads from CMake after every
directory) is listed in the build's binaries.txt, which qiven_executable
writes; and no CMakeLists.txt or CMake file but executable.cmake's
qiven_executable calls add_executable. The control: a planted
add_executable, caught.
"""

import re
import tempfile
import unittest
from pathlib import Path

from foundation.devkit.python import git
from foundation.devkit.test.harness import binaries

ROOT = binaries.ROOT
CALL = re.compile(r"\badd_executable\s*\(")
# The one definition allowed: qiven_executable's own call.
OWN = ("foundation/system/diag/executable.cmake", "add_executable(${name} ")


def executables(build):
    """CMake's executables: name and file, from executables.txt."""
    text = (build / "executables.txt").read_text(encoding="utf-8")
    return {name: Path(file).resolve() for name, file in (line.split("\t") for line in text.splitlines() if line)}


def stray_calls(root, files):
    """Each add_executable call outside qiven_executable in files, relative to root: (file, line)."""
    found = []
    for rel in sorted(files):
        path = root / rel
        if path.name != "CMakeLists.txt" and path.suffix != ".cmake":
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("#", 1)[0]
            if CALL.search(code) and not (rel == OWN[0] and code.strip().startswith(OWN[1])):
                found.append((rel, number))
    return found


class Executables(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_K10_listed(self):
        build = binaries.directory("QIVEN_BUILD")
        made = executables(build)
        self.assertTrue(made, "the file API recorded no executable")
        self.assertEqual(sorted(n for n, f in made.items() if f not in binaries.listed()), [])

    def test_K10_one_call(self):
        files = [rel for rel in git.tracked(ROOT, others=True) if (ROOT / rel).is_file()]
        self.assertEqual(stray_calls(ROOT, files), [])

    def test_K10_controls(self):
        with tempfile.TemporaryDirectory() as d:
            planted = Path(d) / "tests" / "planted"
            planted.mkdir(parents=True)
            (planted / "CMakeLists.txt").write_text("add_executable(planted planted.cpp)\n", encoding="utf-8")
            (Path(d) / OWN[0]).parent.mkdir(parents=True)
            (Path(d) / OWN[0]).write_text("  add_executable(${name} x)\n", encoding="utf-8")
            self.assertEqual(stray_calls(Path(d), ["tests/planted/CMakeLists.txt", OWN[0]]),
                             [("tests/planted/CMakeLists.txt", 1)])


if __name__ == "__main__":
    unittest.main()
