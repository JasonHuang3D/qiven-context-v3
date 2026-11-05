"""The analysis configuration against its list (exemptions.py): the real
tree's agree; and every preset records its name (Presets). The build's
test-code targets against the list's are the analysis build's, held there
(test_analysis.TestTargets)."""

import json
import unittest

from foundation.devkit.structure import exemptions, rules

ROOT = rules.ROOT


def real(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


class Exemptions(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_real_tree(self):
        self.assertEqual(exemptions.findings(ROOT, rules.source_files(ROOT)), [])


class Presets(unittest.TestCase):
    """The preset a tree records (preset.txt), which the pin of test code's
    targets reads (test_analysis.TestTargets, on the analysis build)."""
    ctest_labels = ("fake",)

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
