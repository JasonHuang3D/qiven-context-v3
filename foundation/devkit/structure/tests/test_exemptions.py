"""The analysis configuration against its list (exemptions.py): the real
tree's agree; and the build's test-code targets against the list's
(TestTargets)."""

import json
import os
import unittest
from pathlib import Path

from foundation.devkit.structure import exemptions, rules

ROOT = rules.ROOT


def real(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


class Exemptions(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_real_tree(self):
        self.assertEqual(exemptions.findings(ROOT, rules.source_files(ROOT)), [])


class TestTargets(unittest.TestCase):
    """The targets the build gives test code's analysis, by its own reader
    (test-targets.txt in QIVEN_BUILD's tree), are the list's test_code
    targets; a pinned one a preset does not define (defined-targets.txt) is
    skipped by its name only where the list's not_defined names that target
    and the preset the tree records (preset.txt)."""
    ctest_labels = ("fake",)

    def test_build(self):
        build = os.environ.get("QIVEN_BUILD")
        if not build:
            self.skipTest("no QIVEN_BUILD: run by ctest")
        found, absent = exemptions.targets(ROOT, Path(build))
        self.assertEqual(found, [])
        for name in absent:
            with self.subTest(target=name):
                self.skipTest(f"{name}: pinned, and {build} does not define it")

    def test_every_preset_records_its_name(self):
        """The base preset alone states QIVEN_PRESET, as the preset's own
        name, and every preset one can configure inherits from it."""
        presets = {item["name"]: item for item in json.loads(real("CMakePresets.json"))["configurePresets"]}

        def reaches_base(name):
            parents = presets[name].get("inherits", [])
            parents = [parents] if isinstance(parents, str) else parents
            return name == "base" or any(reaches_base(parent) for parent in parents)

        stating = {name: item["cacheVariables"]["QIVEN_PRESET"] for name, item in presets.items()
                   if "QIVEN_PRESET" in item.get("cacheVariables", {})}
        self.assertEqual(stating, {"base": "${presetName}"})
        self.assertEqual([name for name in presets if not reaches_base(name)], [])
