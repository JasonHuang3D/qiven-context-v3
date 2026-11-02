"""The listing a move runs before and after (tools/loop_listing.py): its
reading of a clang-tidy step and of each compile's rule sets, the
configuration a step resolves to, a header by the unit it compiles through,
and two listings compared with and without a rename map, a map refused and
a merge named; over fixture inputs in a scratch directory, no build needed.
"""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from tools import loop_listing


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
        self.assertEqual(loop_listing.compare(BEFORE, BEFORE, {}), [])

    def test_without_a_map(self):
        self.assertEqual(loop_listing.compare(BEFORE, AFTER, {}), [
            "+ src/diag/a.cpp (only after)", "- src/qiven3/a.cpp (only before)",
            "~ src/qiven3/a.hpp unit: src/qiven3/a.cpp -> src/diag/a.cpp",
            "~ src/qiven3/a.hpp unit_tidy: .clang-tidy@1 -> src/diag/.clang-tidy@2"])

    def test_under_a_map_the_configuration_still_differs(self):
        moves, merged = loop_listing.read_map("# one move\nsrc/qiven3/a.cpp src/diag/a.cpp  # the source\n\n")
        self.assertEqual((moves, merged), ({"src/qiven3/a.cpp": "src/diag/a.cpp"}, set()))
        self.assertEqual(loop_listing.compare(BEFORE, AFTER, moves), [
            "~ src/diag/a.cpp tidy: .clang-tidy@1 -> src/diag/.clang-tidy@2",
            "~ src/diag/a.cpp tidy_from: .clang-tidy@1 -> .clang-tidy@1,src/diag/.clang-tidy@2",
            "~ src/qiven3/a.hpp unit_tidy: .clang-tidy@1 -> src/diag/.clang-tidy@2"])

    def test_a_directory_moved_with_its_configuration(self):
        after = [line.replace("src/qiven3/", "src/new/") for line in BEFORE]
        moves, _ = loop_listing.read_map("src/qiven3/ src/new/\n")
        self.assertEqual(loop_listing.compare(BEFORE, after, moves), [])
        self.assertEqual(loop_listing.renamed("src/qiven3x/a.cpp", moves), "src/qiven3x/a.cpp")

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
        self.assertEqual(loop_listing.compare(before, after, {"a/": "c/"}), [])
        self.assertEqual(loop_listing.compare(before, ["c/x.cpp ruleset=c/r.ruleset,c/r.ruleset"], {"a/": "c/"}),
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
        moves, merged = loop_listing.read_map("a/ x/\nb/ x/\nc/gone.cpp d/kept.cpp\ne/ y/\n")
        self.assertEqual(loop_listing.compare(MERGING, MERGED, moves, merged), [
            "refused: c/gone.cpp moves to d/kept.cpp, where d/kept.cpp stays",
            "refused: e/ is no path of the earlier listing",
            "refused: a/ and b/ move to one path, x/",
            "refused: a/CMakeLists.txt and b/CMakeLists.txt become one path, x/CMakeLists.txt"])

    def test_a_staying_directory_and_two_paths_refused(self):
        self.assertEqual(loop_listing.compare(MERGING, MERGING, {"b/": "d/"}),
                         ["refused: b/ moves to d/, where d/kept.cpp stays"])
        self.assertEqual(loop_listing.compare(MERGING, MERGED, {"a/one.cpp": "x/two.cpp", "b/": "x/"}),
                         ["refused: a/one.cpp and b/two.cpp become one path, x/two.cpp"])

    def test_merge_named_accepted_and_compared(self):
        moves, merged = loop_listing.read_map("merge x/ a/ b/\nmerge d/kept.cpp c/gone.cpp\n")
        self.assertEqual(merged, {"x/", "d/kept.cpp"})
        self.assertEqual(loop_listing.compare(MERGING, MERGED, moves, merged), [
            "~ d/kept.cpp (from c/gone.cpp) tidy: - -> t@1"])
        with self.assertRaises(SystemExit):
            loop_listing.read_map("a/ x/ y/\n")


if __name__ == "__main__":
    unittest.main()
