"""H6: the core without the HAL. The nohal preset's build, QIVEN_HAL off,
holds no HAL target and no HAL operation, and lists every test the build
with the HAL lists but compute's own cases (compute.*), which it does not
build; its whole suite passing is the case's run.

What the build builds, its targets and the sources it compiles, is its
build.ninja's statements but the configure's own: its rerun, whose inputs
are the files the configure reads whatever QIVEN_HAL, and the phony
statement naming those files.

Run in that build, the case reads its own records. Run in a build with the
HAL, it reads the nohal build beside it, build/nohal of the checkout's
presets; when that build is absent, the case is skipped with the reason,
never passed.
"""

import re
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.session import FAKE, has_hal

HAL_TARGETS = re.compile(rb"qiven_compute|\bhal\.cpp|fake_hal\.cpp|hal_ops\.cpp")
LISTED = re.compile(r"^add_test\(\[==\[(.+?)\]==\]", re.MULTILINE)
STATEMENT = re.compile(rb"build (.+?)(?<!\$): (\S+)(.*)")  # outputs, rule, inputs
PATHS = re.compile(rb"(?<!\$) +")  # ninja escapes a path's own space as "$ "


def statements(build):
    """build.ninja's build statements, each joined across its continued
    lines, as (outputs, rule, inputs, the statement)."""
    found, line = [], b""
    for part in (build / "build.ninja").read_bytes().splitlines():
        line += part.lstrip(b" ") if line else part
        if (len(line) - len(line.rstrip(b"$"))) % 2:  # an odd "$" ends a continued line
            line = line[:-1]
            continue
        match = STATEMENT.fullmatch(line)
        if match:
            outputs, rule, inputs = match.groups()
            found.append((set(PATHS.split(outputs)), rule, set(PATHS.split(inputs.strip())), line))
        line = b""
    return found


def built(build):
    """The statements of what the build builds, the configure's own left out."""
    found = statements(build)
    read = set().union(*(inputs for _, rule, inputs, _ in found if rule == b"RERUN_CMAKE"))
    return [line for outputs, rule, _, line in found
            if rule != b"RERUN_CMAKE" and not (rule == b"phony" and outputs <= read)]


def listed(build):
    """The tests a build's listing step registered with CTest."""
    listing = Path(build) / "tests" / "ctest_tests.cmake"
    if not listing.is_file():
        raise AssertionError(f"the build lists no tests yet: {listing}")
    return set(LISTED.findall(listing.read_text(encoding="utf-8")))


class NoHal(unittest.TestCase):
    def test_H6_core_without_it(self):
        here = binaries.directory("QIVEN3_BUILD")
        build = here if not has_hal(here) else here.parent / "nohal"
        if not (build / "CMakeCache.txt").is_file():
            self.skipTest(f"the nohal build is absent: no {build.name} beside {here.name}; "
                          "configure and build the preset nohal, then run its suite")
        self.assertFalse(has_hal(build), f"{build} has QIVEN_HAL on")

        found = sorted(set(HAL_TARGETS.findall(b"\n".join(built(build)))))
        self.assertEqual(found, [], "the nohal build names a HAL target or source")
        drive = build / "bin" / binaries.path(FAKE).name
        self.assertTrue(drive.is_file(), drive)
        self.assertFalse(list((build / "bin").glob("*hal*")) + list((build / "bin").glob("*compute*")))

        ran = binaries.run_file(drive, ["--root", build, "--hal", "fake"])
        self.assertEqual(ran.status, 2)
        self.assertIn(b"no HAL in this build", ran.err)

        if build != here:
            own = {name for name in listed(here) if name.startswith("compute.")}
            self.assertEqual(listed(build), listed(here) - own, "the nohal build lists other tests")
