"""K15: in src/, json's header (qiven3/json.hpp, and json_bytes.hpp behind
it) is included by canon, log and json's own files alone, and log names no
scan: core reaches the format's bytes through canon, and log writes its
lines through json::encode alone. Over v3's tree, and over a planted tree:
an include in another module and a scan in log each found, and the allowed
includes, a scan in a comment and one in a string passing."""

import re
import tempfile
import unittest
from pathlib import Path

from tests.structure.lexer import lex
from tests.structure import rules

HEADERS = ("qiven3/json.hpp", "qiven3/json_bytes.hpp")
# The files that may include them: canon's, log's and json's own.
ALLOWED = ("src/qiven3/canon.", "src/log/", "src/qiven3/json.", "src/qiven3/json_bytes.")
LOG = "src/log/"
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')


def findings(root):
    """(file, line, what) for each include of HEADERS outside ALLOWED, and
    each scan named in log, over the .hpp and .cpp files of root's src/."""
    out = []
    src = Path(root, "src")
    for path in sorted(src.rglob("*")):
        if path.suffix not in (".hpp", ".cpp") or not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        text = path.read_text(encoding="utf-8")
        for number, line in enumerate(text.splitlines(), 1):
            match = INCLUDE.match(line)
            if match and match.group(1) in HEADERS and not rel.startswith(ALLOWED):
                out.append((rel, number, "include " + match.group(1)))
        if rel.startswith(LOG):
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
        }
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            for name, text in plants.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text, encoding="utf-8")
            self.assertEqual(findings(root), [
                ("src/diag/report.hpp", 1, "include qiven3/json_bytes.hpp"),
                ("src/log/reader.cpp", 2, "scan"),
                ("src/store/store.cpp", 2, "include qiven3/json.hpp"),
            ])


if __name__ == "__main__":
    unittest.main()
