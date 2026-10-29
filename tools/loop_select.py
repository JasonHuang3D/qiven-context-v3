"""The local loop's affected tests (tools/loop.py): the tests of a ctest
step, in tiers 1 and 2, that the change since the step last passed in its
tree affects, from the build graph, never from a list kept by hand.

- The record. Each tree keeps RECORD: for each ctest step that passed in
  it, each file of the checkout by its blob id as the job's run began (git's
  index, and git hash-object for a file changed or new), and each output of
  Ninja's log, .ninja_log, by its line as the step found it. A step that
  fails leaves its record as it was, so that its changes stay selected. A
  tree new or emptied, or a step with no record in it, runs every test;
  the step's line names why a tree was emptied, the toolchain's pins
  changed among them (tools/loop_run.py's own_tree): the record goes with
  the tree it describes.
- Every test, when a changed file is a build file (CMakeLists.txt, any
  *.cmake, CMakePresets.json), under tools/, a fixture or a vector
  (tests/fixtures/, tests/vectors/) or a workflow (.github/workflows/); and
  when a changed data file, neither C or C++, which the build graph
  reaches, nor a module a test imports, is named by no test's declaration
  but one of the whole checkout.
- A C++ case or a fuzz target, when the binary its command runs (ctest's
  own listing, --show-only=json-v1) was relinked: its line in .ninja_log
  differs from the record's.
- A Python test, when a file of the checkout it imports, transitively and
  its own module included (read by ast), changed; when a file or directory
  its class declares (ctest_reads, tests/runner/list_tests.py; the build
  writes them into tests/python_reads.txt) holds a changed file, or a
  binary it declares was relinked; and when it reaches the build (a module
  it imports names QIVEN3_BIN, QIVEN3_BUILD or QIVEN3_CTEST, as
  tests/drive/binaries.py does), declares no binary, and any binary of
  bin/ was relinked: a binary it runs is a file it reads at run time.
- Any other test runs.

The step runs the tests chosen by ctest's --tests-from-file, a file kept in
the tree beside the record; with none chosen it does not run. Its line
names the selection: how many of the step's tests, and why.

Loaded by runpy.run_path, given tools/loop_run.py's names.
"""

import ast
import json
import os
import subprocess
import tempfile
from pathlib import Path

RECORD = ".qiven3-loop-passed.json"
CHOSEN = ".qiven3-loop-tests-{}.txt"  # the step's slug
READS = "tests/python_reads.txt"
EVERY_NAMES = ("CMakeLists.txt", "CMakePresets.json")
EVERY_DIRS = ("tools/", "tests/fixtures/", "tests/vectors/", ".github/workflows/")
CODE = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".ipp")
REACH = ("QIVEN3_BIN", "QIVEN3_BUILD", "QIVEN3_CTEST")
SHOWN = 3  # the names a reason shows at most


def every_test(path):
    """Whether a change of this file runs every test."""
    name = path.rsplit("/", 1)[-1]
    return name in EVERY_NAMES or name.endswith(".cmake") or path.startswith(EVERY_DIRS)


def is_path(read):
    """A declared read names a path of the checkout, not a binary's target."""
    return "/" in read or "." in read


def covers(read, path):
    return read == "." or (path.startswith(read) if read.endswith("/") else path == read or
                           path.startswith(read + "/"))


def shown(names):
    names = sorted(names)
    more = f" and {len(names) - SHOWN} more" if len(names) > SHOWN else ""
    return ", ".join(names[:SHOWN]) + more


def blobs(root, git):
    """{path: blob id} of each file of the checkout, ignored ones apart, as
    its working tree holds it; None when git cannot tell. git runs under
    its own environment (tools/loop_run.py's git_env)."""
    if not git:
        return None
    try:
        with tempfile.TemporaryDirectory(prefix="qiven3-loop-") as tmp:
            return listed(root, git, git_env(tmp, *LOOP_WHO))
    except OSError:
        return None


def listed(root, git, env):
    """blobs() under git's own environment env."""
    def run(*argv, data=None):
        done = git_run(git, argv, cwd=root, env=env, data=data)  # tools/loop_git.py's door
        if done.returncode != 0:
            raise OSError(done.stderr.decode("utf-8", "replace").strip()[-200:])
        return done.stdout

    try:
        out = {}
        for entry in run("ls-files", "-s", "-z").split(b"\0"):
            if entry:
                meta, _, path = entry.partition(b"\t")
                out[path.decode("utf-8", "surrogateescape")] = meta.split()[1].decode("ascii")
        dirty = list(dict.fromkeys(p.decode("utf-8", "surrogateescape")
                                   for p in run("ls-files", "-z", "-m", "-o", "--exclude-standard").split(b"\0") if p))
        here = [p for p in dirty if (Path(root) / p).is_file() and "\n" not in p]
        for path in dirty:
            out.pop(path, None)
        if here:
            ids = run("hash-object", "--stdin-paths", data="\n".join(here).encode("utf-8", "surrogateescape") + b"\n")
            out.update(zip(here, ids.decode("ascii").split()))
        return out
    except (OSError, subprocess.SubprocessError, ValueError, IndexError):
        return None


def ninja_log(tree):
    """{output, relative to the tree: its last line's mtime and command
    hash} of the tree's .ninja_log; empty when there is none."""
    try:
        lines = (Path(tree) / ".ninja_log").read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return {}
    base = norm(Path(tree).resolve()) + "/"
    out = {}
    for line in lines:
        fields = line.split("\t")
        if len(fields) == 5 and not line.startswith("#"):
            output = norm(fields[3])
            out[output.removeprefix(base)] = f"{fields[2]} {fields[4]}"
    return out


def norm(path):
    return os.path.normcase(str(path)).replace("\\", "/")


class Imports:
    """The checkout's Python modules by ast: what each imports of the
    checkout, transitively, and whether that reaches the build."""

    def __init__(self, root):
        self.root = Path(root)
        self.direct, self.texts, self.closures = {}, {}, {}

    def text(self, rel):
        if rel not in self.texts:
            try:
                self.texts[rel] = (self.root / rel).read_bytes().decode("utf-8", "replace")
            except OSError:
                self.texts[rel] = ""
        return self.texts[rel]

    def files(self, parts):
        """The checkout's files importing the dotted name runs: each
        package's __init__.py, then the module."""
        out = []
        for i in range(1, len(parts) + 1):
            base = "/".join(parts[:i])
            out += [c for c in (base + "/__init__.py", base + ".py") if (self.root / c).is_file()]
        return out

    def imports(self, rel):
        if rel not in self.direct:
            found = []
            try:
                tree = ast.parse(self.text(rel))
            except (SyntaxError, ValueError):
                tree = ast.Module(body=[], type_ignores=[])
            package = rel.split("/")[:-1]
            for node in ast.walk(tree):
                if isinstance(node, ast.Import):
                    for alias in node.names:
                        found += self.files(alias.name.split("."))
                elif isinstance(node, ast.ImportFrom):
                    base = package[:len(package) - node.level + 1] if node.level else []
                    parts = base + (node.module.split(".") if node.module else [])
                    found += self.files(parts)
                    for alias in node.names:
                        found += self.files(parts + [alias.name])
            self.direct[rel] = list(dict.fromkeys(found))
        return self.direct[rel]

    def closure(self, rel):
        """The module, its packages' __init__.py, and every file of the
        checkout they import, transitively."""
        if rel not in self.closures:
            seen = set()
            todo = self.files(rel.removesuffix(".py").split("/"))
            while todo:
                path = todo.pop()
                if path not in seen:
                    seen.add(path)
                    todo += self.imports(path)
            self.closures[rel] = frozenset(seen)
        return self.closures[rel]

    def module_of(self, test_id):
        parts = test_id.split(".")
        for k in range(len(parts), 0, -1):
            rel = "/".join(parts[:k]) + ".py"
            if (self.root / rel).is_file():
                return rel
        return None

    def reaches(self, closure):
        return any(name in self.text(rel) for rel in closure for name in REACH)

    def every_imported(self):
        """Every file a test module of tests/ imports."""
        out = set()
        for path in sorted((self.root / "tests").rglob("test_*.py")):
            out |= self.closure(path.relative_to(self.root).as_posix())
        return out


def binary_of(command):
    """The file a test's command runs: after the watcher's --, or itself."""
    return command[command.index("--") + 1] if "--" in command else command[0] if command else None


def choose(tests, changed, relinked, outputs, reads, imports, tree):
    """The tests to run of the step's listing, tests [(name, command)]:
    (names, {reason: names of tests}), or (None, why) for every test.
    changed: the checkout's files changed; relinked: the outputs whose
    line changed; outputs: every output the log holds; reads: {test:
    declared reads}; tree: the tree's absolute path."""
    changed = set(changed)
    trigger = sorted(p for p in changed if every_test(p))
    if trigger:
        return None, f"{shown(trigger)} changed"
    narrow = [r for declared in reads.values() for r in declared if is_path(r) and r != "."]
    data = sorted(p for p in changed if not p.endswith(CODE) and not any(covers(r, p) for r in narrow))
    if data:
        imported = imports.every_imported()
        data = [p for p in data if not (p.endswith(".py") and p in imported)]
        if data:
            return None, f"{shown(data)} changed, which no test imports or declares"
    base = norm(tree) + "/"
    names = {o.rsplit("/", 1)[-1] for o in relinked if o.startswith("bin/")}
    stems = {n.split(".", 1)[0] for n in names}
    chosen, why = [], {}

    def pick(name, reason):
        chosen.append(name)
        why.setdefault(reason, []).append(name)

    for name, command in tests:
        if name.startswith("tests."):
            module = imports.module_of(name)
            if module is None:
                pick(name, "no module found")
                continue
            closure = imports.closure(module)
            declared = reads.get(name, ())
            binaries = [r for r in declared if not is_path(r)]
            if closure & changed:
                pick(name, "importing " + shown(closure & changed))
            elif any(covers(r, p) for r in declared if is_path(r) for p in changed):
                pick(name, "declaring " + shown(r for r in declared if is_path(r) and
                                                 any(covers(r, p) for p in changed)))
            elif set(binaries) & stems:
                pick(name, "declaring " + shown(set(binaries) & stems))
            elif not binaries and stems and imports.reaches(closure):
                pick(name, "reaching the build")
            continue
        binary = binary_of(command)
        output = norm(binary).removeprefix(base) if binary else None
        if output is None or output not in outputs:
            pick(name, "a command of no output of the build")
        elif output in relinked:
            pick(name, "relinked " + output.rsplit("/", 1)[-1].split(".", 1)[0])
    return chosen, why


class Selection:
    """One run's affected tests: root the checkout the steps run in, ctest
    the ctest its steps run, env their environment, git git's path."""

    def __init__(self, root, ctest, env, git):
        self.root, self.ctest, self.env, self.git = Path(root), ctest, env, git
        self.sources, self.logs, self.steps = None, {}, {}
        self.imports, self.emptied = None, {}

    def start(self, unit, emptied=None):
        """The checkout's files as the unit's run begins; emptied, {tree:
        why}, the trees emptied for it (tools/loop_run.py's own_tree)."""
        self.sources = blobs(self.root, self.git)
        self.logs, self.imports = {}, Imports(self.root)
        self.emptied = dict(emptied or {})
        unit.notes.clear()

    def listing(self, words):
        """[(name, command)] of the step's tests by ctest's listing; None
        when it fails."""
        try:
            done = subprocess.run([self.ctest, *words[1:], "--show-only=json-v1"], cwd=self.root, env=self.env,
                                  stdin=subprocess.DEVNULL, capture_output=True, timeout=300)
            data = json.loads(done.stdout)
            return [(t["name"], t.get("command") or []) for t in data.get("tests", [])]
        except (OSError, subprocess.SubprocessError, ValueError, KeyError, TypeError):
            return None

    def affected(self, unit, step, tree, words, narrow):
        """The arguments that narrow the step to its affected tests: [] for
        every test, None for none; the step's note in unit.notes."""
        key = "\t".join((step.name, *step.ci))
        self.steps[(unit.name, step.name)] = (tree, key)
        if tree not in self.logs:
            self.logs[tree] = ninja_log(self.root / tree)
        if not narrow:
            return []
        tests = self.listing(words)
        record = load(self.root / tree).get(key)
        if tests is None:
            unit.notes[step.name] = "every test: ctest's listing failed"
            return []
        total = len(tests)
        if self.sources is None:
            chosen, why = None, "git cannot list the checkout"
        elif record is None and tree in self.emptied:
            chosen, why = None, f"its tree emptied, {self.emptied[tree]}"
        elif record is None:
            chosen, why = None, "no passing run of the step in this tree"
        else:
            old, outputs = record.get("sources", {}), self.logs[tree]
            changed = [p for p in set(old) | set(self.sources) if old.get(p) != self.sources.get(p)]
            relinked = {o for o, line in outputs.items() if record.get("outputs", {}).get(o) != line}
            chosen, why = choose(tests, changed, relinked, outputs, declared(self.root / tree), self.imports,
                                 (self.root / tree).resolve())
        if chosen is None:
            unit.notes[step.name] = f"every test, {total} of {total}: {why}"
            return []
        reasons = "; ".join(f"{len(names)} {reason}" for reason, names in why.items())
        unit.notes[step.name] = f"{len(chosen)} of {total} tests" + (f": {reasons}" if reasons else
                                                                      ": nothing it runs changed")
        if not chosen:
            return None
        if len(chosen) == total:
            return []
        path = (self.root / tree / CHOSEN.format(slug(step.name))).resolve()
        path.write_text("".join(name + "\n" for name in chosen), encoding="utf-8")
        return ["--tests-from-file", str(path)]

    def passed(self, unit, step):
        """The step passed: its record in its tree, when the unit owns it."""
        tree, key = self.steps.get((unit.name, step.name), (None, None))
        if tree is None or tree not in unit.trees or self.sources is None:
            return
        if tree not in self.logs:
            self.logs[tree] = ninja_log(self.root / tree)
        records = load(self.root / tree)
        records[key] = {"sources": self.sources, "outputs": self.logs[tree]}
        path = self.root / tree / RECORD
        part = path.with_name(path.name + ".part")
        part.write_text(json.dumps(records, sort_keys=True), encoding="utf-8")
        os.replace(part, path)


def load(tree):
    """The tree's records, {step: record}; empty when it has none."""
    try:
        data = json.loads((Path(tree) / RECORD).read_bytes())
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def declared(tree):
    """{Python test: its declared reads} the build listed in the tree."""
    try:
        lines = (Path(tree) / READS).read_text(encoding="utf-8").splitlines()
    except OSError:
        return {}
    return {name: tuple(r for r in reads.split(",") if r)
            for name, _, reads in (line.partition("\t") for line in lines) if name}
