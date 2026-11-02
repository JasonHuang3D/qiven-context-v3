"""The analysis configuration against its list (exemptions.py): the real
tree's agree; planted configurations in a scratch tree, each refused with its
file: a check off that the list does not name, an entry nothing turns off, a
rule set left out, a family left out, an entry or a rule set with no reason;
and the build's test-code targets against the list's (TestTargets)."""

import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path

from foundation.devkit.structure import exemptions, rules

ROOT = rules.ROOT
LIST = exemptions.LIST
SET = "foundation/devkit/build/analysis.ruleset"
TEST_SET = "foundation/devkit/build/analysis-test.ruleset"
TEST_TIDY = "foundation/devkit/build/analysis-test.clang-tidy"
NEW_TIDY = "src/new/.clang-tidy"
INHERITS = "InheritParentConfig: true\nChecks: '{}'\n"
LIFETIME = '  <Include Path="CppCoreCheckLifetimeRules.ruleset" Action="Default" />\n'


def write(root, rel, text):
    (root / rel).parent.mkdir(parents=True, exist_ok=True)
    (root / rel).write_text(text, encoding="utf-8", newline="\n")


def real(rel):
    return (ROOT / rel).read_text(encoding="utf-8-sig")


class Exemptions(unittest.TestCase):
    ctest_labels = ("fake",)

    def tree(self):
        """A scratch copy of the tree's configuration and its list."""
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        root = Path(scratch.name)
        for rel in rules.source_files(ROOT):
            if rel == LIST or rel.endswith((".clang-tidy", ".ruleset")):
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / rel, root / rel)
        return root

    def found(self, root):
        return exemptions.findings(root, rules.source_files(root))

    def edit_list(self, root, edit):
        data = json.loads((root / LIST).read_text(encoding="utf-8"))
        edit(data)
        write(root, LIST, json.dumps(data))

    def refused(self, root, where, words):
        found = self.found(root)
        self.assertEqual([rel for rel, _ in found], [where], found)
        self.assertIn(words, found[0][1])

    def test_real_tree(self):
        self.assertEqual(self.found(ROOT), [])

    def test_scratch_copy_passes(self):
        self.assertEqual(self.found(self.tree()), [])

    def test_planted(self):
        rule = '<Rule Id="C26400" Action="{}" /></Rules>'
        for rel, text, words in (
                (NEW_TIDY, INHERITS.format("-bugprone-use-after-move"), "bugprone-use-after-move"),
                (NEW_TIDY, INHERITS.format("-bugprone-*"), "bugprone-*"),
                (NEW_TIDY, INHERITS.format("-*"), "'*'"),
                (NEW_TIDY, "Checks: 'bugprone-use-after-move'\n", "defaults"),
                (".clang-tidy", real(".clang-tidy").replace("  bugprone-*,\n", ""), "bugprone-* left out"),
                (SET, real(SET).replace("  </Rules>", rule.format("None")), "C26400"),
                (SET, real(SET).replace("  </Rules>", rule.format("Hidden")), "C26400"),
                (SET, real(SET).replace(LIFETIME, ""), "CppCoreCheckLifetimeRules.ruleset left out"),
                ("src/new.ruleset", '<RuleSet Name="x" />\n', "does not name")):
            with self.subTest(rel=rel, words=words):
                root = self.tree()
                write(root, rel, text)
                self.refused(root, rel, words)

    def test_entry_nothing_turns_off(self):
        root = self.tree()
        self.edit_list(root, lambda data: data["exemptions"].append(
            {"tool": "msvc", "check": "C26400", "scope": "every file", "reason": "planted"}))
        self.refused(root, LIST, "nothing turns off")

    def test_no_reason(self):
        root = self.tree()
        self.edit_list(root, lambda data: data["exemptions"][0].update(reason=" "))
        self.refused(root, LIST, "no reason")

    def test_test_code_scope(self):
        """A check off in the test-code files is named at that scope, for
        both tools; an entry at another scope does not stand for it."""
        root = self.tree()
        write(root, TEST_TIDY, INHERITS.format("-bugprone-throwing-static-initialization"))
        write(root, TEST_SET, real(TEST_SET).replace("</RuleSet>", '<Rules AnalyzerId="a" RuleNamespace="b">'
                                                     '<Rule Id="C26426" Action="None" /></Rules></RuleSet>'))
        checks = (("msvc", "C26426"), ("clang-tidy", "bugprone-throwing-static-initialization"))
        self.edit_list(root, lambda data: data["exemptions"].extend(
            {"tool": tool, "check": check, "scope": "every file", "reason": "planted"} for tool, check in checks))
        self.assertEqual({rel for rel, _ in self.found(root)}, {LIST, TEST_SET, TEST_TIDY})
        self.edit_list(root, lambda data: [entry.update(scope="test code") for entry in data["exemptions"][-2:]])
        self.assertEqual(self.found(root), [])

    def test_rule_set_no_reason(self):
        root = self.tree()
        self.edit_list(root, lambda data: data["rule_sets"][SET].update(reason=""))
        self.refused(root, LIST, "no reason")


class TestTargets(unittest.TestCase):
    """The targets the build gives test code's analysis, by its own reader
    (test-targets.txt in QIVEN3_BUILD's tree), are the list's test_code
    targets: a target left out of the list, a library's given the scope, or
    a pinned one the build defines without it, is refused; a pinned one a
    preset does not define (defined-targets.txt) is skipped by its name only
    where the list's not_defined names that target and the preset the tree
    records (preset.txt), whatever its directory is called."""
    ctest_labels = ("fake",)

    def test_build(self):
        build = os.environ.get("QIVEN3_BUILD")
        if not build:
            self.skipTest("no QIVEN3_BUILD: run by ctest")
        found, absent = exemptions.targets(ROOT, Path(build))
        self.assertEqual(found, [])
        for name in absent:
            with self.subTest(target=name):
                self.skipTest(f"{name}: pinned, and {build} does not define it")

    def given(self, scratch, scoped, defined, preset="analysis", root=ROOT, directory=None):
        """The pin over a tree whose configure recorded preset, None for
        none, in the directory named directory, by default a neutral name."""
        build = Path(scratch, directory or "tree")
        build.mkdir(exist_ok=True)
        (build / "test-targets.txt").write_text("\n".join(scoped) + "\n", encoding="utf-8")
        (build / "defined-targets.txt").write_text("\n".join(defined) + "\n", encoding="utf-8")
        (build / "preset.txt").unlink(missing_ok=True)
        if preset is not None:
            (build / "preset.txt").write_text(preset + "\n", encoding="utf-8")
        return exemptions.targets(root, build)

    def test_every_preset_records_its_name(self):
        """The base preset alone states QIVEN3_PRESET, as the preset's own
        name, and every preset one can configure inherits from it."""
        presets = {item["name"]: item for item in json.loads(real("CMakePresets.json"))["configurePresets"]}

        def reaches_base(name):
            parents = presets[name].get("inherits", [])
            parents = [parents] if isinstance(parents, str) else parents
            return name == "base" or any(reaches_base(parent) for parent in parents)

        stating = {name: item["cacheVariables"]["QIVEN3_PRESET"] for name, item in presets.items()
                   if "QIVEN3_PRESET" in item.get("cacheVariables", {})}
        self.assertEqual(stating, {"base": "${presetName}"})
        self.assertEqual([name for name in presets if not reaches_base(name)], [])

    def test_planted(self):
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        with tempfile.TemporaryDirectory() as scratch:
            found, absent = self.given(scratch, [*pinned, "qiven3_memory"], [*pinned, "qiven3_memory"])
            self.assertEqual((len(found), absent), (1, []), found)
            self.assertIn("qiven3_memory: takes test code's analysis", found[0][1])

    def test_planted_defined(self):
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        with tempfile.TemporaryDirectory() as scratch:
            found, absent = self.given(scratch, pinned[1:], [*pinned, "qiven3_memory"])
            self.assertEqual((len(found), absent), (1, []), found)
            self.assertIn(f"{pinned[0]}: pinned, and {Path(scratch, 'tree')} defines it without", found[0][1])

    def test_skipped_by_name(self):
        """nohal's qiven3_hal_fake, the list's entry, is skipped, with the
        header units' target no such preset defines; a pinned target another
        preset or nohal itself leaves undefined is refused, naming it and
        the preset, and so is a listed one nohal defines."""
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        units = "qiven3_test_headers"
        rest = [name for name in pinned if name not in ("qiven3_hal_fake", units)]
        with tempfile.TemporaryDirectory() as scratch:
            self.assertEqual(self.given(scratch, rest, rest, "nohal"), ([], ["qiven3_hal_fake", units]))
            for preset, kept, absent, words in (("release", rest, [units], "qiven3_hal_fake: pinned"),
                                                ("nohal", rest[1:], ["qiven3_hal_fake", units], f"{rest[0]}: pinned"),
                                                ("nohal", [*rest, "qiven3_hal_fake"], [units],
                                                 "qiven3_hal_fake: listed as not defined")):
                with self.subTest(preset=preset, words=words):
                    found, skipped = self.given(scratch, kept, kept, preset)
                    self.assertEqual(([rel for rel, _ in found], skipped), (["defined-targets.txt"], absent), found)
                    self.assertIn(words, found[0][1])
                    self.assertIn(preset, found[0][1])

    def test_directory_name_is_not_the_preset(self):
        """A release tree in a directory named nohal is refused, and says
        release; a nohal tree in another directory skips; a tree of CI's
        shared directory reads as its own preset."""
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        units = "qiven3_test_headers"
        rest = [name for name in pinned if name not in ("qiven3_hal_fake", units)]
        with tempfile.TemporaryDirectory() as scratch:
            self.assertEqual(self.given(scratch, rest, rest, "nohal", directory="windows-msvc"),
                             ([], ["qiven3_hal_fake", units]))
            for preset, directory in (("release", "nohal"), ("ci-windows", "ci")):
                with self.subTest(preset=preset, directory=directory):
                    found, skipped = self.given(scratch, rest, rest, preset, directory=directory)
                    self.assertEqual(([rel for rel, _ in found], skipped), (["defined-targets.txt"], [units]), found)
                    self.assertIn(f"(preset {preset})", found[0][1])

    def test_no_record(self):
        """A tree with no preset.txt, or an empty one, skips nothing: each
        pinned target it does not define is refused, naming the tree, even
        in a directory named nohal; defining them all, it passes."""
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        rest = [name for name in pinned if name != "qiven3_hal_fake"]
        with tempfile.TemporaryDirectory() as scratch:
            self.assertEqual(self.given(scratch, pinned, pinned, None), ([], []))
            for preset in (None, ""):
                with self.subTest(preset=preset):
                    found, skipped = self.given(scratch, rest, rest, preset, directory="nohal")
                    self.assertEqual(([rel for rel, _ in found], skipped), (["defined-targets.txt"], []), found)
                    self.assertIn(f"{Path(scratch, 'nohal')} (no preset recorded", found[0][1])

    def test_stale_entry(self):
        """An entry naming a target the list does not pin, a preset
        CMakePresets.json does not configure, one hidden, or no reason, is
        refused."""
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        for change in ({"target": "qiven3_memory"}, {"preset": "nohall"}, {"preset": "ci"}, {"preset": "base"},
                       {"reason": " "}):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as scratch:
                data = json.loads(real(LIST))
                data["test_code"]["not_defined"][0].update(change)
                write(Path(scratch), LIST, json.dumps(data))
                shutil.copyfile(ROOT / "CMakePresets.json", Path(scratch, "CMakePresets.json"))
                found, _ = self.given(scratch, pinned, pinned, root=Path(scratch))
                self.assertEqual([rel for rel, _ in found], [LIST], found)
                self.assertIn(next(iter(change.values())), found[0][1])
