"""The structure rules' scopes by the tree's table (layers.py), with no
build's records: core logic (B17) by layer, held in every file of src/ it
held when src/ was its literal root, never in devkit's runner, layer 0, nor
in the vendored SQLite, layer 2's, nor in test code; B1's text over every
tree tools/trees.json names, allowed in the vendored SQLite's module, the
wrapper and the build's digest pin by path; B9's own headers a library's;
the safe subset over the libraries, the test kit and tests/support/, its
test code the table's; and over a planted tree with its own table, B25's
two globals of the runner's cases allowed by name and a third refused,
B28's Case allowed and another type refused, B27 refused in the runner,
and B1's text refused in a core module's directory."""

import tempfile
import unittest
from pathlib import Path

from tests.structure import layers, rules, subset

TREE = layers.of(rules.ROOT)
CXX = (".hpp", ".cpp", ".c", ".h")


def files():
    return rules.source_files(rules.ROOT)


def was_core(rel):
    """Core logic as it was read, src/ its literal root."""
    return rel.startswith("src/") and not rules.under(rel, rules.NOT_CORE) and not rules.is_instantiation(rel)


def planted(case, plants):
    """A scratch tree of plants {path: text}, its root."""
    scratch = tempfile.TemporaryDirectory()
    case.addCleanup(scratch.cleanup)
    root = Path(scratch.name)
    for name, text in plants.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text, encoding="utf-8", newline="\n")
    return root


def subset_findings(root):
    found = []
    subset.check(root, rules.source_files(root), lambda *finding: found.append(finding), layers.of(root))
    return sorted((rule, where, kind, detail.split(":", 1)[0]) for rule, where, kind, detail, what in found
                  if not rules.allowed(rule, where, what))


class Core(unittest.TestCase):

    def test_kept(self):
        """Each file core logic held with src/ its root, it holds."""
        held = [rel for rel in files() if rel.endswith(CXX) and was_core(rel)]
        self.assertGreater(len(held), 100)
        self.assertEqual([rel for rel in held if not rules.is_core(rel, TREE)], [])

    def test_outside(self):
        """No file outside src/ is core logic today: the runner, devkit's,
        the vendored SQLite, system's, and test code are outside it."""
        self.assertEqual([rel for rel in files() if rel.endswith(CXX) and rules.is_core(rel, TREE)
                          and not was_core(rel)], [])
        runner = [rel for rel in files() if rel.startswith(rules.RUNNER) and rel.endswith(CXX)]
        self.assertGreaterEqual(len(runner), 4)
        self.assertEqual(TREE.layer(runner[0]), "devkit")
        self.assertEqual(TREE.layer("third_party/sqlite/sqlite3.c"), "system")
        for rel in (*runner, "third_party/sqlite/sqlite3.c", "third_party/sqlite/sqlite3.h", "tests/unit/arena_test.cpp"):
            with self.subTest(rel=rel):
                self.assertFalse(rules.is_core(rel, TREE))
        self.assertTrue(rules.is_core("src/memory/arena.cpp", TREE))
        self.assertEqual(TREE.layer("src/memory/arena.cpp"), "core")

    def test_test_code(self):
        """Test code, the table's: the top-level tests/ and a module's tests/."""
        self.assertTrue(TREE.test_code("tests/unit/arena_test.cpp"))
        self.assertTrue(TREE.test_code(rules.RUNNER + "tests/runner_test.cpp"))
        self.assertFalse(TREE.test_code(rules.RUNNER + "src/runner.cpp"))
        self.assertFalse(TREE.test_code("src/memory/arena.cpp"))

    def test_b9_libraries(self):
        """B9's own headers: a library's of core to products, as src/ was."""
        self.assertEqual([rel for rel in files() if TREE.library(rel) != rel.startswith("src/")
                          and rel.endswith(CXX)], [])


class Text(unittest.TestCase):

    def text_findings(self, plants):
        root = planted(self, plants)
        out = rules.Findings(layers.of(root))
        rules.source_rules(root, rules.source_files(root), out)
        return sorted(f.where for f in out.found if f.rule == "B1")

    def test_b1_walked(self):
        """B1's text in every tree tools/trees.json names; the vendored
        SQLite's module, the wrapper, the build's pin and the structure tests
        allowed by path; a file outside the trees not read."""
        said = "// sqlite3\n"
        self.assertEqual(self.text_findings({
            "foundation/core/memory/sqlite_name.hpp": said, "foundation/devkit/build/other.cmake": said,
            "tools/probe.py": said, "kits/ai/notes.md": said, "products/qiven3/x.cpp": said, "src/x/x.cpp": said,
            "tests/unit/x_test.cpp": said, "foundation/system/sqlite/README.md": said,
            "foundation/devkit/build/build.cmake": said, "src/store/engine/sqlite.cpp": said,
            "third_party/sqlite/sqlite3.h": said, "tests/structure/x.py": said, "README.md": said}),
            sorted(["foundation/core/memory/sqlite_name.hpp", "foundation/devkit/build/other.cmake", "tools/probe.py",
                    "kits/ai/notes.md", "products/qiven3/x.cpp", "src/x/x.cpp", "tests/unit/x_test.cpp"]))


RUNNER_TABLE = layers.module_file("runner", "devkit", [("runner", ["foundation/devkit/test/runner/"])])
PLANT_TABLE = layers.module_file("plant", "products", [("plant", ["src/"])])


class Subset(unittest.TestCase):

    def test_scope(self):
        """B19 to B31 over the libraries, the test kit whole and
        tests/support/: the runner taken in; the top-level tests/ beside
        tests/support/, and a vendor's, not."""
        for rel, held in ((rules.RUNNER + "src/runner.cpp", True),
                          (rules.RUNNER + "include/qiven/runner/core.hpp", True),
                          (rules.RUNNER + "tests/runner_test.cpp", True), ("src/memory/arena.hpp", True),
                          ("tests/support/goldens/goldens.hpp", True), ("tests/unit/arena_test.cpp", False),
                          ("tests/child/child.hpp", False), ("third_party/sqlite/sqlite3.h", False),
                          ("tools/git_shim.c", False)):
            with self.subTest(rel=rel):
                self.assertEqual(subset.in_scope(rel, TREE), held)

    def test_runner_planted(self):
        """In a planted tree with its own table: the runner's cases' two
        globals allowed by name, a third refused; Case's name allowed,
        another type's view refused; NOLINT and a #pragma warning disable
        refused in the runner; the same globals outside the runner's cases
        refused; outside the table's libraries, the kit and tests/support/,
        none of these read."""
        globals_ = "bool reached = false;\nvoid* held_slots = nullptr;\nint third = 0;\n"
        root = planted(self, {
            "foundation/devkit/test/runner/module.json": RUNNER_TABLE,
            "products/plant/module.json": PLANT_TABLE,
            subset.RUNNER_TEST: globals_,
            "foundation/devkit/test/runner/include/qiven/runner/core.hpp":
                "#include <string_view>\nstruct Case {\n    std::string_view name;\n};\n"
                "struct Other {\n    std::string_view name;\n};\n",
            "foundation/devkit/test/runner/src/runner.cpp":
                "// NOLINT(cppcoreguidelines-avoid-non-const-global-variables)\n"
                "#pragma warning(disable : 26426)\nint quiet() { return 0; }\n",
            "src/x/state.cpp": globals_,
            "tests/unit/state_test.cpp": globals_,
        })
        self.assertEqual(subset_findings(root), sorted([
            ("B25", "foundation/devkit/test/runner/tests/runner_test.cpp", "object", "line 3"),
            ("B27", "foundation/devkit/test/runner/src/runner.cpp", "nolint", "line 1"),
            ("B27", "foundation/devkit/test/runner/src/runner.cpp", "pragma", "line 2"),
            ("B28", "foundation/devkit/test/runner/include/qiven/runner/core.hpp", "", "line 6"),
            *(("B25", "src/x/state.cpp", "object", f"line {n}") for n in (1, 2, 3)),
        ]))

    def test_named_objects(self):
        """B25's finding names its object, which the allowance matches: the
        third global's name, not the two the allowance names."""
        found = []
        subset.object_rules(subset.lex("bool reached = false;\nint* volatile third{};\n")[0],
                            lambda rule, kind, line, detail, what=None: found.append((rule, line, what)))
        self.assertEqual(found, [("B25", 1, "reached"), ("B25", 2, "third")])


if __name__ == "__main__":
    unittest.main()
