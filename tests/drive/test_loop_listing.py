"""The listing a move runs before and after (tools/loop_listing.py): its
reading of a clang-tidy step and of each compile's rule sets, the
configuration a step resolves to, a header by the unit it compiles through,
and two listings compared with and without a rename map, a map refused and
a merge named, what a move could switch (rule sets' content, external
directories, the exemptions by the earlier listing's data), and what refuses
a listing; over fixture inputs in a scratch directory, no build needed.
"""

import contextlib
import io
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from foundation.devkit.structure import layers
from tools import loop_listing, loop_listing_compare


def ninja_path(path):
    return path.as_posix().replace(":", "$:")


class Fixture(unittest.TestCase):
    """A checkout's tree and a build tree beside it, with the configurations
    a step reads."""

    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        self.root = Path(scratch.name).resolve() / "tree"
        self.build = Path(scratch.name).resolve() / "build"
        for rel, text in ((".clang-tidy", "Checks: '*'\n"), ("src/diag/.clang-tidy", "Checks: '-x'\n"),
                          ("foundation/devkit/build/analysis-test.clang-tidy", "Checks: '-t'\n")):
            (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
            (self.root / rel).write_text(text, encoding="utf-8")
        self.composed = self.build / "tidy" / "config" / "test_tests.clang-tidy"
        self.composed.parent.mkdir(parents=True)
        self.composed.write_text("Checks: '*,-t'\n", encoding="utf-8")

    def digest(self, rel):
        return loop_listing.digest(self.root / rel)

    def steps(self):
        r, b = ninja_path(self.root), ninja_path(self.build)
        test = f"{r}/foundation/devkit/build/analysis-test.clang-tidy"
        text = "\n".join((
            f"build {b}/tidy/a.stamp: CUSTOM_COMMAND {r}/src/diag/x.cpp {b}/tidy/compile_commands.json"
            f" {r}/.clang-tidy {r}/src/diag/.clang-tidy || cmake_object_order_depends",
            f'  COMMAND = cmd.exe /C "cd /D {b} && clang-tidy.exe -p {b}/tidy --quiet {r}/src/diag/x.cpp"',
            "  DESC = clang-tidy src/diag/x.cpp",
            f"build {b}/tidy/b.stamp: CUSTOM_COMMAND {r}/tests/t.cpp {r}/.clang-tidy {test}"
            f" {b}/tidy/config/test_tests.clang-tidy",
            f'  COMMAND = cmd.exe /C "clang-tidy.exe --config-file={b}/tidy/config/test_tests.clang-tidy'
            f' --quiet {r}/tests/t.cpp"',
            "  DESC = clang-tidy tests/t.cpp",
            f"build {b}/tidy/c.stamp: CUSTOM_COMMAND {b}/gen/g.cpp",
            f'  COMMAND = cmd.exe /C "clang-tidy.exe --quiet {b}/gen/g.cpp"',
            "  DESC = clang-tidy ../build/gen/g.cpp",
            f"build {b}/x.obj: CXX_COMPILER__x {r}/src/diag/x.cpp",
            "  FLAGS = /W4"))
        return loop_listing.tidy_steps(text, self.root, self.build)

    def rulesets(self):
        ruleset = (self.root / "foundation/devkit/build/analysis.ruleset").as_posix()
        commands = [{"file": str(self.root / "src/diag/x.cpp"),
                     "command": f'cl.exe /analyze:rulesetdirectory"C:/x" /analyze:ruleset"{ruleset}" /c x.cpp'},
                    {"file": str(self.root / "src/diag/x.cpp"), "command": "cl.exe /c x.cpp"},
                    {"file": str(self.build / "gen/g.cpp"), "command": "cl.exe /c g.cpp"}]
        return loop_listing.rulesets(json.dumps(commands), self.root, self.build)


class Steps(Fixture):
    """A step's configuration: the nearest .clang-tidy for a step naming
    none, the composed file and what it rests on for a test source, none
    for a generated source naming none; the rule sets each compile names."""

    ctest_labels = ("fake",)

    def test_step_without_a_file_reads_the_nearest(self):
        self.assertEqual(self.steps()["src/diag/x.cpp"],
                         ("src/diag/.clang-tidy", [".clang-tidy", "src/diag/.clang-tidy"], None))

    def test_composed_step_names_its_file_and_what_it_rests_on(self):
        self.assertEqual(self.steps()["tests/t.cpp"],
                         ("composed", [".clang-tidy", "foundation/devkit/build/analysis-test.clang-tidy"],
                          "BUILD/tidy/config/test_tests.clang-tidy"))

    def test_generated_step_without_a_file_reads_none(self):
        self.assertEqual(self.steps()["BUILD/gen/g.cpp"], ("none", [], None))
        self.assertEqual(len(self.steps()), 3)

    def test_rule_sets_of_each_compile(self):
        self.assertEqual(self.rulesets(), {"src/diag/x.cpp": ["foundation/devkit/build/analysis.ruleset", "none"],
                                           "BUILD/gen/g.cpp": ["none"]})


class Lines(Fixture):
    """A file's line, a header's by its unit, the filters with the root as
    ROOT; a file neither walked, analysed nor compiled left out."""

    ctest_labels = ("fake",)

    def lines(self):
        scoped = {"src/diag/x.cpp": ["diag", "system", ["walker", "walked"]],
                  "src/diag/x.hpp": ["diag", "system", ["walked"]],
                  "tests/t.cpp": ["support", "tests", ["walked", "test_code"]],
                  "docs/a.md": [None, None, ["walker"]]}
        analysed = {"src/diag/x.cpp": "TRUE ", "src/diag/x.hpp": "TRUE ", "docs/a.md": "FALSE "}
        filters = ["filter=ROOT[/\\](src)[/\\].*", "exclude="]
        lines = loop_listing.lines_of(scoped, analysed, {"src/diag/x.hpp": "src/diag/x.cpp"}, self.rulesets(),
                                      self.steps(), filters, self.root, self.build)
        return {line.split()[0]: line for line in lines}

    def test_source_line(self):
        root, near = self.digest(".clang-tidy"), self.digest("src/diag/.clang-tidy")
        self.assertEqual(self.lines()["src/diag/x.cpp"],
                         "src/diag/x.cpp module=diag layer=system scopes=walker,walked,analysed"
                         f" ruleset=foundation/devkit/build/analysis.ruleset,none tidy=src/diag/.clang-tidy@{near}"
                         f" tidy_from=.clang-tidy@{root},src/diag/.clang-tidy@{near}")

    def test_header_by_its_unit(self):
        line = self.lines()["src/diag/x.hpp"]
        self.assertIn(" ruleset=- tidy=- tidy_from=- unit=src/diag/x.cpp unit_ruleset="
                      "foundation/devkit/build/analysis.ruleset,none unit_tidy=src/diag/.clang-tidy@", line)

    def test_composed_and_generated(self):
        lines = self.lines()
        composed = loop_listing.digest(self.composed)
        self.assertIn(f" ruleset=- tidy=composed@{composed} tidy_from=.clang-tidy@", lines["tests/t.cpp"])
        self.assertTrue(lines["tests/t.cpp"].endswith(" tidy_file=BUILD/tidy/config/test_tests.clang-tidy"))
        self.assertEqual(lines["BUILD/gen/g.cpp"],
                         "BUILD/gen/g.cpp module=- layer=- scopes=analysed ruleset=none tidy=none tidy_from=-")

    def test_unwalked_unanalysed_left_out_filters_kept(self):
        lines = self.lines()
        self.assertNotIn("docs/a.md", lines)
        self.assertEqual(lines["cmake:filter"], "cmake:filter value=ROOT[/\\](src)[/\\].*")
        self.assertEqual(lines["cmake:exclude"], "cmake:exclude value=")


BEFORE = ["src/qiven3/a.cpp module=base scopes=walked tidy=.clang-tidy@1 tidy_from=.clang-tidy@1",
          "src/qiven3/a.hpp module=base scopes=walked unit=src/qiven3/a.cpp unit_tidy=.clang-tidy@1",
          "tests/t.cpp module=support tidy=composed@3 tidy_file=BUILD/tidy/config/test_tests.clang-tidy"]
AFTER = ["src/diag/a.cpp module=base scopes=walked tidy=src/diag/.clang-tidy@2"
         " tidy_from=.clang-tidy@1,src/diag/.clang-tidy@2",
         "src/qiven3/a.hpp module=base scopes=walked unit=src/diag/a.cpp unit_tidy=src/diag/.clang-tidy@2",
         "tests/t.cpp module=support tidy=composed@3 tidy_file=BUILD/tidy/config/test_other.clang-tidy"]


class Compare(unittest.TestCase):
    """Two listings compared: only what differs, a map renaming paths and
    path values but never excusing a changed configuration; the build's
    file names not compared."""

    ctest_labels = ("fake",)

    def test_equal(self):
        self.assertEqual(loop_listing_compare.compare(BEFORE, BEFORE, {}), [])

    def test_without_a_map(self):
        self.assertEqual(loop_listing_compare.compare(BEFORE, AFTER, {}), [
            "+ src/diag/a.cpp (only after)", "- src/qiven3/a.cpp (only before)",
            "~ src/qiven3/a.hpp unit: src/qiven3/a.cpp -> src/diag/a.cpp",
            "~ src/qiven3/a.hpp unit_tidy: .clang-tidy@1 -> src/diag/.clang-tidy@2"])

    def test_under_a_map_the_configuration_still_differs(self):
        moves, merged = loop_listing_compare.read_map("# one move\nsrc/qiven3/a.cpp src/diag/a.cpp  # the source\n\n")
        self.assertEqual((moves, merged), ({"src/qiven3/a.cpp": "src/diag/a.cpp"}, set()))
        self.assertEqual(loop_listing_compare.compare(BEFORE, AFTER, moves), [
            "~ src/diag/a.cpp tidy: .clang-tidy@1 -> src/diag/.clang-tidy@2",
            "~ src/diag/a.cpp tidy_from: .clang-tidy@1 -> .clang-tidy@1,src/diag/.clang-tidy@2",
            "~ src/qiven3/a.hpp unit_tidy: .clang-tidy@1 -> src/diag/.clang-tidy@2"])

    def test_a_directory_moved_with_its_configuration(self):
        after = [line.replace("src/qiven3/", "src/new/") for line in BEFORE]
        moves, _ = loop_listing_compare.read_map("src/qiven3/ src/new/\n")
        self.assertEqual(loop_listing_compare.compare(BEFORE, after, moves), [])
        self.assertEqual(loop_listing_compare.renamed("src/qiven3x/a.cpp", moves), "src/qiven3x/a.cpp")

    def test_exit_code(self):
        with tempfile.TemporaryDirectory() as scratch:
            before, after, moves = (Path(scratch) / name for name in ("before.txt", "after.txt", "map.txt"))
            before.write_text("\n".join(BEFORE) + "\n", encoding="utf-8")
            after.write_text("\n".join(AFTER) + "\n", encoding="utf-8")
            moves.write_text("src/qiven3/a.cpp src/diag/a.cpp\n", encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()) as shown:
                codes = [loop_listing.main(["compare", str(before), str(before)]),
                         loop_listing.main(["compare", str(before), str(after), str(moves)]),
                         loop_listing.main(["compare"])]
        self.assertEqual(codes, [0, 1, 2])
        self.assertIn("~ src/diag/a.cpp tidy: .clang-tidy@1 -> src/diag/.clang-tidy@2", shown.getvalue())

    def test_rule_sets_per_compile_renamed(self):
        before = ["a/x.cpp ruleset=a/r.ruleset,a/r.ruleset|b/s.ruleset,none"]
        after = ["c/x.cpp ruleset=c/r.ruleset,c/r.ruleset|b/s.ruleset,none"]
        self.assertEqual(loop_listing_compare.compare(before, after, {"a/": "c/"}), [])
        after = ["c/x.cpp ruleset=c/r.ruleset,c/r.ruleset"]
        self.assertEqual(loop_listing_compare.compare(before, after, {"a/": "c/"}),
                         ["~ c/x.cpp ruleset: c/r.ruleset,c/r.ruleset|b/s.ruleset,none -> c/r.ruleset,c/r.ruleset"])


MERGING = ["a/CMakeLists.txt module=m", "a/one.cpp module=m", "b/CMakeLists.txt module=m", "b/two.cpp module=m",
           "c/gone.cpp module=m", "d/kept.cpp module=m tidy=t@1"]
MERGED = ["x/CMakeLists.txt module=m", "x/one.cpp module=m", "x/two.cpp module=m", "d/kept.cpp module=m tidy=t@1"]


class Refused(unittest.TestCase):
    """A map refused, each move named: an OLD the earlier listing lacks, two
    OLDs or two paths taking one NEW, a NEW staying in it; accepted where a
    merge line names NEW, each path merged compared."""

    ctest_labels = ("fake",)

    def test_merging_map_refused(self):
        moves, merged = loop_listing_compare.read_map("a/ x/\nb/ x/\nc/gone.cpp d/kept.cpp\ne/ y/\n")
        self.assertEqual(loop_listing_compare.compare(MERGING, MERGED, moves, merged), [
            "refused: c/gone.cpp moves to d/kept.cpp, where d/kept.cpp stays",
            "refused: e/ is no path of the earlier listing",
            "refused: a/ and b/ move to one path, x/",
            "refused: a/CMakeLists.txt and b/CMakeLists.txt become one path, x/CMakeLists.txt"])

    def test_a_staying_directory_and_two_paths_refused(self):
        self.assertEqual(loop_listing_compare.compare(MERGING, MERGING, {"b/": "d/"}),
                         ["refused: b/ moves to d/, where d/kept.cpp stays"])
        self.assertEqual(loop_listing_compare.compare(MERGING, MERGED, {"a/one.cpp": "x/two.cpp", "b/": "x/"}),
                         ["refused: a/one.cpp and b/two.cpp become one path, x/two.cpp"])

    def test_merge_named_accepted_and_compared(self):
        moves, merged = loop_listing_compare.read_map("merge x/ a/ b/\nmerge d/kept.cpp c/gone.cpp\n")
        self.assertEqual(merged, {"x/", "d/kept.cpp"})
        self.assertEqual(loop_listing_compare.compare(MERGING, MERGED, moves, merged), [
            "~ d/kept.cpp (from c/gone.cpp) tidy: - -> t@1"])
        with self.assertRaises(SystemExit):
            loop_listing_compare.read_map("a/ x/ y/\n")


B = "foundation/devkit/build/"  # the checkout's own exemptions name these, which main reads
DATA = {"tidy_on": ["bugprone-*"],
        "rule_sets": {B + "analysis.ruleset": {"includes": ["Native.ruleset"], "reason": "every source"},
                      B + "analysis-test.ruleset": {"includes": ["analysis.ruleset"], "reason": "test code"},
                      B + "stack.ruleset": {"includes": [], "reason": "the stack preset"}},
        "test_code": {"clang-tidy": B + "analysis-test.clang-tidy", "msvc": B + "analysis-test.ruleset",
                      "targets": ["q_fixtures", "q_seams"],
                      "not_defined": [{"preset": "nohal", "target": "q_fake", "reason": "no HAL"}]},
        "exemptions": [{"tool": "msvc", "check": "C1", "scope": "every file", "reason": "style, 100% #1"},
                       {"tool": "msvc", "check": "C2", "scope": "test code", "reason": "test code's"},
                       {"tool": "clang-tidy", "check": "bugprone-x", "scope": "src/diag/", "reason": "handlers"}]}
EXEMPT = loop_listing.exempt_lines(DATA)
TEST_LINE = (f"t/main.cpp module=log layer=services scopes=walked,test_code,analysed ruleset={B}analysis-test.ruleset"
             f" tidy=composed@1 tidy_from=.clang-tidy@2,{B}analysis-test.clang-tidy@3 tidy_file=BUILD/t.clang-tidy")
MOVED = ("c/src/main.cpp module=case_main layer=devkit scopes=walked,k15,analysed"
         f" ruleset={B}analysis.ruleset tidy=.clang-tidy@2 tidy_from=.clang-tidy@2")
MOVE = {"t/main.cpp": "c/src/main.cpp"}
NAMED = [("c/src/main.cpp", "layer", "services", "devkit"), ("c/src/main.cpp", "module", "log", "case_main"),
         ("c/src/main.cpp", "ruleset", B + "analysis-test.ruleset", B + "analysis.ruleset"),
         ("c/src/main.cpp", "scopes", "walked,test_code,analysed", "walked,k15,analysed"),
         ("c/src/main.cpp", "tidy", "composed@1", ".clang-tidy@2"),
         ("c/src/main.cpp", "tidy_from", f".clang-tidy@2,{B}analysis-test.clang-tidy@3", ".clang-tidy@2")]


class Expected(unittest.TestCase):
    """A move's differences named in a file of expected differences: each
    named one accepted where it tightens, refused where it loosens though
    named; a difference not named and a line naming none refused."""

    ctest_labels = ("fake",)

    def compare(self, after, expected, before=(TEST_LINE,)):
        return loop_listing_compare.compare([*before, *EXEMPT], [*after, *EXEMPT], MOVE, (), expected)

    def test_named_tightening_accepted(self):
        self.assertEqual(self.compare([MOVED], NAMED), [])

    def test_unnamed_difference_refused(self):
        self.assertEqual(self.compare([MOVED], NAMED[1:]), ["~ c/src/main.cpp layer: services -> devkit"])
        self.assertEqual(loop_listing_compare.compare([TEST_LINE], [MOVED], MOVE)[0],
                         "~ c/src/main.cpp layer: services -> devkit")

    def test_named_loosening_refused(self):
        after = "c/tests/main.cpp module=- layer=- scopes=walked ruleset=none tidy=- tidy_from=-"
        move = {"t/main.cpp": "c/tests/main.cpp"}
        found = loop_listing_compare.compare([TEST_LINE], [after], move)
        named = [tuple(line[2:].replace(":", "").replace(" ->", "").split()) for line in found]
        self.assertEqual(loop_listing_compare.compare([TEST_LINE, *EXEMPT], [after, *EXEMPT], move, (), named), [
            "refused: c/tests/main.cpp layer: leaves its layer", "refused: c/tests/main.cpp module: leaves its module",
            "refused: c/tests/main.cpp ruleset: leaves the analysis",
            "refused: c/tests/main.cpp scopes: leaves analysed",
            "refused: c/tests/main.cpp tidy: leaves the analysis",
            "refused: c/tests/main.cpp tidy_from: leaves the analysis"])

    def test_stale_line_refused(self):
        stale = ("c/src/main.cpp", "unit", "-", "c/src/x.cpp")
        self.assertEqual(self.compare([MOVED], [*NAMED, stale]),
                         ["refused: stale, c/src/main.cpp unit - c/src/x.cpp names no difference"])

    def test_each_field_read(self):
        lib, test = B + "analysis.ruleset", B + "analysis-test.ruleset"
        tidy, diag = ".clang-tidy@2", "src/diag/.clang-tidy@4"
        cases = [("line", "listed", "-", "leaves the listing"), ("line", "-", "listed", None),
                 ("module", "log", "-", "leaves its module"), ("layer", "-", "core", None),
                 ("unit", "a.cpp", "b.cpp", None),
                 ("scopes", "walked,core", "walked,test_code", "leaves core, enters test_code"),
                 ("scopes", "test_code", "walked,library,k15", None),
                 ("ruleset", f"{test},none", f"{lib},{lib}", None), ("unit_ruleset", f"{lib},{lib}", lib,
                                                                       "a compile of it is lost"),
                 ("ruleset", lib, test, "enters the test scope's rule set"),
                 ("ruleset", lib, B + "stack.ruleset", "enters a rule set narrower than the library's"),
                 ("ruleset", lib, f"{lib}|x.ruleset", f"is {lib}|x.ruleset, not the library's alone"),
                 ("tidy", tidy, diag, "enters the exemptions of src/diag/"), ("unit_tidy", diag, tidy, None),
                 ("tidy", "-", tidy, None), ("tidy", tidy, "-", "leaves the analysis"),
                 ("tidy", tidy, ".clang-tidy@5", "its configuration's content changes"),
                 ("tidy", tidy, "composed@1", "is composed@1, not the library's alone"),
                 ("unit_tidy_from", tidy, f"{tidy},{B}analysis-test.clang-tidy@3",
                  "enters the test scope's configuration"),
                 ("external", "-", "a/", "a/ is newly external"), ("external", "a/,none", "a/,a/",
                                                                   "a/ is newly external"),
                 ("unit_external", "a/", "a/|b/", "b/ is newly external"), ("external", "a/|b/", "a/", None),
                 ("digest", "x", "y", "the rule set's content changed"), ("reason", "a", "b", None),
                 ("value", "a", "b", "value is no field a move may change")]
        library, narrow = loop_listing_compare.narrower(DATA)
        self.assertEqual([loop_listing_compare.loosens(f, a, b, library, narrow) for f, a, b, _ in cases],
                         [c[3] for c in cases])

    def test_file_and_exit_code(self):
        with tempfile.TemporaryDirectory() as scratch:
            names = [Path(scratch) / name for name in ("before.txt", "after.txt", "map.txt", "expected.txt", "bad.txt")]
            for path, text in zip(names, ("\n".join([TEST_LINE, *EXEMPT]), "\n".join([MOVED, *EXEMPT]),
                                          "t/main.cpp c/src/main.cpp",
                                          "# the move\n" + "\n".join(" ".join(n) for n in NAMED), "a b c")):
                path.write_text(text + "\n", encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()) as shown:
                codes = [loop_listing.main(["compare", *map(str, names[:4])]),
                         loop_listing.main(["compare", *map(str, names[:3])])]
            with self.assertRaises(SystemExit):
                loop_listing.main(["compare", *map(str, names[:3]), str(names[4])])
        self.assertEqual(codes, [0, 1])
        self.assertIn("but for 6 named differences, none loosening", shown.getvalue())

    def test_refusals_of_the_map_unchanged(self):
        moves, merged = loop_listing_compare.read_map("a/ x/\nb/ x/\n")
        merging, into = [*MERGING, *EXEMPT], [*MERGED, *EXEMPT]
        self.assertEqual(loop_listing_compare.compare(merging, into, moves, merged, [])[0],
                         "refused: a/ and b/ move to one path, x/")
        moves, merged = loop_listing_compare.read_map("merge x/ a/ b/\nmerge d/kept.cpp c/gone.cpp\n")
        self.assertEqual(loop_listing_compare.compare(merging, into, moves, merged, []),
                         ["~ d/kept.cpp (from c/gone.cpp) tidy: - -> t@1"])
        named = [("d/kept.cpp", "tidy", "-", "t@1")]
        self.assertEqual(loop_listing_compare.compare(merging, into, moves, merged, named),
                         ["refused: d/kept.cpp tidy: is t@1, not the library's alone"])


class Sections(Fixture):
    """What a compile takes beyond a rule set's path: the rule sets' content,
    its external directories inside the checkout, and the exemptions whole."""

    ctest_labels = ("fake",)

    def ruleset(self, rel, text):
        (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
        (self.root / rel).write_text(text, encoding="utf-8")
        return self.root / rel

    def lines(self, rule_sets, directory):
        command = " ".join(["cl.exe", f'"/analyze:rulesetdirectory{directory}"',
                            *(f"/analyze:ruleset{r}" for r in rule_sets), "/c x.cpp"])
        return loop_listing.ruleset_lines(json.dumps([{"file": "x.cpp", "command": command}]), self.root, self.build)

    def test_digest_covers_includes_and_a_rename(self):
        tools = self.root.parent / "Rule Sets"
        tools.mkdir()
        (tools / "Native.ruleset").write_text('<RuleSet Name="n"><Rule Id="C1" Action="Warning" /></RuleSet>\n',
                                              encoding="utf-8")
        general = '<RuleSet Name="g"><Include Path="Native.ruleset" Action="Default" /></RuleSet>\n'
        test = '<RuleSet Name="t"><Include Path="{}" Action="Default" /></RuleSet>\n'
        lib = self.ruleset("b/analysis.ruleset", general)
        top = self.ruleset("b/analysis-test.ruleset", test.format("analysis.ruleset"))
        first = self.lines([lib, top], tools)
        self.assertEqual([line.split()[0] for line in first], ["ruleset:b/analysis-test.ruleset",
                                                               "ruleset:b/analysis.ruleset"])
        lib.rename(self.root / "b/lint.ruleset")
        top.write_text(test.format("lint.ruleset"), encoding="utf-8")
        renamed = self.lines([self.root / "b/lint.ruleset", top], tools)
        self.assertEqual([line.split()[1] for line in renamed], [line.split()[1] for line in first])
        self.assertEqual(loop_listing_compare.compare(first, renamed, {"b/analysis.ruleset": "b/lint.ruleset"}), [])
        (tools / "Native.ruleset").write_text('<RuleSet Name="n"><Rule Id="C1" Action="None" /></RuleSet>\n',
                                              encoding="utf-8")
        changed = self.lines([self.root / "b/lint.ruleset", top], tools)
        self.assertNotEqual(changed[0].split()[1], first[0].split()[1])
        self.assertNotEqual(changed[1].split()[1], first[1].split()[1])

    def test_an_include_unread_refuses(self):
        lost = self.ruleset("b/a.ruleset", '<RuleSet><Include Path="gone.ruleset" /></RuleSet>\n')
        with self.assertRaisesRegex(SystemExit, "a rule set cannot be read: .*gone.ruleset"):
            self.lines([lost], self.root / "none")
        broken = self.ruleset("b/c.ruleset", "<RuleSet>\n")
        with self.assertRaisesRegex(SystemExit, "a rule set cannot be read: .*c.ruleset"):
            self.lines([broken], self.root / "none")

    def test_external_directories_in_each_spelling(self):
        r, b = self.root.as_posix(), self.build.as_posix()
        commands = [{"file": str(self.root / "a.cpp"),
                     "command": f'cl.exe -external:I{r}/src/a /external:I "{r}/src/b" "-external:I{r}/s c/d" -I{r}/inc'
                                f' -isystem {r}/src/e -isystem{r}/src/f /imsvc{r}/src/g -external:IC:/vcpkg/include'
                                f' -external:I{b}/gen /c a.cpp'},
                    {"file": str(self.root / "a.cpp"), "command": "cl.exe /c a.cpp"},
                    {"file": str(self.root / "b.cpp"), "command": f"cl.exe -I{r}/src/a -external:IC:/vcpkg /c b.cpp"}]
        self.assertEqual(loop_listing.externals(json.dumps(commands), self.root, self.build), {
            "a.cpp": ["none", "s%20c/d/|src/a/|src/b/|src/e/|src/f/|src/g/"]})

    def test_external_on_a_line_and_its_unit_a_file_with_none_unchanged(self):
        scoped = {"a.cpp": ["m", "core", ["walked"]], "a.hpp": ["m", "core", ["walked"]],
                  "b.cpp": ["m", "core", ["walked"]]}
        sets = {"a.cpp": ["none"], "b.cpp": ["none"]}
        lines = loop_listing.lines_of(scoped, {}, {"a.hpp": "a.cpp"}, sets, {}, [], self.root, self.build,
                                      {"a.cpp": ["src/a/"]})
        self.assertEqual(lines, [
            "a.cpp module=m layer=core scopes=walked ruleset=none external=src/a/ tidy=- tidy_from=-",
            "a.hpp module=m layer=core scopes=walked ruleset=- tidy=- tidy_from=- unit=a.cpp unit_ruleset=none"
            " unit_external=src/a/ unit_tidy=- unit_tidy_from=-",
            "b.cpp module=m layer=core scopes=walked ruleset=none tidy=- tidy_from=-"])

    def test_exemptions_carried_whole_and_sorted(self):
        self.assertEqual(EXEMPT, sorted(EXEMPT))
        self.assertIn("exempt:check:msvc:C1:every%20file reason=style,%20100%25%20%231", EXEMPT)
        self.assertIn("exempt:check:clang-tidy:bugprone-x:src/diag/ reason=handlers", EXEMPT)
        self.assertIn("exempt:target:q_seams", EXEMPT)
        self.assertIn("exempt:not_defined:nohal:q_fake reason=no%20HAL", EXEMPT)
        self.assertIn(f"exempt:rule_set:{B}analysis-test.ruleset includes={B}analysis.ruleset reason=test%20code",
                      EXEMPT)
        self.assertIn(f"exempt:rule_set:{B}analysis.ruleset includes=Native.ruleset reason=every%20source", EXEMPT)
        self.assertIn(f"exempt:test_code:msvc value={B}analysis-test.ruleset", EXEMPT)
        self.assertIn('exempt:tidy_on value=["bugprone-*"]', EXEMPT)
        self.assertEqual(len(EXEMPT), 3 + 2 + 1 + 3 + 2 + 1)


LIB, TEST = B + "analysis.ruleset", B + "analysis-test.ruleset"
FILE = f"s/a.cpp module=m layer=core scopes=walked,library ruleset={LIB} tidy=.clang-tidy@2 tidy_from=.clang-tidy@2"
SET = f"ruleset:{LIB} digest=aaa"
CHECK = "exempt:check:msvc:C9:{} reason=r"


class Switched(unittest.TestCase):
    """What a move could switch, refused though named, each by its own
    message; unnamed, a difference as any other; judged by the earlier
    listing's data, whichever checkout runs the compare."""

    ctest_labels = ("fake",)

    def compare(self, before, after, moves=None, expected=None):
        return loop_listing_compare.compare([*before, *EXEMPT], [*after, *EXEMPT], moves or {}, (), expected)

    def named(self, before, after, moves=None):
        """Each difference named, as the compare prints it."""
        lines = self.compare(before, after, moves)
        return [loop_listing_compare.read_expected(line.lstrip("~+- ").replace(": ", " ").replace(" -> ", " "))[0]
                if line.startswith("~") else (line.split()[1], "line", *(
                    ("listed", "-") if line.startswith("-") else ("-", "listed"))) for line in lines]

    def test_a_rule_sets_content_changed(self):
        changed = SET.replace("aaa", "bbb")
        self.assertEqual(self.compare([SET], [changed]), [f"~ ruleset:{LIB} digest: aaa -> bbb"])
        self.assertEqual(self.compare([SET], [changed], None, [(f"ruleset:{LIB}", "digest", "aaa", "bbb")]),
                         [f"refused: ruleset:{LIB} digest: the rule set's content changed"])

    def test_a_rule_set_new_or_gone(self):
        self.assertEqual(self.compare([], [SET], None, [(f"ruleset:{LIB}", "line", "-", "listed")]),
                         [f"refused: ruleset:{LIB} line: a rule set no compile took"])
        self.assertEqual(self.compare([SET], [], None, [(f"ruleset:{LIB}", "line", "listed", "-")]),
                         [f"refused: ruleset:{LIB} line: leaves the listing"])

    def test_a_path_newly_external(self):
        after = FILE.replace("tidy=", "external=src/x/ tidy=", 1)
        self.assertEqual(self.compare([FILE], [after]), ["~ s/a.cpp external: - -> src/x/"])
        self.assertEqual(self.compare([FILE], [after], None, [("s/a.cpp", "external", "-", "src/x/")]),
                         ["refused: s/a.cpp external: src/x/ is newly external"])
        self.assertEqual(self.compare([after], [FILE], None, [("s/a.cpp", "external", "src/x/", "-")]), [])
        moved = after.replace("src/x/", "lib/x/")
        self.assertEqual(self.compare([after, "src/x/h.hpp"], [moved, "lib/x/h.hpp"], {"src/x/": "lib/x/"}), [])

    def test_an_exemption_added_or_widened(self):
        wide = CHECK.format("src/")
        self.assertEqual(self.compare([], [wide]), [f"+ {wide.split()[0]} (only after)"])
        for before, after, why in (([], [wide], "an exemption added"),
                                   ([CHECK.format("src/a/")], [wide], "an exemption's scope grown"),
                                   ([CHECK.format("test%20code")], [CHECK.format("every%20file")],
                                    "an exemption's scope grown"),
                                   ([], ["exempt:target:q_new"], "an exemption added")):
            found = self.compare(before, after, None, self.named(before, after))
            self.assertIn(f"refused: {after[0].split()[0]} line: {why}", found)

    def test_an_exemption_removed_or_narrowed_tightens(self):
        for before, after in (([CHECK.format("src/")], [CHECK.format("src/a/")]), ([CHECK.format("src/")], []),
                              ([CHECK.format("every%20file")], [CHECK.format("test%20code")]),
                              (["exempt:target:q_old"], [])):
            self.assertEqual(self.compare(before, after, None, self.named(before, after)), [])

    def test_a_reason_may_be_named_a_table_value_not(self):
        then, now = CHECK.format("src/"), CHECK.format("src/").replace("reason=r", "reason=s")
        self.assertEqual(self.compare([then], [now], None, [(then.split()[0], "reason", "r", "s")]), [])
        key = f"exempt:rule_set:{TEST}"
        before = EXEMPT
        after = [line if not line.startswith(key) else line.replace("includes=", "includes=x,") for line in EXEMPT]
        named = [(key, "includes", LIB, f"x,{LIB}")]
        self.assertEqual(loop_listing_compare.compare(before, after, {}, (), named),
                         [f"refused: {key} includes: includes is no field a move may change"])

    def test_paths_and_targets_renamed_under_the_map_are_neither(self):
        move = {"src/diag/": "lib/diag/", "q_seams": "q_joins", LIB: "b/lint.ruleset"}
        after = [line.replace("src/diag/", "lib/diag/").replace("q_seams", "q_joins").replace(LIB, "b/lint.ruleset")
                 for line in [*EXEMPT, SET]]
        self.assertEqual(loop_listing_compare.compare([*EXEMPT, SET], after, move), [])
        plain = dict(move)
        plain.pop("q_seams")
        self.assertEqual(loop_listing_compare.compare([*EXEMPT, SET], after, plain), [
            "- exempt:target:q_seams (only before)", "+ exempt:target:q_joins (only after)"][::-1])

    def test_the_table_under_the_map(self):
        move = {B: "x/", "s/a.cpp": "s/b.cpp"}
        moved = [line.replace(B, "x/") for line in EXEMPT]
        before = FILE.replace(LIB, TEST)
        after = FILE.replace("s/a.cpp", "s/b.cpp").replace(LIB, "x/analysis.ruleset")
        named = [("s/b.cpp", "ruleset", "x/analysis-test.ruleset", "x/analysis.ruleset")]
        self.assertEqual(loop_listing_compare.compare([before, *EXEMPT], [after, *moved], move, (), named), [])

    def test_the_earlier_listings_data_judges(self):
        stack = B + "stack.ruleset"
        own = {**DATA, "rule_sets": {**DATA["rule_sets"], TEST: {"includes": ["stack.ruleset"]}}}
        theirs = loop_listing.exempt_lines(own)
        was, now = FILE, FILE.replace(LIB, stack)
        named = [("s/a.cpp", "ruleset", LIB, stack)]
        for after in (EXEMPT, theirs):  # the later listing's own table never decides
            found = loop_listing_compare.compare([was, *EXEMPT], [now, *after], {}, (), named)
            self.assertIn("refused: s/a.cpp ruleset: enters a rule set narrower than the library's", found)
        self.assertEqual(loop_listing_compare.compare([was, *theirs], [now, *theirs], {}, (), named), [])

    def test_a_listing_without_data_refused(self):
        with self.assertRaisesRegex(SystemExit, "carries no exemptions' data"):
            loop_listing_compare.compare([FILE], [FILE], {}, (), [])


class Refusing(unittest.TestCase):
    """What refuses a listing, with the refuser's own text: cmake failing, a
    table the structure refuses, a commit without the exemptions' list."""

    ctest_labels = ("fake",)

    def test_cmake_text_kept(self):
        failed = subprocess.CompletedProcess(["cmake"], 1, "said out\n", "CMake Error: the refusal\n")
        with mock.patch.object(loop_listing.subprocess, "run", return_value=failed), \
                self.assertRaisesRegex(SystemExit, "(?s)cmake failed, exit 1.*said out.*CMake Error: the refusal"):
            loop_listing.cmake_lines(["-P", "x.cmake"])

    def test_the_reader_keeps_the_tables_errors(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            where = root / "foundation/devkit/x/module.json"
            where.parent.mkdir(parents=True)
            table = json.loads(layers.module_file("x", "devkit", [("x", ["foundation/devkit/x/"])]))
            where.write_text(json.dumps({**table, "from": "tests/x/"}), encoding="utf-8")
            kept, errors = layers.read(root)
            self.assertIs(kept, layers.of(root))
            self.assertEqual(errors, layers.MODULES["tree"](root)[1])
            self.assertTrue(errors)

    def test_a_refused_table_refuses_the_listing(self):
        root = Path(layers.__file__).resolve().parents[3]
        with tempfile.TemporaryDirectory() as scratch:
            paths = Path(scratch) / "paths.txt"
            paths.write_text("a.cpp\n", encoding="utf-8")
            with mock.patch.object(layers, "read", return_value=(None, ["x: adds from"])), \
                    self.assertRaisesRegex(SystemExit, "(?s)the structure refuses this tree's table.*x: adds from"):
                loop_listing.child_scopes(root, paths)

    def test_a_commit_without_the_list_refused(self):
        scoped = {"files": {}, "exemptions": "b/list.json"}
        with tempfile.TemporaryDirectory() as checkout, tempfile.TemporaryDirectory() as scratch, \
                mock.patch.object(loop_listing, "committed_tree", return_value=["a.cpp"]), \
                mock.patch.object(loop_listing, "scopes", return_value=scoped), \
                self.assertRaisesRegex(SystemExit, "c0 has no b/list.json"):
            loop_listing.listing(checkout, "c0", scratch)


if __name__ == "__main__":
    unittest.main()
