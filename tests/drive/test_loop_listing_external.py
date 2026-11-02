"""What else the listing (tools/loop_listing.py) sees of a move that could
switch a file's checks: a repository header a compile takes as external by
angle brackets, a compile's /external:env:, and each rule set the exemptions'
data names, a compile naming it or none; and each judged by the compare; over
fixture inputs in a scratch directory, no build needed.
"""

import json
import tempfile
import unittest
from pathlib import Path

from tests.drive.test_loop_listing import EXEMPT, FILE, B, Fixture
from tools import loop_listing, loop_listing_compare

FLAG = "/external:anglebrackets"
HEADER = "src/diag/x.hpp"


class Written(Fixture):
    """Fixture's tree, a file written into it and compiles over it."""

    def write(self, rel, text=""):
        (self.root / rel).parent.mkdir(parents=True, exist_ok=True)
        (self.root / rel).write_text(text, encoding="utf-8")
        return self.root / rel

    def source(self, text):
        return self.write("src/a.cpp", text)

    def compile(self, *flags, at="src"):
        return {"file": str(self.root / "src/a.cpp"),
                "command": " ".join(["cl.exe", *flags, f'"-I{self.root / at}"', "/c", "a.cpp"])}

    def externals(self, *compiles):
        return loop_listing.externals(json.dumps(compiles), self.root, self.build)


class Angled(Written):
    """A repository header taken as external by a compile's angle include:
    found as the compile finds it, on that compile's line."""

    ctest_labels = ("fake",)

    def setUp(self):
        super().setUp()
        self.write(HEADER)

    def test_an_angle_include_of_a_repository_header(self):
        self.source("#include <diag/x.hpp>\n#include <vector>\n")
        self.assertEqual(self.externals(self.compile(FLAG)), {"src/a.cpp": [HEADER]})
        self.assertEqual(self.externals(self.compile("-external:anglebrackets")), {"src/a.cpp": [HEADER]})

    def test_quotes_or_no_flag_take_nothing(self):
        self.source('#include "diag/x.hpp"\n')
        self.assertEqual(self.externals(self.compile(FLAG)), {})
        self.source("#include <diag/x.hpp>\n")
        self.assertEqual(self.externals(self.compile()), {})

    def test_the_walk_reaches_every_depth_and_ends(self):
        self.source('#include "b.hpp"\n')
        self.write("src/b.hpp", '#include "c.hpp"\n')
        self.write("src/c.hpp", '#include "b.hpp"\n  #  include  <diag/x.hpp>\n')
        self.assertEqual(self.externals(self.compile(FLAG)), {"src/a.cpp": [HEADER]})

    def test_what_an_external_header_includes_is_external_too(self):
        self.write(HEADER, '#include "y.hpp"\n')
        self.write("src/diag/y.hpp")
        both = {"src/a.cpp": [f"{HEADER}|src/diag/y.hpp"]}
        self.source("#include <diag/x.hpp>\n")
        self.assertEqual(self.externals(self.compile(FLAG)), both)
        self.source('#include "diag/y.hpp"\n#include <diag/x.hpp>\n')
        self.assertEqual(self.externals(self.compile(FLAG)), both, "reached by quotes first, it counts")

    def test_a_directive_read_as_text(self):
        self.source("// #include <diag/x.hpp>\n")
        self.assertEqual(self.externals(self.compile(FLAG)), {})
        self.source("#if 0\n#include <diag/x.hpp>\n#endif\n")
        self.assertEqual(self.externals(self.compile(FLAG)), {"src/a.cpp": [HEADER]})

    def test_found_by_the_compiles_directories_alone(self):
        self.source("#include <x.hpp>\n")
        self.assertEqual(self.externals(self.compile(FLAG)), {}, "a suffix is no find")
        self.assertEqual(self.externals(self.compile(FLAG, at="src/diag")), {"src/a.cpp": [HEADER]})
        self.source('#include "x.hpp"\n')
        self.write("src/x.hpp", "#include <x.hpp>\n")
        self.assertEqual(self.externals(self.compile(FLAG, at="src/diag")), {"src/a.cpp": [HEADER]},
                         "an angle include is not looked for beside its includer")

    def test_a_directory_earlier_in_the_order_wins(self):
        with tempfile.TemporaryDirectory() as outside:
            (Path(outside) / "diag").mkdir()
            (Path(outside) / "diag/x.hpp").write_text("", encoding="utf-8")
            self.source("#include <diag/x.hpp>\n")
            self.assertEqual(self.externals(self.compile(FLAG, f'"-I{outside}"')), {})
            after = self.compile(FLAG)
            after["command"] += f' "-I{outside}"'
            self.assertEqual(self.externals(after), {"src/a.cpp": [HEADER]})

    def test_a_quoted_include_is_found_beside_an_includer_above(self):
        self.source('#include "sub/b.hpp"\n')
        self.write("src/sub/b.hpp", '#include "c.hpp"\n')
        self.write("src/c.hpp", "#include <x.hpp>\n")
        self.assertEqual(self.externals(self.compile(FLAG, at="src/diag")), {"src/a.cpp": [HEADER]})

    def test_the_build_tree_is_walked_and_stays_out(self):
        (self.build / "gen").mkdir()
        (self.build / "gen/g.hpp").write_text("#include <diag/x.hpp>\n", encoding="utf-8")
        self.source("#include <g.hpp>\n")
        self.assertEqual(self.externals(self.compile(FLAG, f'"-I{self.build / "gen"}"')), {"src/a.cpp": [HEADER]})

    def test_a_source_the_configure_has_not_made(self):
        self.assertEqual(self.externals(self.compile(FLAG)), {})

    def test_each_compile_apart_a_directory_beside_it(self):
        self.source("#include <diag/x.hpp>\n")
        (self.root / "ext").mkdir()
        self.assertEqual(self.externals(self.compile(), self.compile(FLAG, f'"/external:I{self.root / "ext"}"')),
                         {"src/a.cpp": ["ext/|" + HEADER, "none"]})

    def test_the_external_read_of_an_includer_and_of_a_header_through_it(self):
        self.source("#include <diag/x.hpp>\n")
        taken = ("m", "core", ["walked"])
        lines = loop_listing.lines_of({"src/a.cpp": taken, HEADER: taken}, {}, {HEADER: "src/a.cpp"}, {}, {}, [],
                                      self.root, self.build, self.externals(self.compile(FLAG)))
        includer, header = (dict(f.split("=", 1) for f in line.split()[1:]) for line in lines)
        self.assertEqual((includer["external"], header["unit_external"]), (HEADER, HEADER))
        self.assertNotIn("external", header)


class Environment(Written):
    """A compile's /external:env: as spelled, its directories the machine's."""

    ctest_labels = ("fake",)

    def test_spelled(self):
        self.source("")
        self.assertEqual(self.externals(self.compile("/external:env:X", '"-external:env:Y Z"')),
                         {"src/a.cpp": ["-external:env:Y%20Z|/external:env:X"]})


class RuleSets(Written):
    """Each rule set the exemptions' data names digested, a compile naming it or none."""

    ctest_labels = ("fake",)
    STACK = B + "stack.ruleset"
    RULE = '<RuleSet><Rules><Rule Id="{}" Action="Error" /></Rules></RuleSet>\n'

    def lines(self, commands, *listed):
        return loop_listing.ruleset_lines(json.dumps(commands), self.root, self.build, listed)

    def test_a_rule_set_no_compile_names(self):
        self.write(self.STACK, self.RULE.format("C6262"))
        before = self.lines([], self.STACK)
        self.assertRegex(before[0], rf"^ruleset:{self.STACK} digest=[0-9a-f]{{12}}$")
        self.write(self.STACK, self.RULE.format("C6263"))
        self.assertEqual(len(before), 1)
        self.assertNotEqual(self.lines([], self.STACK), before)

    def test_named_by_a_compile_too_it_has_one_line(self):
        at = self.write(self.STACK, self.RULE.format("C6262"))
        named = [{"file": "x.cpp", "command": f'cl.exe "/analyze:ruleset{at}" /c x.cpp'}]
        self.assertEqual(self.lines(named, self.STACK), self.lines([], self.STACK))

    def test_its_include_is_read_in_any_compiles_directory(self):
        self.write("toolset/Vs.ruleset", self.RULE.format("C1"))
        self.write(self.STACK, '<RuleSet><Include Path="Vs.ruleset" Action="Default" /></RuleSet>\n')
        command = f'cl.exe "/analyze:rulesetdirectory{self.root / "toolset"}" /c x.cpp'
        before = self.lines([{"file": "x.cpp", "command": command}], self.STACK)
        self.write("toolset/Vs.ruleset", self.RULE.format("C2"))
        self.assertNotEqual(self.lines([{"file": "x.cpp", "command": command}], self.STACK), before)
        with self.assertRaises(SystemExit):
            self.lines([], self.STACK)

    def test_one_unread_refuses_the_listing(self):
        with self.assertRaises(SystemExit) as caught:
            self.lines([], self.STACK)
        self.assertIn("a rule set cannot be read", str(caught.exception))


class Judged(unittest.TestCase):
    """Each newly external, refused though named; a rename under the map is none."""

    ctest_labels = ("fake",)

    def compare(self, before, after, moves=None, expected=None):
        return loop_listing_compare.compare([before, *EXEMPT], [after, *EXEMPT], moves or {}, (), expected)

    def test_a_header_newly_external(self):
        after = f"{FILE} external=s/x.hpp"
        self.assertEqual(self.compare(FILE, after), ["~ s/a.cpp external: - -> s/x.hpp"])
        self.assertEqual(self.compare(FILE, after, None, [("s/a.cpp", "external", "-", "s/x.hpp")]),
                         ["refused: s/a.cpp external: s/x.hpp is newly external"])

    def test_a_header_newly_external_through_its_unit(self):
        before = "s/x.hpp module=m unit=s/a.cpp"
        after = f"{before} unit_external=s/x.hpp"
        self.assertEqual(self.compare(before, after, None, [("s/x.hpp", "unit_external", "-", "s/x.hpp")]),
                         ["refused: s/x.hpp unit_external: s/x.hpp is newly external"])

    def test_one_more_compile_taking_a_header(self):
        then, now = "none,s/x.hpp", "s/x.hpp,s/x.hpp"
        self.assertEqual(self.compare(f"{FILE} external={then}", f"{FILE} external={now}", None,
                                      [("s/a.cpp", "external", then, now)]),
                         ["refused: s/a.cpp external: s/x.hpp is newly external"])

    def test_an_environment_newly_external(self):
        name = "/external:env:X"
        self.assertEqual(self.compare(FILE, f"{FILE} external={name}", None, [("s/a.cpp", "external", "-", name)]),
                         [f"refused: s/a.cpp external: {name} is newly external"])

    def test_a_header_renamed_under_the_map_is_equal(self):
        before, after = ([f"{FILE} external={at}", f"{at} module=m"] for at in ("s/x.hpp", "t/x.hpp"))
        moved = {"s/x.hpp": "t/x.hpp"}
        self.assertEqual(loop_listing_compare.compare([*before, *EXEMPT], [*after, *EXEMPT], moved), [])

    def test_a_header_no_longer_external_tightens(self):
        gone = ("s/a.cpp", "external", "s/x.hpp", "-")
        self.assertEqual(self.compare(f"{FILE} external=s/x.hpp", FILE, None, [gone]), [])

    def test_a_rule_set_no_compile_names_changed(self):
        stack = f"ruleset:{B}stack.ruleset"
        self.assertEqual(self.compare(f"{stack} digest=aaa", f"{stack} digest=bbb", None,
                                      [(stack, "digest", "aaa", "bbb")]),
                         [f"refused: {stack} digest: the rule set's content changed"])

    def test_a_listing_with_itself_is_equal(self):
        line = f"{FILE} external=s/x.hpp|/external:env:X"
        self.assertEqual(self.compare(line, line), [])


if __name__ == "__main__":
    unittest.main()
