"""K15: in the link graph's modules, json's header (qiven3/json.hpp, and
json_bytes.hpp behind it) is included by canon, log and json's own files
alone, and log names no scan: core reaches the format's bytes through
canon, and log writes its lines through json::encode alone. The files read:
the .hpp and .cpp files of the trees tools/trees.json names, each of a
module of the link graph by the tree's table (tests/structure/layers.py),
neither under the top-level tests/ nor a vendor's. Over v3's tree, and over
a planted tree with its own table: an include in another module, one under
a layer tree and one in devkit's runner among them, and a scan in log each
found, and the allowed includes, a scan in a comment and one in a string,
and an include in the top-level tests/ passing."""

import re
import tempfile
import unittest
from pathlib import Path

from tests.structure.lexer import lex
from tests.structure import layers, rules

HEADERS = ("qiven3/json.hpp", "qiven3/json_bytes.hpp")
# The files that may include them, by the table's modules: the log's, and canon's and json's own files in
# the base module, which holds the format.
LOG = "log"
BASE, OWN = "base", ("canon.", "json.", "json_bytes.")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def read(rel, tree):
    """Whether K15 reads rel: a file of a link graph module's, not under the top-level tests/, not a vendor's."""
    return tree.layer(rel) in layers.LINKED and not rel.startswith((layers.TESTS, layers.VENDORED))


def allowed(rel, tree):
    module = tree.module(rel)
    return module == LOG or (module == BASE and rel.rsplit("/", 1)[-1].startswith(OWN))


def findings(root):
    """(file, line, what) for each include of HEADERS outside the allowed
    files, and each scan named in log, over the files K15 reads (read())."""
    out, tree = [], layers.of(root)
    for path in sorted(p for top in layers.TREES for p in Path(root, top).rglob("*")):
        if path.suffix not in (".hpp", ".cpp") or not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if not read(rel, tree):
            continue
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), 1):
            match = INCLUDE.match(line)
            if match and match.group(1) in HEADERS and not allowed(rel, tree):
                out.append((rel, number, "include " + match.group(1)))
        if tree.module(rel) == LOG:
            tokens, _ = lex(text)
            out += [(rel, line, "scan") for kind, value, line in tokens if kind == "id" and value == "scan"]
    return sorted(out)


class Tree(unittest.TestCase):

    def test_k15(self):
        self.assertEqual(findings(rules.ROOT), [])


class Plant(unittest.TestCase):
    def test_k15_caught(self):
        plants = {
            "src/store/store.cpp": '#include "memory/memory.hpp"\n#include "qiven3/json.hpp"\n',
            "src/diag/report.hpp": '#  include <qiven3/json_bytes.hpp>\n',
            "src/log/reader.cpp": '#include "log/log.hpp"\nvoid f() { json::scan(text, 16, v); }\n',
            "src/qiven3/canon.cpp": '#include "qiven3/json.hpp"\n',
            "src/log/log.cpp": ('#include "qiven3/json.hpp"\n// json::scan stays out\n'
                                'const char* s = "scan";\n'),
            "src/qiven3/json.cpp": '#include "qiven3/json.hpp"\n',
            "src/store/notes.txt": '#include "qiven3/json.hpp"\n',
            "foundation/core/codec/src/codec.cpp": '#include <qiven3/json.hpp>\n',
            "foundation/devkit/test/runner/src/runner.cpp": '#include "qiven3/json.hpp"\n',
            "tests/unit/json_test.cpp": '#include "qiven3/json.hpp"\n',
            "tools/probe.cpp": '#include "qiven3/json.hpp"\n',
            # The tree's table: each module's directory and its rows' paths.
            "foundation/services/log/module.json": layers.module_file("log", "services", [("log", ["src/log/"])]),
            "foundation/core/text/module.json": layers.module_file(
                "text", "core", [("base", ["src/qiven3/", "tests/unit/json_test.cpp"])]),
            "foundation/services/store/module.json": layers.module_file("store", "services",
                                                                        [("store", ["src/store/"])]),
            "foundation/system/diag/module.json": layers.module_file("diag", "system", [("diag", ["src/diag/"])]),
            "foundation/core/codec/module.json": layers.module_file("codec", "core",
                                                                    [("codec", ["foundation/core/codec/"])]),
            "foundation/devkit/test/runner/module.json": layers.module_file(
                "runner", "devkit", [("runner", ["foundation/devkit/test/runner/"])]),
        }
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name, text in plants.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text, encoding="utf-8")
            self.assertEqual(findings(root), [
                ("foundation/core/codec/src/codec.cpp", 1, "include qiven3/json.hpp"),
                ("foundation/devkit/test/runner/src/runner.cpp", 1, "include qiven3/json.hpp"),
                ("src/diag/report.hpp", 1, "include qiven3/json_bytes.hpp"),
                ("src/log/reader.cpp", 2, "scan"),
                ("src/store/store.cpp", 2, "include qiven3/json.hpp"),
            ])


if __name__ == "__main__":
    unittest.main()
