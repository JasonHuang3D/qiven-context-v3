"""The modules' check: the modules' table kept true of the checkout.

    py -3.15 -I tools/loop_modules.py SOURCE [BUILD]
    py -3.15 -I tools/loop_modules.py --help

The table is the tree: its files are module.json at the root,
tools/module.json and each module.json under foundation/, kits/ and
products/, read in sorted path order. Its rows: each built module's present
rows, its own README.md and module.json (the root's module.json alone) added
to its first row, or to the first row its from names when it has none; a
slot's directory a row of the no-test layer, and a group's README.md and
module.json one. A table file that does not parse, lacks or adds a key for
its status, or names a layer or a status this check does not know is
refused; so are two table files or two rows of one name, a from naming no
row, a tracked file in a slot's directory, or directly in a group's, but
its README.md and module.json.

Each tracked file in one module; each module's dependencies, followed
transitively, covering its includes (each #include "..." resolved to the
tracked file it names, one resolving to none refused but in third_party/,
where they are counted on one line) and, given a configured build tree,
its links (read from CMake's file API, the codemodel the root CMakeLists.txt
asks for); a foundation module depending on a product module refused, but
the table's listed exceptions; a module or a dependency the table does not
name, or a layer it does not know, refused. One line an error, naming the
file or the edge, then the count of vendored includes left unresolved; exit
0 when none, 1 when any, 2 when the check could not run.
"""

import fnmatch
import json
import posixpath
import re
import runpy
import sys
from pathlib import Path

# git's one door (tools/loop_git.py), by runpy: under -I this directory is off sys.path.
GIT = runpy.run_path(str(Path(__file__).resolve().with_name("loop_git.py")))

ROOT = Path(__file__).resolve().parents[1]  # the checkout this file is in
TOPS = ("foundation", "kits", "products")  # the trees whose every module.json is a table file
FILE_LAYERS = ("devkit", "core", "system", "services", "kits", "products", "foundation", "tools", "repository")
KEYS = {  # a table file's keys by its status; a built one with no present rows has from, a slot may have of
    "built": ("name", "layer", "status", "platforms", "adopted", "parameters", "present"),
    "slot": ("name", "layer", "status", "kind", "may_depend", "platforms", "adopted", "parameters"),
    "group": ("name", "layer", "status", "adopted"),
}
LAYERS = ("foundation", "product", "tooling", "support", "whole-build", "no-test")
SOURCES = (".c", ".cpp", ".h", ".hpp")
INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
ROOTS = ("src", "tests", "third_party/sqlite")  # the include directories the build gives
VENDORED = "third_party/"  # a vendor's source: its unresolved includes counted, not refused


def is_table_file(path):
    """Whether a checkout's path, from its root, is one of the table's files."""
    return path in ("module.json", "tools/module.json") or (
        path.endswith("/module.json") and path.split("/", 1)[0] in TOPS)


def table_files(root):
    """The table's files under a checkout's root, from it, sorted."""
    root = Path(root)
    found = [p.relative_to(root).as_posix() for top in TOPS if (root / top).is_dir()
             for p in (root / top).rglob("module.json") if p.is_file()]
    return sorted(found + [p for p in ("module.json", "tools/module.json") if (root / p).is_file()])


def table_of(texts, files=None):
    """The table, {"modules": [rows]}, and its errors, from the table's
    files {path: text}; given the tracked files, what a slot's or a group's
    directory holds checked too."""
    errors, rows, names, later, dirs = [], [], {}, [], []
    for path in sorted(texts):
        where = path.rpartition("/")[0]
        own = [f"{where}/README.md", f"{where}/module.json"] if where else [path]
        try:
            data = json.loads(texts[path])
            status = data.get("status")
        except (ValueError, AttributeError) as error:
            errors.append(f"table file {path}: does not parse: {error}")
            continue
        if status not in KEYS:
            errors.append(f"table file {path}: status {status} is none of {', '.join(KEYS)}")
            continue
        want = set(KEYS[status]) | ({"from"} if status == "built" and data.get("present") == [] else set())
        lacks, adds = want - set(data), set(data) - want - ({"of"} if status == "slot" else set())
        if lacks or adds:
            errors.append(f"table file {path}: " + "; ".join(
                f"{how} {', '.join(sorted(keys))}" for how, keys in (("lacks", lacks), ("adds", adds)) if keys))
            continue
        if status == "built" and any(not isinstance(row, dict) or set(row) != {"name", "layer", "depends", "paths"}
                                     for row in data["present"]):
            errors.append(f"table file {path}: a present row lacks or adds a key of name, layer, depends, paths")
            continue
        if data["layer"] not in FILE_LAYERS:
            errors.append(f"table file {path}: layer {data['layer']} is none of {', '.join(FILE_LAYERS)}")
        if data["name"] in names:
            errors.append(f"table file {path}: name {data['name']} is {names[data['name']]}'s too")
        names.setdefault(data["name"], path)
        if status == "built":
            mine = [dict(row, paths=list(row["paths"])) for row in data["present"]]
            if mine:
                mine[0]["paths"] += own
            else:
                later.append((path, data["from"], own))
            rows += [(path, row) for row in mine]
        else:
            paths = [where + "/"] if status == "slot" else own
            rows.append((path, {"name": data["name"], "layer": "no-test", "depends": [], "paths": paths}))
            dirs.append((status, data["name"], where, own))
    named = {}
    for path, row in rows:
        if row["name"] in named:
            errors.append(f"row {row['name']}: in {named[row['name']][0]} and {path}")
        named.setdefault(row["name"], (path, row))
    for path, names_from, own in later:
        missing = [name for name in names_from if name not in named]
        errors += [f"table file {path}: from {name} names no row" for name in missing]
        if names_from and not missing:
            named[names_from[0]][1]["paths"] += own
    for status, name, where, own in dirs:
        for f in files or ():
            inside = f.startswith(where + "/") if status == "slot" else f.rpartition("/")[0] == where
            if inside and f not in own:
                errors.append(f"{status} {name}: its directory holds {f}")
    return {"modules": [row for _, row in rows]}, errors


def tree(root=ROOT, files=None):
    """table_of() over a checkout's table files."""
    root = Path(root)
    return table_of({p: (root / p).read_text(encoding="utf-8") for p in table_files(root)}, files)


def load(root=ROOT):
    """A checkout's table, {"modules": [...]}; ValueError naming its first
    error, when it has any."""
    table, errors = tree(root)
    if errors:
        raise ValueError(f"the table: {errors[0]}" + (f", and {len(errors) - 1} more" if len(errors) > 1 else ""))
    return table


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
    """The tracked file an #include "name" of the file including names, its
    .. steps taken, or None."""
    for base in (posixpath.dirname(including), *ROOTS):
        candidate = posixpath.normpath(posixpath.join(base, name))
        if candidate in tracked:
            return candidate
    return None


def check(table, files, read, targets=None, left=None):
    """Every error: the table's, each file's ownership, each include edge and
    unresolved include and, with targets [(name, [tracked sources], [linked
    target names])], each link edge. read(path) gives a tracked file's bytes;
    a vendored file's unresolved include goes to the list left, if given."""
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
        data = read(f)
        for match in INCLUDE.finditer(data):
            name = match.group(1).decode("utf-8", "replace")
            found = resolve(f, name, tracked)
            if found is None:
                line = data.count(b"\n", 0, match.start()) + 1
                if not f.startswith(VENDORED):
                    errors.append(f'include {f}:{line} "{name}": resolves to no tracked file')
                elif left is not None:
                    left.append(f'{f}:{line} "{name}"')
            elif found in module and not covered(module[f], module[found]):
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
    git = GIT["which"]()
    if git is None:
        raise FileNotFoundError("no git")
    done = GIT["run"](git, ["ls-files", "-z"], cwd=source, check=True)
    return [p for p in done.stdout.decode("utf-8").split("\0") if p]


def run(source, build=None):
    """The check's errors on a checkout, its table read from its tree, and,
    given one, its build tree, and the vendored includes left unresolved."""
    source = Path(source)
    files = tracked(source)
    table, errors = tree(source, files)
    targets = codemodel(build, source) if build else None
    left = []
    return errors + check(table, files, lambda f: (source / f).read_bytes(), targets, left), left


def main(argv):
    if argv in (["--help"], ["-h"]):
        print(__doc__.strip())
        return 0
    if len(argv) not in (1, 2):
        print("usage: loop_modules.py SOURCE [BUILD]", file=sys.stderr)
        return 2
    try:
        errors, left = run(*argv)
    except (OSError, ValueError, KeyError) as error:
        print(f"modules: the check could not run: {error}", file=sys.stderr)
        return 2
    for line in errors:
        print(f"modules: {line}")
    print(f"vendored: {len(left)} includes in {VENDORED} left unresolved")
    print(f"modules: {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
