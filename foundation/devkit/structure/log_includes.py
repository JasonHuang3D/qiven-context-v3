"""K15: in the link graph's modules, json's header (qiven/text/json.hpp,
and json_bytes.hpp behind it) is included by text's and log's files
alone, and log names no scan: core reaches the format's bytes through
canon, and log writes its lines through json::encode alone. The files read:
the C++ files of a set, each of a module of the link graph by the tree's
table (layers.py), neither under the top-level tests/ nor a vendor's. Each
include is read from the lexer's tokens and taken as resolved, as the build
resolves it (modules.py's resolve) against the whole listing: a quoted name
from the including file's directory first, then the include roots, an angle
name from the roots alone; so an include is the header's by the file it
reaches, however it is spelled. findings() applies it; checks.py, to each
module's files of v3's tree."""

import itertools

from foundation.devkit.structure.lexer import includes, lex
from foundation.devkit.structure import layers, modules

HEADERS = ("qiven/text/json.hpp", "qiven/text/json_bytes.hpp")
# The modules whose files may include them, by the table's modules: the log's, and text's, which holds
# json and canon.
LOG, TEXT = "log", "text"


def read(rel, tree):
    """Whether K15 reads rel: a file of a link graph module's, not under the top-level tests/, not a vendor's."""
    return tree.layer(rel) in layers.LINKED and not rel.startswith(layers.TESTS) and rel not in layers.VENDORED


def allowed(rel, tree):
    return tree.module(rel) in (LOG, TEXT)


def findings(root, listing, files, tree):
    """(file, line, what) for each include of files that resolves to one of
    HEADERS outside the allowed files, each scan named in log, over the
    files K15 reads (read()); and, where files hold one of text's, each of
    HEADERS the include roots do not reach. listing is the tree's every
    file, which the includes resolve against; tree its table (layers.Layers)."""
    out = []
    present, roots = set(listing), modules.include_roots(tree.trees, listing)
    own = {path: name for name in HEADERS
           if (path := modules.resolve(tree.trees, "", name, present, roots, angle=True))}
    if any(tree.module(rel) == TEXT for rel in files):
        out += [(name, 0, "no such header") for name in HEADERS if name not in own.values()]
    for rel in files:
        if not rel.endswith(layers.CXX) or not read(rel, tree):
            continue
        tokens, _ = lex((root / rel).read_text(encoding="utf-8"))
        for line, group in itertools.groupby(tokens, key=lambda token: token[2]):
            for name in includes(list(group)):
                path = modules.resolve(tree.trees, rel, name[1:], present, roots, angle=name[0] == "<")
                if path in own and not allowed(rel, tree):
                    out.append((rel, line, "include " + own[path]))
        if tree.module(rel) == LOG:
            out += [(rel, line, "scan") for kind, value, line in tokens if kind == "id" and value == "scan"]
    return sorted(out)
