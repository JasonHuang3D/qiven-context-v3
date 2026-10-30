"""The modules' check (tools/loop_modules.py) on a synthetic table and tree:
a clean one passes, and each plant is caught, its line naming the file or
the edge: a file in no module, a file in two, an undeclared include, an
include resolving to no tracked file (counted, not refused, under
third_party/), an undeclared link, a foundation module depending on a
product one outside the table's exceptions. The real table is read and
checked for its own errors."""

import contextlib
import io
import sys
import unittest

from tools import loop_modules


def table():
    return {
        "exceptions": [{"from": "low", "to": ["high"], "until": "a split"}],
        "modules": [
            {"name": "shared", "layer": "support", "depends": ["core"], "paths": ["tests/runner/"]},
            {"name": "core", "layer": "foundation", "depends": [], "paths": ["src/core/"]},
            {"name": "low", "layer": "foundation", "depends": ["core", "high"], "paths": ["src/low/"]},
            {"name": "high", "layer": "product", "depends": ["low"], "paths": ["src/high/", "tests/unit/high_*"]},
        ],
    }


FILES = {
    "src/core/core.hpp": b"#pragma once\n",
    "src/low/low.cpp": b'#include "core/core.hpp"\n#include "high/high.hpp"\n',
    "src/high/high.hpp": b'#include "low/low.hpp"\n#include <vector>\n',
    "src/low/low.hpp": b'#include "core/core.hpp"\n',
    "tests/runner/runner.cpp": b'#include "core/core.hpp"\n#include "runner.hpp"\n',
    "tests/runner/runner.hpp": b"#pragma once\n",
    "tests/unit/high_test.cpp": b'#include "high/high.hpp"\n#include "runner/runner.hpp"\n',
}
TARGETS = [
    ("core_lib", ["src/core/core.hpp"], []),
    ("low_lib", ["src/low/low.cpp"], ["core_lib", "high_lib"]),
    ("high_lib", ["src/high/high.hpp"], ["low_lib", "core_lib"]),
    ("runner_lib", ["tests/runner/runner.cpp"], ["core_lib"]),
    ("high_cases", ["tests/unit/high_test.cpp"], ["high_lib", "low_lib", "core_lib", "runner_lib"]),
]


def errors(table_=None, files=None, targets=TARGETS):
    files = FILES if files is None else files
    return loop_modules.check(table_ or table(), sorted(files), files.__getitem__, targets)


class Check(unittest.TestCase):
    ctest_labels = ("fake",)

    def caught(self, found, expected):
        """The plant's line, logged, and no other."""
        print(f"plant caught: {found}", file=sys.stderr)
        self.assertEqual(found, [expected])

    def test_clean(self):
        self.assertEqual(errors(), [])

    def test_file_in_no_module(self):
        self.caught(errors(files={**FILES, "src/stray.cpp": b""}), "file src/stray.cpp: in no module")

    def test_file_in_two(self):
        t = table()
        t["modules"][1]["paths"].append("src/low/low.hpp")
        self.caught(errors(t), "file src/low/low.hpp: in modules core, low")

    def test_undeclared_include(self):
        files = {**FILES, "src/core/core.hpp": b'#include "low/low.hpp"\n'}
        self.caught(errors(files=files), "include src/core/core.hpp -> src/low/low.hpp: module core does not depend on low")

    def test_unresolved_include(self):
        files = {**FILES, "src/core/core.hpp": b'#pragma once\n#include "gone.hpp"\n'}
        self.caught(errors(files=files), 'include src/core/core.hpp:2 "gone.hpp": resolves to no tracked file')

    def test_vendored_include_counted(self):
        """Under third_party/ an unresolved include is counted, not refused."""
        t = table()
        t["modules"].append({"name": "vendor", "layer": "foundation", "depends": [], "paths": ["third_party/"]})
        files = {**FILES, "third_party/lib/lib.c": b'#include "lib.h"\n#include "config.h"\n', "third_party/lib/lib.h": b""}
        left = []
        self.assertEqual(loop_modules.check(t, sorted(files), files.__getitem__, TARGETS, left), [])
        self.assertEqual(left, ['third_party/lib/lib.c:2 "config.h"'])

    def test_parent_steps_resolved(self):
        """An include's .. steps taken from the including file's directory."""
        self.assertEqual(errors(files={**FILES, "src/low/low.cpp": b'#include "../core/core.hpp"\n'}), [])
        files = {**FILES, "src/core/core.hpp": b'#include "../low/low.hpp"\n'}
        self.caught(errors(files=files), "include src/core/core.hpp -> src/low/low.hpp: module core does not depend on low")

    def test_undeclared_link(self):
        targets = [*TARGETS[:4], ("core_cases", ["src/core/core.hpp"], ["low_lib"])]
        self.caught(errors(targets=targets), "link core_cases -> low_lib: module core does not depend on low")

    def test_support_reaches_its_own_dependencies_alone(self):
        targets = [*TARGETS[:3], ("runner_lib", ["tests/runner/runner.cpp"], ["core_lib", "low_lib"])]
        self.caught(errors(targets=targets), "link runner_lib -> low_lib: module shared does not depend on low")

    def test_foundation_on_product_outside_the_exception(self):
        t = table()
        t["modules"][1]["depends"].append("high")
        self.caught(errors(t), "edge core -> high: foundation depending on product")

    def test_the_exception_listed(self):
        t = table()
        t["exceptions"] = []
        self.caught(errors(t), "edge low -> high: foundation depending on product")

    def test_help_and_usage(self):
        """--help and -h print the usage and exit 0; a wrong count fails, 2."""
        for flag in ("--help", "-h"):
            with self.subTest(flag=flag), contextlib.redirect_stdout(io.StringIO()) as out:
                self.assertEqual(loop_modules.main([flag]), 0)
            self.assertIn("loop_modules.py SOURCE [BUILD]", out.getvalue())
        for argv in ([], ["a", "b", "c"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(loop_modules.main(argv), 2)
            self.assertIn("usage:", err.getvalue())

    def test_the_real_table(self):
        """The checkout's table: its layers, names and foundation line."""
        self.assertEqual(loop_modules.check_table(loop_modules.load()), [])


if __name__ == "__main__":
    unittest.main()
