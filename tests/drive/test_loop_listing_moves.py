"""A move as the compare (tools/loop_listing_compare.py) reads it: a scope that
follows from a file's layer alone, lost beside that layer's named difference;
over fixture listings, no build needed.
"""

import unittest

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


if __name__ == "__main__":
    unittest.main()
