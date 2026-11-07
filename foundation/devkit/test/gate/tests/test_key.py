"""key: a test's key and the code id over planted Files. A key wrong in silence
leaves a changed test out, its pass carried, or never lets a red end."""

import unittest

from foundation.devkit.test.gate import key

PY = "foundation/devkit/a/tests/test_one.py"
FILES = key.Files(
    modules={"a": {"foundation/devkit/a/one.py": "1" * 40, "foundation/devkit/a/README.md": "2" * 40,
                   PY: "3" * 40, "foundation/devkit/a/tests/test_two.py": "4" * 40},
             "b": {"foundation/devkit/b/two.cpp": "5" * 40},
             "structure": {"foundation/devkit/structure/rules.py": "6" * 40}},
    unowned={"loose.txt": "7" * 40, "NOTES.md": "8" * 40})
PY_TEST = "foundation.devkit.a.tests.test_one.Case.test_it"


def changed(path, module, value="f" * 40):
    """FILES with path's digest in module set to value, or added."""
    modules = {m: dict(held) for m, held in FILES.modules.items()}
    unowned = dict(FILES.unowned)
    (modules[module] if module else unowned)[path] = value
    return key.Files(modules, unowned)


class Key(unittest.TestCase):
    def test_modules(self):
        """Its modules are its labels that name one; a check test's M enters whole."""
        self.assertEqual(key.modules_of("t", ["fast", "b", "a"], FILES.modules), (["a", "b"], frozenset()))
        self.assertEqual(key.modules_of("rules.a", ["a", "structure"], FILES.modules),
                         (["a", "structure"], frozenset({"a"})))
        with self.assertRaises(key.NoModule):
            key.modules_of("t", ["cxx", "fuzz"], FILES.modules)

    def test_parts(self):
        """The configuration and its module's files enter; another module's,
        a document and another Python test file do not; its own file does."""
        base = key.test_key("ci-windows", "t", ["a"], FILES)
        self.assertEqual(len(base), 64)
        self.assertNotEqual(key.test_key("ci-asan", "t", ["a"], FILES), base)
        self.assertNotEqual(key.test_key("ci-windows", "t", ["a"], changed("foundation/devkit/a/one.py", "a")), base)
        self.assertNotEqual(key.test_key("ci-windows", "t", ["a"], changed("foundation/devkit/a/new.cmake", "a")),
                            base)
        for path, module in (("foundation/devkit/b/two.cpp", "b"), ("foundation/devkit/a/README.md", "a"),
                             (PY, "a"), ("loose.txt", None)):
            with self.subTest(path=path):
                self.assertEqual(key.test_key("ci-windows", "t", ["a"], changed(path, module)), base)
        own = key.test_key("ci-windows", PY_TEST, ["a"], FILES)
        self.assertNotEqual(key.test_key("ci-windows", PY_TEST, ["a"], changed(PY, "a")), own)
        self.assertEqual(key.test_key("ci-windows", PY_TEST, ["a"],
                                      changed("foundation/devkit/a/tests/test_two.py", "a")), own)

    def test_check_whole(self):
        """A check test's module enters with its documents and test files; the
        rules' module without them."""
        base = key.test_key("ci-windows", "rules.a", ["a", "structure"], FILES)
        for path, module in (("foundation/devkit/a/README.md", "a"), (PY, "a"),
                             ("foundation/devkit/structure/rules.py", "structure")):
            with self.subTest(path=path):
                self.assertNotEqual(key.test_key("ci-windows", "rules.a", ["a", "structure"], changed(path, module)),
                                    base)
        self.assertEqual(key.test_key("ci-windows", "rules.a", ["a", "structure"],
                                      changed("foundation/devkit/structure/README.md", "structure")), base)

    def test_own_file(self):
        """A Python test's own file by its dotted path, the longest that matches."""
        paths = [PY, "foundation/devkit/a/tests/test_one_more.py", "foundation/devkit/a/one.py"]
        self.assertEqual(key.own_file(PY_TEST, paths), PY)
        self.assertIsNone(key.own_file("a.Case.test_it", paths))

    def test_code_id(self):
        """Every file but the documents, owned or not, enters the code id."""
        base = key.code_id(FILES)
        self.assertEqual(len(base), 64)
        self.assertNotEqual(key.code_id(changed("loose.txt", None)), base)
        self.assertNotEqual(key.code_id(changed(PY, "a")), base)
        self.assertEqual(key.code_id(changed("NOTES.md", None)), base)
        self.assertEqual(key.code_id(changed("foundation/devkit/a/README.md", "a")), base)
