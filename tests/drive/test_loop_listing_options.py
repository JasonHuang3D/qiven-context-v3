"""Each compile's warning and analysis options as the listing
(tools/loop_listing.py) reads them, as spelled and in order, and each change
of them judged by the compare: refused though named, a drop or a weakening
told as such; over fixture inputs in a scratch directory, no build needed.
"""

import json
import unittest

from tests.drive.test_loop_listing import EXEMPT, FILE, LIB, TEST, Fixture
from tools import loop_listing, loop_listing_compare

OTHERS = "/nologo /DWIN32 -std:c17 /Fothird_party\\x.obj /Fdx.pdb /FS /EHsc /permissive- /utf-8"


class Spelled(Fixture):
    """The options of each compile, as it spells them, in its order."""

    ctest_labels = ("fake",)

    def options(self, *commands):
        compiles = [{"file": str(self.root / "src/a.cpp"), "command": f"cl.exe {c} /c a.cpp"} for c in commands]
        return loop_listing.options(json.dumps(compiles), self.root, self.build)

    def test_every_form_in_the_compiles_order(self):
        spelled = ("/W4 -WX /wd4073 /analyze:WX- /external:W0 /w14996 /Wv:18 /we4700 /wo4701 /Wall /w /W0 "
                   "-analyze:pluginEspXEngine.dll /analyze /analyze- /analyze:external- /WX- -W3 /w2C4100")
        found = self.options(f"{OTHERS} {spelled}")
        self.assertEqual(found, {"src/a.cpp": ["|".join(spelled.split()[:-1])]}, "a /w2 with no number is none")

    def test_a_rule_set_and_its_directory_from_the_root(self):
        at, toolset = self.root / LIB, self.root / "toolset"
        found = self.options(f'/W4 "/analyze:ruleset{at}" /analyze:rulesetdirectory "{toolset}" -analyze:ruleset {at}')
        self.assertEqual(found, {"src/a.cpp": [
            f"/W4|/analyze:ruleset{LIB}|/analyze:rulesetdirectorytoolset|-analyze:ruleset{LIB}"]})

    def test_a_compile_with_none_and_the_compiles_sorted(self):
        self.assertEqual(self.options("/WX /W4", OTHERS), {"src/a.cpp": ["/WX|/W4", "none"]})

    def test_a_line_carries_them_and_a_header_its_units(self):
        taken = ("m", "core", ["walked"])
        lines = loop_listing.lines_of({"src/a.cpp": taken, "src/a.hpp": taken}, {}, {"src/a.hpp": "src/a.cpp"}, {},
                                      {}, [], self.root, self.build, {}, self.options("/W4 /WX"))
        self.assertIn(" options=/W4|/WX ", lines[0])
        self.assertIn(" unit_options=/W4|/WX ", lines[1])
        self.assertNotIn(" options=", lines[1])


class Judged(unittest.TestCase):
    """Any change of a file's options refused though named, told as a drop
    or a weakening, but the rule set's path alone, which its ruleset field
    judges; equal under a rename map."""

    ctest_labels = ("fake",)
    BASE = f"{FILE} options=/W4|/WX"

    def compare(self, before, after, moves=None, field="options"):
        then, now = (line.split(f" {field}=")[1].split()[0] if f" {field}=" in line else "-"
                     for line in (before, after))
        return loop_listing_compare.compare([before, *EXEMPT], [after, *EXEMPT], moves or {}, (),
                                            [("s/a.cpp", field, then, now)])

    def refused(self, after, why, before=BASE, field="options"):
        self.assertEqual(self.compare(before, after, field=field), [f"refused: s/a.cpp {field}: {why}"])

    def test_an_option_dropped(self):
        self.refused(f"{FILE} options=/W4", "drops /WX")

    def test_a_level_lowered(self):
        self.refused(f"{FILE} options=/W3|/WX", "drops /W4; weakens by /W3")

    def test_a_warning_turned_off(self):
        self.refused(f"{FILE} options=/W4|/WX|/wd26400", "weakens by /wd26400")

    def test_analysis_errors_turned_off(self):
        before = f"{FILE} options=/W4|/WX|/analyze"
        self.refused(f"{before}|/analyze:WX-", "weakens by /analyze:WX-", before)

    def test_an_option_added(self):
        self.refused(f"{FILE} options=/W4|/WX|/we4700", "adds /we4700")

    def test_every_option_lost_with_the_compile(self):
        self.refused(FILE, "drops /W4, /WX")

    def test_the_order_changed(self):
        self.refused(f"{FILE} options=/WX|/W4", "its options change order or compile")

    def test_a_units_option_dropped(self):
        before = "s/a.hpp module=m unit=s/a.cpp unit_options=/W4|/WX"
        self.assertEqual(loop_listing_compare.compare(
            [before, *EXEMPT], ["s/a.hpp module=m unit=s/a.cpp unit_options=/W4", *EXEMPT], {}, (),
            [("s/a.hpp", "unit_options", "/W4|/WX", "/W4")]), ["refused: s/a.hpp unit_options: drops /WX"])

    def test_unnamed_it_is_a_difference(self):
        self.assertEqual(loop_listing_compare.compare([self.BASE, *EXEMPT], [f"{FILE} options=/W4", *EXEMPT], {}),
                         ["~ s/a.cpp options: /W4|/WX -> /W4"])

    def test_a_rule_set_renamed_under_the_map_is_equal(self):
        before, after = ([f"{FILE} options=/W4|/analyze:ruleset{at}", f"{at} module=m"] for at in ("s/r.ruleset",
                                                                                                    "t/r.ruleset"))
        self.assertEqual(loop_listing_compare.compare([*before, *EXEMPT], [*after, *EXEMPT],
                                                      {"s/r.ruleset": "t/r.ruleset"}), [])

    def swapped(self, then, now, named=("ruleset", "options"), beside=""):
        """The compare of the rule set's path swapped then to now, in its field and its place among the options."""
        before, after = (f"{FILE.replace(LIB, at)} options=/W4|/analyze:ruleset{at}" for at in (then, now))
        after += beside
        field = {f: [line.split(f" {f}=")[1].split()[0] for line in (before, after)] for f in named}
        return loop_listing_compare.compare([before, *EXEMPT], [after, *EXEMPT], {}, (),
                                            [("s/a.cpp", f, *field[f]) for f in named])

    def test_the_rule_set_swapped_to_the_librarys_with_its_field_named(self):
        self.assertEqual(self.swapped(TEST, LIB), [])

    def test_the_rule_set_swapped_its_field_unnamed(self):
        self.assertEqual(self.swapped(TEST, LIB, named=("options",)), [
            f"~ s/a.cpp ruleset: {TEST} -> {LIB}",
            "refused: s/a.cpp options: its rule set's path changes, and no tightening of its ruleset field is named"])

    def test_the_rule_set_swapped_to_the_tests(self):
        self.assertEqual(self.swapped(LIB, TEST), [
            "refused: s/a.cpp options: its rule set's path changes, and no tightening of its ruleset field is named",
            "refused: s/a.cpp ruleset: enters the test scope's rule set"])

    def test_the_rule_set_swapped_and_another_option_changed(self):
        self.assertEqual(self.swapped(TEST, LIB, beside="|/WX"), [
            f"refused: s/a.cpp options: drops /analyze:ruleset{TEST}; adds /WX, /analyze:ruleset{LIB}"])

    def test_a_listing_with_itself_is_equal(self):
        self.assertEqual(loop_listing_compare.compare([self.BASE, *EXEMPT], [self.BASE, *EXEMPT], {}), [])


if __name__ == "__main__":
    unittest.main()
