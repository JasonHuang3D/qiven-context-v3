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

from tests.structure import exemptions, rules

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
    preset does not define (defined-targets.txt) is skipped by its name."""
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

    def given(self, build, scoped, defined):
        Path(build, "test-targets.txt").write_text("\n".join(scoped) + "\n", encoding="utf-8")
        Path(build, "defined-targets.txt").write_text("\n".join(defined) + "\n", encoding="utf-8")
        return exemptions.targets(ROOT, Path(build))

    def test_planted(self):
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        with tempfile.TemporaryDirectory() as scratch:
            found, absent = self.given(scratch, [*pinned, "qiven3_memory"], [*pinned, "qiven3_memory"])
            self.assertEqual((len(found), absent), (1, []), found)
            self.assertIn("qiven3_memory: takes test code's analysis", found[0][1])
            self.assertEqual(self.given(scratch, pinned[1:], pinned[1:]), ([], [pinned[0]]))

    def test_planted_defined(self):
        pinned = json.loads(real(LIST))["test_code"]["targets"]
        with tempfile.TemporaryDirectory() as scratch:
            found, absent = self.given(scratch, pinned[1:], [*pinned, "qiven3_memory"])
            self.assertEqual((len(found), absent), (1, []), found)
            self.assertIn(f"{pinned[0]}: pinned, and {Path(scratch)} defines it without", found[0][1])
