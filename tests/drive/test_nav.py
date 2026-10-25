"""The navigation aid, tools/nav.py, over clangd.

Nav: on a scratch checkout of its own, a small project and its compilation
database where the analysis preset's would be: a method called seven times
resolves to its definition with its seven references; a call through a
template parameter, and one in a concept, are listed under unresolved,
a comment and a literal not; a name's callers past --limit, and a header's
declarations past the envelope's bound, are cut with their full count,
the whole answer under detail_ref; a type on a line; the refusals. Then
the background index over the scratch, built in a few seconds: the
references from it, the template's call unresolved, and a file changed
after it reported as changed; the clangd that built it recorded, and its
record planted with another clangd's version, then with none: stale,
refused without --file and absent with it. Skipped where clangd is absent.

Checkout: the reviewer's facts on this checkout, from the analysis
preset's stripped database, without the index: the call at
tests/unit/files_test.cpp's first.publish resolves to Staged::publish's
definition with its seven references, and system_->publish in
src/platform/files.hpp, a call through a template parameter, is listed
under unresolved; the lines found by grep, not written here. Skipped where
clangd or the database is absent; labelled drive alone, so that it runs in
the full suite."""

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import nav_lsp

ROOT = Path(__file__).resolve().parents[2]
NAV = ROOT / "tools" / "nav.py"
DATABASE = "build/windows-analysis/tidy/compile_commands.json"
BOUND = 8192
TIMEOUT = 600
CALLERS = 12
HEADER = """#pragma once

namespace mini {

template <class S>
concept System = requires(S& s) { s.publish(1); };

template <System S>
class Staged {
public:
    explicit Staged(S* system) : system_(system) {}
    void publish(int name) {
        // publish(name) in a comment is no use
        system_->publish(name);
    }

private:
    S* system_;
};

int tick(int step);

}  // namespace mini
"""
FAKE = "#pragma once\n\nnamespace mini {\n\nstruct Fake {\n    int publish(int name) { return name; }\n};\n\n}\n"
TICK = '#include "mini/files.hpp"\n\nnamespace mini {\n\nint tick(int step) { return step + 1; }\n\n}\n'
USE = """#include "mini/fake.hpp"
#include "mini/files.hpp"

namespace {

void one() {
    mini::Fake fake;
    mini::Staged<mini::Fake> first(&fake);
    first.publish(1);
    first.publish(2);
    first.publish(3);
    const char* text = "first.publish(4)";
    (void)text;
}

void two(mini::Staged<mini::Fake>& second) {
    second.publish(5);
    second.publish(6);
    second.publish(7);
    second.publish(8);
}

""" + "".join(f"int caller_{i}() {{ return mini::tick({i}); }}\n" for i in range(CALLERS)) + "\n}\n"
FILES = {"src/mini/files.hpp": HEADER, "src/mini/fake.hpp": FAKE, "src/mini/tick.cpp": TICK,
         "tests/mini/use.cpp": USE, ".gitignore": "build/\n",
         "src/mini/long.hpp": "#pragma once\n" + "".join(
             f"int declaration_with_a_long_name_to_pass_the_bound_{i}(int first_argument, int second_argument);\n"
             for i in range(300))}


def clangd_or_skip(case):
    try:
        nav_lsp.find_clangd()
    except nav_lsp.Refusal as e:
        case.skipTest(f"no clangd: {e}")


def nav(*argv, root=None):
    """The tool's exit status and envelope, with its text's length."""
    extra = ["--root", str(root)] if root else []
    done = subprocess.run([sys.executable, "-I", str(NAV), *argv, *extra], capture_output=True, timeout=TIMEOUT,
                          stdin=subprocess.DEVNULL, cwd=str(ROOT), check=False)
    text = done.stdout.decode("utf-8")
    lines = text.splitlines()
    if done.returncode == 2:
        return 2, None, done.stderr.decode("utf-8", "replace")
    assert len(lines) == 1, text[-2000:] + done.stderr.decode("utf-8", "replace")[-2000:]
    return done.returncode, json.loads(lines[0]), len(lines[0].encode("utf-8"))


def line_of(text, needle, start=0):
    """The 1-based line of the first line holding needle, from start."""
    return next(n for n, line in enumerate(text.split("\n"), 1) if n > start and needle in line)


def git(root, *argv):
    subprocess.run(["git", *argv], cwd=str(root), check=True, capture_output=True, timeout=120)


def scratch(case):
    """The scratch checkout: the project, its database, one commit."""
    top = Path(tempfile.mkdtemp(prefix="qiven3-nav-")).resolve()
    case.addCleanup(shutil.rmtree, top, ignore_errors=True)
    for rel, text in FILES.items():
        (top / rel).parent.mkdir(parents=True, exist_ok=True)
        (top / rel).write_bytes(text.encode("utf-8"))
    commands = [{"directory": str(top), "file": str(top / rel),
                 "arguments": ["clang++", "-std=c++20", "-I" + str(top / "src"), "-c", str(top / rel)]}
                for rel in ("src/mini/tick.cpp", "tests/mini/use.cpp")]
    (top / DATABASE).parent.mkdir(parents=True)
    (top / DATABASE).write_text(json.dumps(commands, indent=1), encoding="utf-8")
    git(top, "init", "-q")
    git(top, "add", "-A")
    git(top, "-c", "user.name=nav", "-c", "user.email=nav@example.invalid", "commit", "-q", "-m", "scratch")
    return top


def symbol(envelope, name):
    found = [s for s in envelope["data"]["symbols"]["items"] if s["name"] == name]
    assert len(found) == 1, envelope
    return found[0]


def unresolved_at(envelope):
    return [u["at"] for u in envelope["data"]["unresolved"]["lines"]["items"]]


class Nav(unittest.TestCase):
    ctest_labels = ("fake",)
    ctest_reads = ("tools/nav.py", "tools/nav_query.py")

    def setUp(self):
        clangd_or_skip(self)
        self.top = scratch(self)

    def publish_lines(self):
        return [n for n, line in enumerate(USE.split("\n"), 1) if re.search(r"(first|second)\.publish\(\d\)", line)
                and '"' not in line]

    def test_references(self):
        status, envelope, size = nav("refs", "Staged::publish", "--file", "tests/mini/use.cpp", root=self.top)
        self.assertEqual((status, envelope["status"], envelope["code"]), (0, "ok", "OK"), envelope)
        found = symbol(envelope, "mini::Staged::publish")
        self.assertEqual(found["definition"], f"src/mini/files.hpp:{line_of(HEADER, 'void publish(')}")
        lines = [f"tests/mini/use.cpp:{n}" for n in self.publish_lines()]
        self.assertEqual(len(lines), 7)
        self.assertEqual(found["references"], {"count": 7, "shown": 7, "items": lines})
        self.assertEqual(envelope["data"]["unresolved"]["lines"]["count"], 0, envelope)
        self.assertEqual(envelope["data"]["index"], {"present": False, "build": "nav.py index (heavy)"})
        self.assertLessEqual(size, BOUND)
        self.assertRegex(envelope["operation_id"], r"^op:[0-9a-f]{8}$")

    def test_unresolved(self):
        status, envelope, _ = nav("callers", "publish", "--file", "src/mini/files.hpp", root=self.top)
        self.assertEqual(status, 0, envelope)
        through = f"src/mini/files.hpp:{line_of(HEADER, 'system_->publish(')}"
        concept = f"src/mini/files.hpp:{line_of(HEADER, 's.publish(1)')}"
        self.assertEqual(sorted(unresolved_at(envelope)), sorted([through, concept]))
        item = next(u for u in envelope["data"]["unresolved"]["lines"]["items"] if u["at"] == through)
        self.assertEqual((item["use"], item["text"]), ("member call", "system_->publish(name);"))
        self.assertIn("template parameter", envelope["data"]["unresolved"]["label"])
        self.assertEqual(symbol(envelope, "mini::Staged::publish")["callers"]["count"], 0)

    def test_truncated(self):
        status, envelope, _ = nav("callers", "tick", "--file", "tests/mini/use.cpp", "--limit", "5", root=self.top)
        self.assertEqual(status, 0, envelope)
        found = symbol(envelope, "mini::tick")
        self.assertEqual(found["references"], CALLERS)
        callers = found["callers"]
        self.assertEqual((callers["count"], callers["shown"], len(callers["items"])), (CALLERS, 5, 5))
        self.assertEqual(callers["items"][0], {"caller": "caller_0", "file": "tests/mini/use.cpp",
                                               "lines": [line_of(USE, "caller_0()")]})
        status, envelope, size = nav("api", "src/mini/long.hpp", "--limit", "200", root=self.top)
        self.assertEqual(status, 0, envelope)
        declarations = envelope["data"]["declarations"]
        self.assertEqual(declarations["count"], 300)
        self.assertLess(declarations["shown"], 200)
        self.assertEqual(len(declarations["items"]), declarations["shown"])
        self.assertLessEqual(size, BOUND)
        self.assertEqual(declarations["items"][0], "2: int declaration_with_a_long_name_to_pass_the_bound_0"
                                                   "(int first_argument, int second_argument)")
        whole = json.loads((self.top / envelope["detail_ref"]).read_text(encoding="utf-8"))
        self.assertEqual(len(whole["data"]["declarations"]["items"]), 300)

    def test_type_and_refusals(self):
        line = line_of(USE, "first.publish(1)")
        status, envelope, _ = nav("type", f"tests/mini/use.cpp:{line}", "first", root=self.top)
        self.assertEqual(status, 0, envelope)
        self.assertIn("Staged<", envelope["data"]["type"])
        self.assertEqual(envelope["data"]["at"], f"tests/mini/use.cpp:{line}:5")
        status, envelope, _ = nav("type", f"tests/mini/use.cpp:{line}", "absent", root=self.top)
        self.assertEqual((status, envelope["code"]), (1, "NOT_ON_LINE"))
        status, envelope, _ = nav("refs", "publish", root=self.top)
        self.assertEqual((status, envelope["status"], envelope["code"]), (1, "not_ready", "NO_INDEX"))
        status, envelope, _ = nav("refs", "nothing_named_so", "--file", "tests/mini/use.cpp", root=self.top)
        self.assertEqual((status, envelope["status"], envelope["code"]), (1, "missing", "NO_SYMBOL"))
        self.assertEqual(nav("refs", "a b", "--file", "x", root=self.top)[0], 2)
        (self.top / DATABASE).unlink()
        status, envelope, _ = nav("refs", "publish", "--file", "tests/mini/use.cpp", root=self.top)
        self.assertEqual((status, envelope["code"]), (1, "DATABASE_ABSENT"))

    def test_index_and_stale(self):
        status, envelope, _ = nav("index", root=self.top)
        self.assertEqual(status, 0, envelope)
        commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(self.top), capture_output=True, text=True,
                                check=True).stdout.strip()
        self.assertEqual(envelope["data"]["index"]["commit"], commit)
        self.assertGreaterEqual(envelope["data"]["shards"], 2, envelope)
        version = nav_lsp.clangd_version(nav_lsp.find_clangd())
        self.assertRegex(version or "", r"^\d+\.\d+\.\d+$")
        self.assertEqual(envelope["data"]["index"]["clangd"], version)
        status, envelope, _ = nav("refs", "publish", root=self.top)
        self.assertEqual(status, 0, envelope)
        index = envelope["data"]["index"]
        self.assertEqual((index["present"], index["commit"], index["loaded"], index["clangd"]),
                         (True, commit, True, version), index)
        self.assertEqual(index["changed"]["count"], 0)
        found = symbol(envelope, "mini::Staged::publish")
        self.assertEqual(found["references"]["items"], [f"tests/mini/use.cpp:{n}" for n in self.publish_lines()])
        self.assertEqual(unresolved_at(envelope), [f"src/mini/files.hpp:{line_of(HEADER, text)}"
                                                   for text in ("s.publish(1)", "system_->publish(")])
        status, envelope, _ = nav("def", "Fake::publish", root=self.top)
        self.assertEqual((status, envelope["data"]["symbols"]["count"]), (0, 1), envelope)
        self.assertEqual(symbol(envelope, "mini::Fake::publish")["definition"],
                         f"src/mini/fake.hpp:{line_of(FAKE, 'int publish(')}")
        with open(self.top / "src/mini/tick.cpp", "a", encoding="utf-8") as f:
            f.write("// changed after the index\n")
        status, envelope, _ = nav("def", "tick", root=self.top)
        self.assertEqual(status, 0, envelope)
        self.assertEqual(envelope["data"]["index"]["changed"], {"count": 1, "shown": 1, "items": ["src/mini/tick.cpp"]})
        self.assertEqual(symbol(envelope, "mini::tick")["definition"], f"src/mini/tick.cpp:{line_of(TICK, 'int tick')}")
        self.other_clangd(version)

    def other_clangd(self, version):
        """The record planted with another clangd's version, then with none:
        the index stale, refused without --file, absent with it."""
        path = self.top / "build" / "nav" / "index.json"
        record = json.loads(path.read_text(encoding="utf-8"))
        for planted, named in (("1.0.0", "1.0.0"), (None, "(none named)")):
            record["clangd"] = planted
            path.write_text(json.dumps(record), encoding="utf-8")
            build = f"stale: built by clangd {named}, the call runs clangd {version}: nav.py index (heavy)"
            status, envelope, _ = nav("refs", "publish", root=self.top)
            self.assertEqual((status, envelope["code"]), (1, "NO_INDEX"), envelope)
            self.assertTrue(envelope["data"]["message"].endswith(f"; the index is {build}"), envelope)
            status, envelope, _ = nav("refs", "publish", "--file", "tests/mini/use.cpp", root=self.top)
            self.assertEqual(status, 0, envelope)
            self.assertEqual(envelope["data"]["index"], {"present": False, "build": build})


class Checkout(unittest.TestCase):
    ctest_labels = ("fake",)
    ctest_reads = ("tools/nav.py", "tools/nav_query.py", "src/platform/files.hpp", "tests/unit/files_test.cpp")

    def setUp(self):
        clangd_or_skip(self)
        if not (ROOT / DATABASE).is_file():
            self.skipTest(f"no stripped database: {DATABASE}, the analysis preset's, is not built")

    def test_publish(self):
        header = (ROOT / "src/platform/files.hpp").read_text(encoding="utf-8")
        test = (ROOT / "tests/unit/files_test.cpp").read_text(encoding="utf-8")
        calls = [n for n, line in enumerate(test.split("\n"), 1) if re.search(r"[\w)]\.publish\(\"", line)]
        self.assertEqual(len(calls), 8)
        status, envelope, _ = nav("refs", "publish", "--file", "tests/unit/files_test.cpp", "--no-index")
        self.assertEqual(status, 0, envelope)
        found = symbol(envelope, "qiven3::platform::files::Staged::publish")
        self.assertEqual(found["definition"], f"src/platform/files.hpp:{line_of(header, 'void publish(std::')}")
        self.assertEqual(found["references"]["items"], [f"tests/unit/files_test.cpp:{n}" for n in calls])
        status, envelope, _ = nav("refs", "Staged::publish", "--file", "src/platform/files.hpp", "--no-index")
        self.assertEqual(status, 0, envelope)
        self.assertIn(f"src/platform/files.hpp:{line_of(header, 'system_->publish(')}", unresolved_at(envelope))


if __name__ == "__main__":
    unittest.main()
