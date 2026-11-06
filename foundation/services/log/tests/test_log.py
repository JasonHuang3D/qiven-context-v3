"""K13: levels below the build's minimum compile to nothing, and an event's
fields hold no run-time text.

- The binary half: qiven_log_test holds an event at trace and its control
  at info (log_k13.cpp). The control's name is in the binary's bytes in
  every build; the trace event's is there when the build's minimum,
  the cache's QIVEN_LOG_LEVEL, is trace, and absent otherwise.
- The static half: the build makes qiven_log_plant_control, whose event
  takes a Name; the plant, qiven_log_plant, the same file with its field a
  std::string_view, fails to compile at the line marked, and the control,
  built by the same command, succeeds.
"""

import re
import unittest

from foundation.devkit.python import build_tree
from foundation.devkit.python import process
from foundation.devkit.test.harness import binaries

TRACE = b"k13.trace-marker"
CONTROL = b"k13.info-control"
PLANT = binaries.ROOT / "foundation" / "services" / "log" / "tests" / "log_plant.cpp"
# A build of one target ends within this many seconds.
BUILD_TIMEOUT = 300


def level():
    """The build's minimum level, as its cache holds it."""
    found = binaries.build_option("QIVEN_LOG_LEVEL")
    if found is None:
        raise RuntimeError("no QIVEN_LOG_LEVEL in the build's cache (QIVEN_BUILD)")
    return found


def build(target):
    """cmake --build of the build's directory for one target: its status and
    its output."""
    cmake = build_tree.cmake()
    if cmake is None:
        raise RuntimeError("no cmake beside QIVEN_CTEST or on PATH")
    done = process.run([cmake, "--build", binaries.directory("QIVEN_BUILD"), "--target", target], limit=BUILD_TIMEOUT)
    return done.status, (done.out + done.err).decode("utf-8", "replace")


def marked(text, mark):
    """The line of PLANT holding mark, from 1."""
    lines = [n for n, line in enumerate(text.splitlines(), 1) if mark in line]
    if len(lines) != 1:
        raise RuntimeError(f"{PLANT.name} holds '{mark}' {len(lines)} times")
    return lines[0]


class Levels(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_trace_name_by_minimum(self):
        data = binaries.path("qiven_log_test").read_bytes()
        self.assertIn(CONTROL, data, "the control's name is not in the binary")
        if level() == "trace":
            self.assertIn(TRACE, data, "the minimum is trace, and the trace event's name is not in the binary")
        else:
            self.assertNotIn(TRACE, data, f"the minimum is {level()}, and the trace event's name is in the binary")


class Field(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_planted_field_does_not_compile(self):
        status, output = build("qiven_log_plant_control")
        self.assertEqual(status, 0, output[-2000:])
        status, output = build("qiven_log_plant")
        self.assertNotEqual(status, 0, "the planted std::string_view field compiled")
        line = marked(PLANT.read_text(encoding="utf-8"), "// K13: the plant")
        # MSVC writes file(line); gcc and clang file:line:column.
        where = re.compile(r"log_plant\.cpp(\(" + str(line) + r"\)|:" + str(line) + r":)")
        self.assertRegex(output, where, output[-2000:])
        # The build never made the plant's object.
        self.assertFalse(any(p.name.startswith("log_plant") for p in self.objects()), self.objects())

    @staticmethod
    def objects():
        build_dir = binaries.directory("QIVEN_BUILD")
        return [p for p in build_dir.rglob("qiven_log_plant.dir/*") if p.is_file() and
                p.suffix in (".obj", ".o")]


if __name__ == "__main__":
    unittest.main()
