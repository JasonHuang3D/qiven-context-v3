"""K15: in the link graph's modules, json's header (qiven/text/json.hpp,
and json_bytes.hpp behind it) is included by text's and log's files
alone, and log names no scan: core reaches the format's bytes through
canon, and log writes its lines through json::encode alone. The files read:
the C++ files of the trees tools/trees.json names, each of a
module of the link graph by the tree's table (foundation/devkit/structure/layers.py),
neither under the top-level tests/ nor a vendor's. Each include is read
from the lexer's tokens and taken as resolved, as the build resolves it
(foundation/devkit/structure/modules.py's resolve): a quoted name from the including file's
directory first, then the include roots, an angle name from the roots
alone; so an include is the header's by the file it reaches, however it is
spelled. Over v3's tree."""

import itertools
import unittest
from pathlib import Path

from foundation.devkit.structure.lexer import includes, lex
from foundation.devkit.structure import layers, modules, rules

HEADERS = ("qiven/text/json.hpp", "qiven/text/json_bytes.hpp")
# The modules whose files may include them, by the table's modules: the log's, and text's, which holds
# json and canon.
LOG, TEXT = "log", "text"


def read(rel, tree):
    """Whether K15 reads rel: a file of a link graph module's, not under the top-level tests/, not a vendor's."""
    return tree.layer(rel) in layers.LINKED and not rel.startswith(layers.TESTS) and rel not in layers.VENDORED


def allowed(rel, tree):
    return tree.module(rel) in (LOG, TEXT)


def findings(root):
    """(file, line, what) for each include that resolves to one of HEADERS
    outside the allowed files, each scan named in log, over the files K15
    reads (read()), and each of HEADERS the include roots do not reach."""
    out, tree = [], layers.of(root)
    files = sorted({p.relative_to(root).as_posix() for top in layers.TREES for p in Path(root, top).rglob("*")
                    if p.is_file()})
    present, roots = set(files), modules.include_roots(tree.trees, files)
    own = {path: name for name in HEADERS
           if (path := modules.resolve(tree.trees, "", name, present, roots, angle=True))}
    out += [(name, 0, "no such header") for name in HEADERS if name not in own.values()]
    for rel in files:
        if not rel.endswith(layers.CXX) or not read(rel, tree):
            continue
        tokens, _ = lex(Path(root, rel).read_text(encoding="utf-8"))
        for line, group in itertools.groupby(tokens, key=lambda token: token[2]):
            for name in includes(list(group)):
                path = modules.resolve(tree.trees, rel, name[1:], present, roots, angle=name[0] == "<")
                if path in own and not allowed(rel, tree):
                    out.append((rel, line, "include " + own[path]))
        if tree.module(rel) == LOG:
            out += [(rel, line, "scan") for kind, value, line in tokens if kind == "id" and value == "scan"]
    return sorted(out)


class Tree(unittest.TestCase):

    def test_k15(self):
        self.assertEqual(findings(rules.ROOT), [])


if __name__ == "__main__":
    unittest.main()
