"""The structure rules' scopes by the tree's table (layers.py), with no
build's records: core logic (B17) by layer, held in every file of src/ it
held when src/ was its literal root, never in devkit's runner, layer 0, nor
in the vendored SQLite, layer 2's, nor in test code; B1's text over every
tree tools/trees.json names but tools/ and a vendor's, allowed by exact path;
B9's own headers a library's;
the safe subset over the libraries, the test kit and tests/support/, its
test code the table's; and over a planted tree with its own table, B25's
two globals of the runner's cases allowed by name and a third refused,
B28's Case allowed and another type refused, B27 refused in the runner,
and B1's text refused in a core module's directory; each allowance's reach."""

import tempfile
import unittest
from pathlib import Path

from foundation.devkit.structure import layers, rules, subset

TREE = layers.of(rules.ROOT)
CXX = layers.CXX


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

    def test_moved(self):
        """A file moved into a module's own tests/ keeps its checks: a case
        meets B9 as in tests/unit/, a shared test file stays in the subset,
        and a tests/ of no module's directory is a library's still."""
        def rules_on(by, rel):
            out = rules.Findings(TREE)
            rules.direct_include(by, rel, "v3", rel, out)
            return sorted(f.rule for f in out.found)

        runner = f"{TREE.directory('runner')}include/qiven/runner/runner.hpp"
        moved = "foundation/core/memory/tests/"
        for header in (runner, f"{layers.SUPPORT}x.hpp", f"{moved}support/x.hpp", f"{layers.CHILD}x.hpp"):
            self.assertEqual(rules_on("tests/unit/x_test.cpp", header), [], header)
            self.assertEqual(rules_on(f"{moved}x_test.cpp", header), [], header)
        self.assertEqual(rules_on("foundation/core/memory/src/x.cpp", runner), ["B9"])
        self.assertEqual(rules_on(f"{moved}x_test.cpp", "tests/unit/x.hpp"),
                         rules_on("tests/unit/x_test.cpp", "tests/unit/x.hpp"))
        self.assertTrue(subset.in_scope(f"{moved}support/x.hpp", TREE))
        self.assertFalse(subset.in_scope(f"{moved}x_test.cpp", TREE))
        self.assertTrue(TREE.library("src/memory/tests/x.cpp"))


class Text(unittest.TestCase):

    def text_findings(self, plants):
        root = planted(self, plants)
        out = rules.Findings(layers.of(root))
        rules.source_rules(root, rules.source_files(root), out)
        return sorted(f.where for f in out.found if f.rule == "B1")

    def test_b1_layers(self):
        """B1's text over the layers' code and test code, a tool's test among
        it; each allowance by exact path, the rest of its directory refused;
        tools/, a vendor's and a file outside the walked trees not read."""
        said = "// sqlite3\n"
        self.assertEqual(self.text_findings({
            "foundation/core/memory/sqlite_name.hpp": said, "foundation/devkit/build/other.cmake": said,
            "kits/ai/notes.md": said, "products/qiven3/x.cpp": said, "src/x/x.cpp": said,
            "tests/unit/x_test.cpp": said, "tests/drive/test_loop_x.py": said,
            "foundation/system/sqlite/tests/x_test.cpp": said, "foundation/system/sqlite/notes.md": said,
            "tests/audit/read_audit_more.cpp": said, "foundation/system/sqlite/README.md": said,
            "foundation/system/sqlite/module.json": "{\"contract\": \"sqlite3.h\"}\n",
            "foundation/devkit/build/build.cmake": said, "tests/audit/read_audit.cpp": said,
            "src/store/engine/sqlite.cpp": said, "third_party/sqlite/sqlite3.h": said, "third_party/other/x.c": said,
            "tools/probe.py": said, "tools/status.py": said, "foundation/devkit/structure/x.py": said,
            "README.md": said}),
            sorted(["foundation/core/memory/sqlite_name.hpp", "foundation/devkit/build/other.cmake",
                    "kits/ai/notes.md", "products/qiven3/x.cpp", "src/x/x.cpp", "tests/unit/x_test.cpp",
                    "tests/drive/test_loop_x.py", "foundation/system/sqlite/tests/x_test.cpp",
                    "foundation/system/sqlite/notes.md", "tests/audit/read_audit_more.cpp"]))

    def test_b1_reasons(self):
        """Each of B1's text allowances a reason, and the text's what B1's include never takes;
        each directory exempt from a rule, B1's foundation/devkit/structure/ among them, a reason in the data too."""
        text = [key for key in rules.STATED if key[0] == "B1"]
        self.assertEqual(len(text), 4)
        for key in text:
            with self.subTest(key=key):
                self.assertTrue(rules.STATED[key])
                self.assertFalse(rules.allowed("B1", key[1]))
        self.assertEqual(list(rules.EXEMPT["B1"]), [rules.STRUCTURE])
        for rule, exempt in rules.EXEMPT.items():
            for where, reason in exempt.items():
                with self.subTest(rule=rule, where=where):
                    self.assertTrue(where.endswith("/") and isinstance(reason, str) and reason)


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

    def test_b27_analysed(self):
        """B27 over every file the analysis reads, beyond the subset's, each
        form refused; not in test code the analysis does not read."""
        forms = ("// NOLINTBEGIN\n#pragma warning(disable : 26426)\n#pragma warning(push, 0)\n"
                 "#pragma warning(push,0)\n#pragma clang diagnostic ignored \"-Wunused\"\n"
                 "__pragma(warning(push, 0))\n[[gsl::suppress(\"r.11\")]] int a;\n")
        kinds = ["nolint", "pragma", "pragma", "pragma", "pragma", "pragma", "suppress"]
        files = ("foundation/devkit/test/runner/src/runner.cpp", "src/x/x.cpp", "tests/child/child.cpp",
                 "tests/support/s.hpp")
        root = planted(self, {name: forms for name in (*files, "tests/unit/x_test.cpp", "third_party/v/v.c")})
        self.assertEqual(sorted((where, kind, detail) for rule, where, kind, detail in subset_findings(root)
                                if rule == "B27"),
                         sorted((name, kind, f"line {line}") for name in files
                                for line, kind in enumerate(kinds, 1)))

    def test_b27_quiet_pragmas(self):
        """A pragma that turns nothing off is no finding: warning's push and
        pop, clang's diagnostic push."""
        root = planted(self, {"src/x/x.cpp": "#pragma warning(push)\n#pragma warning(pop)\n"
                                             "#pragma clang diagnostic push\n#pragma once\n"})
        self.assertEqual(subset_findings(root), [])

    def test_named_objects(self):
        """B25's finding names its object, which the allowance matches: the
        third global's name, not the two the allowance names."""
        found = []
        subset.object_rules(subset.lex("bool reached = false;\nint* volatile third{};\n")[0],
                            lambda rule, kind, line, detail, what=None: found.append((rule, line, what)))
        self.assertEqual(found, [("B25", 1, "reached"), ("B25", 2, "third")])

    def objects(self, text):
        """The object rules' findings over text, as (rule, kind, what)."""
        found = []
        subset.object_rules(subset.lex(text)[0],
                            lambda rule, kind, line, detail, what=None: found.append((rule, kind, what)))
        return found

    def test_declarators(self):
        """Each declarator a finding of its own, whatever its initializer,
        its constness its own."""
        for text, names in (("bool planted_a, reached = false;\n", ["planted_a", "reached"]),
                            ("char planted_t[2], held_slots;\n", ["planted_t", "held_slots"]),
                            ("bool reached = false, planted = true;\n", ["reached", "planted"]),
                            ("bool reached{false}, planted{true};\n", ["reached", "planted"]),
                            ("bool reached = {false}, planted;\n", ["reached", "planted"]),
                            ("int held[N] = {1, 2}, planted = 3, more{4};\n", ["held", "planted", "more"]),
                            ("bool reached = probe(1, 2), planted;\n", ["reached", "planted"]),
                            ("bool reached = 1 < 2, planted = 3 > 2;\n", ["reached", "planted"]),
                            ("bool reached, planted(true);\n", ["reached", "planted"]),
                            ("std::map<int, int> first, second;\n", ["first", "second"]),
                            ("namespace { bool reached, planted; }\n", ["reached", "planted"]),
                            ("struct S { int a; } first, second;\n", ["first", "second"]),
                            ("int const first = 0, second = 0;\n", []), ("const int first = 0, second{1};\n", []),
                            ("const char* first = nullptr, * const second = nullptr;\n", ["first"]),
                            ("char* const first = nullptr, second = 0;\n", ["second"]),
                            ("int first{};\nint second{};\n", ["first", "second"]),
                            ("int function(int a = 0) { return a; }\nint later;\n", ["later"])):
            with self.subTest(text=text):
                self.assertEqual(self.objects(text), [("B25", "object", name) for name in names])

    def test_allowances_exact(self):
        """In a planted tree: a further global in an allowed declaration, a
        static member of an allowed name and a second member of Case refused,
        each form on its own line; Case's name and a borrowing type's members
        allowed."""
        root = planted(self, {
            "foundation/devkit/test/runner/module.json": RUNNER_TABLE,
            "products/plant/module.json": PLANT_TABLE,
            subset.RUNNER_TEST: "bool planted_a, reached = false;\nchar planted_t[2], held_slots;\n"
                                "bool reached{false}, planted_b{true};\n"
                                "struct PlantB {\n    static int held_slots;\n};\n",
            "foundation/devkit/test/runner/include/qiven/runner/core.hpp":
                "#include <string_view>\nstruct Case {\n    std::string_view name;\n    std::string_view planted_d;\n"
                "    std::string_view planted_e, name;\n};\nstruct Other {\n    std::string_view name;\n};\n",
            "src/platform/platform.hpp":
                "#include <string_view>\nstruct Command {\n    std::string_view program, cwd;\n};\n",
        })
        runner, core = subset.RUNNER_TEST, "foundation/devkit/test/runner/include/qiven/runner/core.hpp"
        self.assertEqual(subset_findings(root), sorted([
            *(("B25", runner, "object", f"line {n}") for n in (1, 2, 3)), ("B25", runner, "static", "line 5"),
            *(("B28", core, "", f"line {n}") for n in (4, 5, 8)),
        ]))


if __name__ == "__main__":
    unittest.main()
