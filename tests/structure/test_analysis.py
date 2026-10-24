"""The analysis tools' controls: the tools' halves of B19 to B31, and B26
whole. Each rule plants one file in a temporary directory, which clang-tidy,
by the tree's .clang-tidy, and MSVC's /analyze, by the analysis build's own
options and rule set, must each report under the names its row gives; a
clean file passes both. Each tool compiles the plant by the analysis
build's command for a source of src/, so that dropping a check from
.clang-tidy or analysis.ruleset fails its rule's case.

Run on the analysis build (QIVEN3_BUILD) after it is built, from the
toolset's environment, which both tools read the system's headers from:
ctest --preset analysis. On any other build each case is skipped. B18, B20,
B24, B27 and B29 to B31 have no tool's half: their tokens are
test_boundaries'. clang-tidy's C-style cast check reports a cast between
unrelated types, not a value's conversion ((int)d), which C26493 reports;
clang's own dangling warnings are the clang presets', and clang-tidy reports
B28's plant as bugprone-dangling-handle, as this build's tools ran them.
"""

import json
import os
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEMPLATE = "src/memory/arena.cpp"

# Each rule's plants, each a file, and the names each tool must report:
# clang-tidy's checks, MSVC's warnings. Under /WX, /analyze stops at a
# file's first finding (C2220), so each of MSVC's names has a file of its own.
# B20 has none: neither tool reports placement new, std::construct_at or
# std::destroy_at; C26409 is plain new's and delete's, as this build's tools ran them.
PLANTS = {
    "B19": (("int* make() { return new int(1); }\n"
             "void drop(int* p) { delete p; }\n",
             ("cppcoreguidelines-owning-memory",), ("C26409",)),
            ("#include <cstdlib>\n"
             "void* take() { return std::malloc(8); }\n"
             "void give(void* p) { std::free(p); }\n",
             ("cppcoreguidelines-no-malloc",), ("C26408",))),
    "B21": (("long bits(const double* d) { return *reinterpret_cast<const long*>(d); }\n",
             ("cppcoreguidelines-pro-type-reinterpret-cast",), ("C26490",)),
            ("int* unconst(const int* p) { return const_cast<int*>(p); }\n",
             ("cppcoreguidelines-pro-type-const-cast",), ("C26492",)),
            ("long* bits(double* d) { return (long*)d; }\n",
             ("cppcoreguidelines-pro-type-cstyle-cast",), ("C26493",))),
    "B22": (("#include <cstdio>\n"
             "int show(int n) { return std::printf(\"%d\", n); }\n",
             ("cppcoreguidelines-pro-type-vararg",), ()),),
    "B23": (("union Bits { float f; unsigned u; };\n"
             "unsigned read(Bits b) { return b.u; }\n",
             ("cppcoreguidelines-pro-type-union-access",), ()),),
    "B25": (("int counter = 0;\n"
             "int next() { return ++counter; }\n",
             ("cppcoreguidelines-avoid-non-const-global-variables",), ()),),
    "B26": (("int second(const int* p) { return *(p + 1); }\n",
             ("cppcoreguidelines-pro-bounds-pointer-arithmetic",), ("C26481",)),),
    "B28": (("#include <string>\n#include <string_view>\n"
             "std::string_view name() { std::string s = \"text\"; return s; }\n",
             ("bugprone-dangling-handle",), ("C26816",)),),
}
CLEAN = ("#include <span>\n"
         "int total(std::span<const int> values) {\n"
         "    int sum = 0;\n"
         "    for (const int v : values) {\n"
         "        sum += v;\n"
         "    }\n"
         "    return sum;\n"
         "}\n")


def build_directory():
    value = os.environ.get("QIVEN3_BUILD")
    return Path(value) if value else None


def cache(build):
    found = {}
    path = build / "CMakeCache.txt"
    if path.is_file():
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            name, sep, value = line.partition("=")
            if sep and ":" in name and not line.startswith(("#", "//")):
                found[name.split(":", 1)[0]] = value
    return found


def is_analysis(build):
    return build is not None and cache(build).get("QIVEN3_ANALYZE") == "analysis"


ANALYSIS = unittest.skipUnless(is_analysis(build_directory()), "not the analysis build (QIVEN3_BUILD)")


def template(database):
    """The analysis build's command for TEMPLATE, as its arguments."""
    for entry in json.loads(database.read_text(encoding="utf-8")):
        if Path(entry["file"]).resolve() == (ROOT / TEMPLATE).resolve():
            return shlex.split(entry["command"], posix=False)
    raise AssertionError(f"{database} has no command for {TEMPLATE}: build the analysis preset")


def command(args, source, directory):
    """The template's arguments compiling source into directory."""
    out = []
    for arg in args[:-1]:
        if arg.startswith("/Fo"):
            arg = "/Fo" + str(directory / "plant.obj")
        elif arg.startswith("/Fd"):
            arg = "/Fd" + str(directory / "plant.pdb")
        out.append(arg)
    return out + [str(source)]


def run(args, cwd):
    ran = subprocess.run(args, cwd=cwd, capture_output=True, check=False)
    return ran.returncode, (ran.stdout + ran.stderr).decode("utf-8", "replace")


class Tools:
    """The two tools over one planted file."""

    def __init__(self, directory, text):
        self.build = build_directory()
        self.directory = directory
        self.source = directory / "plant.cpp"
        self.source.write_text(text, encoding="utf-8")
        if not os.environ.get("INCLUDE"):
            raise AssertionError("no INCLUDE: run the analysis tests from the toolset's environment")

    def tidy(self):
        args = command(template(self.build / "tidy" / "compile_commands.json"), self.source, self.directory)
        entry = {"directory": str(self.directory), "file": str(self.source), "command": subprocess.list2cmdline(
            [a.strip('"') for a in args])}
        (self.directory / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
        tidy = cache(self.build).get("QIVEN3_CLANG_TIDY")
        if not tidy:
            raise AssertionError("the analysis build names no QIVEN3_CLANG_TIDY")
        return run([tidy, "-p", str(self.directory), "--config-file=" + str(ROOT / ".clang-tidy"), "--quiet",
                    str(self.source)], self.directory)

    def analyze(self):
        args = command(template(self.build / "compile_commands.json"), self.source, self.directory)
        return run([a.strip('"') for a in args], self.directory)


def reported(self, status, output, names, tool):
    self.assertNotEqual(status, 0, f"{tool} passed the plant:\n{output}")
    for name in names:
        with self.subTest(tool=tool, name=name):
            self.assertIn(name, output, f"{tool} did not report {name}:\n{output}")


def control(self, rule):
    for index, (text, tidy_names, msvc_names) in enumerate(PLANTS[rule]):
        with tempfile.TemporaryDirectory() as tmp, self.subTest(plant=index):
            tools = Tools(Path(tmp), text)
            if tidy_names:
                reported(self, *tools.tidy(), tidy_names, "clang-tidy")
            if msvc_names:
                reported(self, *tools.analyze(), msvc_names, "/analyze")


@ANALYSIS
class Control(unittest.TestCase):
    """Each rule's plant reported by each tool its row names."""
    ctest_reads = (".clang-tidy", "analysis.ruleset", "src/")

    def test_clean(self):
        with tempfile.TemporaryDirectory() as tmp:
            tools = Tools(Path(tmp), CLEAN)
            for tool, (status, output) in (("clang-tidy", tools.tidy()), ("/analyze", tools.analyze())):
                with self.subTest(tool=tool):
                    self.assertEqual(status, 0, output)


for _rule in PLANTS:
    setattr(Control, "test_" + _rule.lower(), lambda self, rule=_rule: control(self, rule))
