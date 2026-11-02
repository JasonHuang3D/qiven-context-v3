"""A move as the compare (tools/loop_listing_compare.py) reads it: a scope that
follows from a file's layer alone, lost beside that layer's named difference;
each move's own map, a repeated OLD refused and a file moved twice passing;
over fixture listings, no build needed.
"""

import contextlib
import io
import tempfile
import unittest
from pathlib import Path

from tests.drive.test_loop_listing import EXEMPT
from tools import loop_listing_compare

LIBRARY = "walked,library,core"
KIT = "walked"


def row(path="s/a.cpp", layer="services", scopes=LIBRARY):
    return f"{path} module=m layer={layer} scopes={scopes}"


class Listings(unittest.TestCase):
    """Fixture listings, the exemptions' lines beside each."""

    def compare(self, before, after, moves=None, expected=None):
        return loop_listing_compare.compare([*before, *EXEMPT], [*after, *EXEMPT], moves or {}, (), expected)


class OfTheLayer(Listings):
    """library and core lost only where the same file's layer is named as changing."""

    ctest_labels = ("fake",)
    LAYER = ("s/a.cpp", "layer", "services", "devkit")
    SCOPES = ("s/a.cpp", "scopes", LIBRARY, KIT)

    def lost(self, *named, then=LIBRARY, now=KIT, layer="devkit"):
        return self.compare([row(scopes=then)], [row(layer=layer, scopes=now)], None, list(named))

    def test_both_lost_beside_the_layer(self):
        self.assertEqual(self.lost(self.LAYER, self.SCOPES), [])

    def test_either_alone_lost_beside_the_layer(self):
        for now in ("walked,core", "walked,library"):
            self.assertEqual(self.lost(self.LAYER, ("s/a.cpp", "scopes", LIBRARY, now), now=now), [], now)

    def test_the_layer_not_named_is_refused_and_told(self):
        self.assertEqual(self.lost(self.SCOPES), ["~ s/a.cpp layer: services -> devkit",
                                                  "refused: s/a.cpp scopes: leaves core, leaves library"])

    def test_no_layer_changed_is_refused(self):
        self.assertEqual(self.lost(self.SCOPES, layer="services"),
                         ["refused: s/a.cpp scopes: leaves core, leaves library"])

    def test_a_layer_line_naming_no_difference_grants_nothing(self):
        self.assertEqual(self.lost(self.LAYER, self.SCOPES, layer="services"),
                         ["refused: s/a.cpp scopes: leaves core, leaves library",
                          "refused: stale, s/a.cpp layer services devkit names no difference"])

    def test_another_files_layer_grants_nothing(self):
        other = ("s/b.cpp", "layer", "services", "devkit")
        before = [row(), row("s/b.cpp")]
        after = [row(scopes=KIT), row("s/b.cpp", layer="devkit")]
        self.assertEqual(self.compare(before, after, None, [self.SCOPES, other]),
                         ["refused: s/a.cpp scopes: leaves core, leaves library"])

    def test_a_merge_grants_only_the_file_whose_layer_changes(self):
        before = [row("s/a.cpp"), row("s/b.cpp", layer="devkit", scopes="walked,library")]
        named = [("n.cpp", "layer", "services", "devkit"), ("n.cpp", "scopes", LIBRARY, KIT),
                 ("n.cpp", "scopes", "walked,library", KIT)]
        out = loop_listing_compare.compare([*before, *EXEMPT], [row("n.cpp", "devkit", KIT), *EXEMPT],
                                           {"s/a.cpp": "n.cpp", "s/b.cpp": "n.cpp"}, ("n.cpp",), named)
        self.assertEqual(out, ["refused: n.cpp scopes: leaves library"])

    def test_any_other_scope_is_refused_beside_the_layer(self):
        for scope in ("walker", "walked", "layered", "subset", "k15", "analysed"):
            then = f"library,core,{scope}"
            self.assertEqual(self.lost(self.LAYER, ("s/a.cpp", "scopes", then, "library"), then=then, now="library"),
                             [f"refused: s/a.cpp scopes: leaves {scope}"], scope)

    def test_test_code_entered_stays_refused_beside_the_layer(self):
        now = "walked,test_code"
        self.assertEqual(self.lost(self.LAYER, ("s/a.cpp", "scopes", LIBRARY, now), now=now),
                         ["refused: s/a.cpp scopes: enters test_code"])

    def test_the_layer_gone_stays_refused(self):
        layer = ("s/a.cpp", "layer", "services", "-")
        self.assertEqual(self.lost(layer, self.SCOPES, layer="-"), ["refused: s/a.cpp layer: leaves its layer"])

    def test_gained_stays_a_tightening(self):
        gained = ("s/a.cpp", "scopes", KIT, LIBRARY)
        self.assertEqual(self.compare([row(layer="devkit", scopes=KIT)], [row()], None,
                                      [("s/a.cpp", "layer", "devkit", "services"), gained]), [])
        self.assertEqual(self.lost(gained, then=KIT, now=LIBRARY, layer="services"), [])


class RepeatedOld(unittest.TestCase):
    """A map naming an OLD on two lines is refused, both lines named: the first would be lost."""

    ctest_labels = ("fake",)

    def refused(self, text):
        with self.assertRaises(SystemExit) as caught:
            loop_listing_compare.read_map(text)
        return str(caught.exception)

    def test_two_lines_of_one_old(self):
        self.assertEqual(self.refused("# moves\ns/a.cpp t/a.cpp\n\ns/b.cpp t/b.cpp\ns/a.cpp t/dummy.cpp  # again\n"),
                         "loop_listing: refused: s/a.cpp is named twice: line 2: s/a.cpp t/a.cpp; "
                         "line 5: s/a.cpp t/dummy.cpp  # again")

    def test_the_same_line_twice(self):
        self.assertIn("line 1: s/a.cpp t/a.cpp; line 2: s/a.cpp t/a.cpp", self.refused("s/a.cpp t/a.cpp\n" * 2))

    def test_a_merge_and_a_move(self):
        self.assertIn("line 1: merge n.cpp s/a.cpp s/b.cpp; line 2: s/b.cpp t/b.cpp",
                      self.refused("merge n.cpp s/a.cpp s/b.cpp\ns/b.cpp t/b.cpp\n"))

    def test_two_merges_and_one_merge_itself(self):
        self.assertIn("line 1: merge n s/a s/b; line 2: merge m s/a s/c",
                      self.refused("merge n s/a s/b\nmerge m s/a s/c\n"))
        self.assertIn("line 1: merge n s/a s/a; line 1: merge n s/a s/a", self.refused("merge n s/a s/a\n"))

    def test_a_directory_and_a_file_under_it_are_two_olds(self):
        self.assertEqual(loop_listing_compare.read_map("s/ t/\ns/a.cpp u/a.cpp\n")[0],
                         {"s/": "t/", "s/a.cpp": "u/a.cpp"})


class MovedTwice(Listings):
    """Each move its own map, read against the commit before it."""

    ctest_labels = ("fake",)
    FIRST, SECOND, THIRD = (f"{at}/a.cpp" for at in ("s", "t", "u"))

    def lines(self, path):
        return [row(path), row("s/kept.cpp")]

    def test_two_maps_each_against_the_commit_before_it(self):
        self.assertEqual(self.compare(self.lines(self.FIRST), self.lines(self.SECOND), {self.FIRST: self.SECOND}), [])
        self.assertEqual(self.compare(self.lines(self.SECOND), self.lines(self.THIRD), {self.SECOND: self.THIRD}), [])

    def test_the_first_map_read_again_is_refused(self):
        self.assertEqual(self.compare(self.lines(self.SECOND), self.lines(self.THIRD),
                                      {self.FIRST: self.SECOND, self.SECOND: self.THIRD}),
                         [f"refused: {self.FIRST} is no path of the earlier listing"])

    def test_the_second_map_against_the_first_commit_is_refused(self):
        self.assertEqual(self.compare(self.lines(self.FIRST), self.lines(self.THIRD), {self.SECOND: self.THIRD}),
                         [f"refused: {self.SECOND} is no path of the earlier listing"])

    def test_a_map_from_its_file_by_path(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            moves = root / "moves"
            moves.mkdir()
            (moves / "second.map").write_text(f"{self.SECOND} {self.THIRD}\n", encoding="utf-8")
            (moves / "second.expected").write_text(f"{self.THIRD} layer services devkit\n", encoding="utf-8")
            (root / "before").write_text("\n".join([row(self.SECOND), *EXEMPT]) + "\n", encoding="utf-8")
            (root / "after").write_text("\n".join([row(self.THIRD, "devkit"), *EXEMPT]) + "\n", encoding="utf-8")
            with contextlib.redirect_stdout(io.StringIO()) as told:
                code = loop_listing_compare.main([str(root / name) for name in (
                    "before", "after", "moves/second.map", "moves/second.expected")])
        self.assertEqual((code, told.getvalue()),
                         (0, "loop_listing: the listings are equal but for 1 named differences, none loosening\n"))


if __name__ == "__main__":
    unittest.main()
