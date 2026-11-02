"""K15: in the link graph's modules, json's header (qiven3/json.hpp, and
json_bytes.hpp behind it) is included by canon, log and json's own files
alone, and log names no scan: core reaches the format's bytes through
canon, and log writes its lines through json::encode alone. The files read:
the C++ files of the trees tools/trees.json names, each of a
module of the link graph by the tree's table (foundation/devkit/structure/layers.py),
neither under the top-level tests/ nor a vendor's. Each include is read
from the lexer's tokens and taken as resolved, as the build resolves it
(tools/loop_modules.py's resolve): a quoted name from the including file's
directory first, then the include roots, an angle name from the roots
alone; so an include is the header's by the file it reaches, however it is
spelled. Over v3's tree, and over a planted tree with its own table: an
include in another module, one under a layer tree and one in devkit's
runner among them, the header's three spellings from a file beside it, and
a scan in log each found, and the allowed includes, a same-named header
elsewhere, an include in a comment, a scan in a comment and one in a
string, and an include in the top-level tests/ passing."""

import itertools
import tempfile
import unittest
from pathlib import Path

from foundation.devkit.structure.lexer import includes, lex
from foundation.devkit.structure import layers, rules

HEADERS = ("qiven3/json.hpp", "qiven3/json_bytes.hpp")
# The files that may include them, by the table's modules: the log's, and canon's and json's own files in
# the base module, which holds the format.
LOG = "log"
BASE, OWN = "base", ("canon.", "json.", "json_bytes.")
RESOLVE, INCLUDE_ROOTS = layers.MODULES["resolve"], layers.MODULES["include_roots"]


def read(rel, tree):
    """Whether K15 reads rel: a file of a link graph module's, not under the top-level tests/, not a vendor's."""
    return tree.layer(rel) in layers.LINKED and not rel.startswith((layers.TESTS, layers.VENDORED))


def allowed(rel, tree):
    module = tree.module(rel)
    return module == LOG or (module == BASE and rel.rsplit("/", 1)[-1].startswith(OWN))


def findings(root):
    """(file, line, what) for each include that resolves to one of HEADERS
    outside the allowed files, each scan named in log, over the files K15
    reads (read()), and each of HEADERS the include roots do not reach."""
    out, tree = [], layers.of(root)
    files = sorted({p.relative_to(root).as_posix() for top in layers.TREES for p in Path(root, top).rglob("*")
                    if p.is_file()})
    present, roots = set(files), INCLUDE_ROOTS(files)
    own = {path: name for name in HEADERS if (path := RESOLVE("", name, present, roots, angle=True))}
    out += [(name, 0, "no such header") for name in HEADERS if name not in own.values()]
    for rel in files:
        if not rel.endswith(layers.CXX) or not read(rel, tree):
            continue
        tokens, _ = lex(Path(root, rel).read_text(encoding="utf-8"))
        for line, group in itertools.groupby(tokens, key=lambda token: token[2]):
            for name in includes(list(group)):
                path = RESOLVE(rel, name[1:], present, roots, angle=name[0] == "<")
                if path in own and not allowed(rel, tree):
                    out.append((rel, line, "include " + own[path]))
        if tree.module(rel) == LOG:
            out += [(rel, line, "scan") for kind, value, line in tokens if kind == "id" and value == "scan"]
    return sorted(out)


# A planted tree's table and the headers: each module's directory and its rows' paths.
TABLE = {
    "src/qiven3/json.hpp": "#pragma once\n",
    "src/qiven3/json_bytes.hpp": "#pragma once\n",
    "foundation/services/log/module.json": layers.module_file("log", "services", [("log", ["src/log/"])]),
    "foundation/core/text/module.json": layers.module_file(
        "text", "core", [("base", ["src/qiven3/", "tests/unit/json_test.cpp"])]),
    "foundation/services/store/module.json": layers.module_file("store", "services", [("store", ["src/store/"])]),
    "foundation/system/diag/module.json": layers.module_file("diag", "system", [("diag", ["src/diag/"])]),
    "foundation/core/codec/module.json": layers.module_file("codec", "core",
                                                            [("codec", ["foundation/core/codec/"])]),
    "foundation/devkit/test/runner/module.json": layers.module_file(
        "runner", "devkit", [("runner", ["foundation/devkit/test/runner/"])]),
}


def planted(plants):
    """K15's findings over a scratch tree of TABLE and plants."""
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        for name, text in {**TABLE, **plants}.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_text(text, encoding="utf-8")
        return findings(root)


class Tree(unittest.TestCase):

    def test_k15(self):
        self.assertEqual(findings(rules.ROOT), [])


class Plant(unittest.TestCase):
    def test_k15_caught(self):
        self.assertEqual(planted({
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
        }), [
            ("foundation/core/codec/src/codec.cpp", 1, "include qiven3/json.hpp"),
            ("foundation/devkit/test/runner/src/runner.cpp", 1, "include qiven3/json.hpp"),
            ("src/diag/report.hpp", 1, "include qiven3/json_bytes.hpp"),
            ("src/log/reader.cpp", 2, "scan"),
            ("src/store/store.cpp", 2, "include qiven3/json.hpp"),
        ])

    def test_k15_every_suffix(self):
        """A file of every C++ suffix read; a .txt, in the plant above, left."""
        for suffix in layers.CXX:
            self.assertEqual(planted({f"src/store/x{suffix}": '#include "qiven3/json.hpp"\n'}),
                             [(f"src/store/x{suffix}", 1, "include qiven3/json.hpp")])

    def test_k15_resolved(self):
        """The header's three spellings beside it and a relative one from
        another directory each found; a same-named
        header elsewhere, an include in a comment and one the roots do not
        reach passing; a tree without the headers found."""
        self.assertEqual(planted({
            "src/qiven3/location.cpp": ('#include "json.hpp"\n#include "../qiven3/json.hpp"\n'
                                        '#include "qiven3/json.hpp"\n#include <json_bytes.hpp>\n'),
            "src/store/json.hpp": "#pragma once\n",
            "src/store/cache.cpp": ('#include "json.hpp"\n/*\n#include "qiven3/json.hpp"\n*/\n'
                                    '#include "src/qiven3/json.hpp"\n#include <../qiven3/json.hpp>\n'
                                    '#include "../qiven3/json_bytes.hpp"\n'),
        }), [
            ("src/qiven3/location.cpp", 1, "include qiven3/json.hpp"),
            ("src/qiven3/location.cpp", 2, "include qiven3/json.hpp"),
            ("src/qiven3/location.cpp", 3, "include qiven3/json.hpp"),
            ("src/store/cache.cpp", 7, "include qiven3/json_bytes.hpp"),
        ])
        with tempfile.TemporaryDirectory() as scratch:
            self.assertEqual(findings(scratch), [(name, 0, "no such header") for name in HEADERS])


if __name__ == "__main__":
    unittest.main()
