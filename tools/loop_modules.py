"""The modules' check: tools/modules.json kept true of the checkout.

    py -3.15 -I tools/loop_modules.py SOURCE [BUILD]

Each tracked file in one module; each module's dependencies, followed
transitively, covering its includes (each #include "..." resolved to the
tracked file it names) and, given a configured build tree, its links (read
from CMake's file API, the codemodel the root CMakeLists.txt asks for); a
foundation module depending on a product module refused, but the table's
listed exceptions; a module or a dependency the table does not name, or a
layer it does not know, refused. One line an error, naming the file or the
edge; exit 0 when none, 1 when any, 2 when the check could not run.
"""

import fnmatch
import json
import re
import sys
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools import loop_git  # noqa: E402

TABLE = Path(__file__).resolve().parent / "modules.json"
LAYERS = ("foundation", "product", "tooling", "support", "whole-build", "no-test")
SOURCES = (".c", ".cpp", ".h", ".hpp")
INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
ROOTS = ("src", "tests", "third_party/sqlite")  # the include directories the build gives


def load(path=TABLE):
    """The table: {"modules": [...], "exceptions": [...]}."""
    return json.loads(Path(path).read_text(encoding="utf-8"))


def holds(pattern, path):
    """Whether a table's path holds a file: a directory's prefix, or a pattern."""
    return path.startswith(pattern) if pattern.endswith("/") else fnmatch.fnmatchcase(path, pattern)


def owners(table, files):
    """{file: [the names of the modules whose paths hold it]}."""
    return {f: [m["name"] for m in table["modules"] if any(holds(p, f) for p in m["paths"])] for f in files}


def reach(table):
    """{module: every module its dependencies reach, transitively, itself not included}."""
    depends = {m["name"]: set(m["depends"]) for m in table["modules"]}
    closure = {}
    for name in depends:
        seen, stack = set(), list(depends[name])
        while stack:
            other = stack.pop()
            if other not in seen and other in depends:
                seen.add(other)
                stack.extend(depends[other])
        closure[name] = seen - {name}
    return closure


def excepted(table, source, target):
    return any(e["from"] == source and target in e["to"] for e in table.get("exceptions", ()))


def check_table(table):
    """The errors of the table alone: names, layers, the foundation line."""
    errors = []
    layer = {m["name"]: m["layer"] for m in table["modules"]}
    for m in table["modules"]:
        if m["layer"] not in LAYERS:
            errors.append(f"module {m['name']}: layer {m['layer']} is none of {', '.join(LAYERS)}")
        for other in m["depends"]:
            if other not in layer:
                errors.append(f"edge {m['name']} -> {other}: no module {other}")
            elif m["layer"] == "foundation" and layer[other] == "product" and not excepted(table, m["name"], other):
                errors.append(f"edge {m['name']} -> {other}: foundation depending on product")
    return errors


def resolve(including, name, tracked):
    """The tracked file an #include "name" of the file including names, or None."""
    for base in (str(Path(including).parent.as_posix()), *ROOTS):
        candidate = Path(base, name).as_posix()
        if candidate in tracked:
            return candidate
    return None


def check(table, files, read, targets=None):
    """Every error: the table's, each file's ownership, each include edge and,
    with targets [(name, [tracked sources], [linked target names])], each
    link edge. read(path) gives a tracked file's bytes."""
    errors = check_table(table)
    owner = owners(table, files)
    for f, names in sorted(owner.items()):
        if not names:
            errors.append(f"file {f}: in no module")
        elif len(names) > 1:
            errors.append(f"file {f}: in modules {', '.join(names)}")
    module = {f: names[0] for f, names in owner.items() if len(names) == 1}
    closure = reach(table)

    shared = {m["name"]: set(m["depends"]) for m in table["modules"] if m["layer"] == "support"}

    def covered(source, target):
        """The shared support is every module's, and reaches its own
        dependencies alone, not theirs."""
        if source in shared:
            return source == target or target in shared[source]
        return source == target or target in closure.get(source, ()) or target in shared

    tracked = set(files)
    for f in sorted(files):
        if not f.endswith(SOURCES) or f not in module:
            continue
        for name in INCLUDE.findall(read(f)):
            found = resolve(f, name.decode("utf-8", "replace"), tracked)
            if found in module and not covered(module[f], module[found]):
                errors.append(f"include {f} -> {found}: module {module[f]} does not depend on {module[found]}")
    of_target = {}
    for name, sources, _ in targets or ():
        mods = sorted({module[s] for s in sources if s in module})
        if len(mods) > 1:
            errors.append(f"target {name}: sources in modules {', '.join(mods)}")
        elif mods:
            of_target[name] = mods[0]
    for name, _, links in targets or ():
        for other in links:
            if name in of_target and other in of_target and not covered(of_target[name], of_target[other]):
                errors.append(f"link {name} -> {other}: module {of_target[name]} does not depend on {of_target[other]}")
    return errors


def codemodel(build, source):
    """[(target, [its tracked sources, relative to source], [the targets it
    links or depends on])] of a configured build tree's codemodel reply."""
    reply = Path(build) / ".cmake" / "api" / "v1" / "reply"
    indices = sorted(reply.glob("index-*.json"))
    if not indices:
        raise FileNotFoundError(f"{reply}: no codemodel reply; configure the tree first")
    index = json.loads(indices[-1].read_text(encoding="utf-8"))
    model = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text(encoding="utf-8"))
    loaded = [json.loads((reply / ref["jsonFile"]).read_text(encoding="utf-8"))
              for ref in model["configurations"][0]["targets"]]
    names = {t["id"]: t["name"] for t in loaded}
    root = Path(source).resolve()
    rows = []
    for t in loaded:
        sources = []
        for s in t.get("sources", ()):
            path = Path(s["path"])
            if not s.get("isGenerated") and not path.is_absolute():
                sources.append(path.as_posix())
            elif not s.get("isGenerated") and path.resolve().is_relative_to(root):
                sources.append(path.resolve().relative_to(root).as_posix())
        rows.append((t["name"], sources, [names[d["id"]] for d in t.get("dependencies", ())]))
    return rows


def tracked(source):
    """The checkout's tracked files, by the loop's git door."""
    git = loop_git.which()
    if git is None:
        raise FileNotFoundError("no git")
    done = loop_git.run(git, ["ls-files", "-z"], cwd=source, check=True)
    return [p for p in done.stdout.decode("utf-8").split("\0") if p]


def run(source, build=None, table_path=None):
    """The check's errors on a checkout and, given one, its build tree."""
    source = Path(source)
    table = load(table_path or source / "tools" / "modules.json")
    files = tracked(source)
    targets = codemodel(build, source) if build else None
    return check(table, files, lambda f: (source / f).read_bytes(), targets)


def main(argv):
    if len(argv) not in (1, 2):
        print("usage: loop_modules.py SOURCE [BUILD]", file=sys.stderr)
        return 2
    try:
        errors = run(*argv)
    except (OSError, ValueError, KeyError) as error:
        print(f"modules: the check could not run: {error}", file=sys.stderr)
        return 2
    for line in errors:
        print(f"modules: {line}")
    print(f"modules: {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
