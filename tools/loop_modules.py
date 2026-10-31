"""The modules' check: the modules' table kept true of the checkout.

    py -3.15 -I tools/loop_modules.py SOURCE [BUILD]
    py -3.15 -I tools/loop_modules.py --help

The table is the tree: its files are module.json at the root,
tools/module.json and each module.json under foundation/, kits/ and
products/, read in sorted path order. Its rows: each built module's present
rows, its own README.md and module.json (the root's module.json alone) added
to its first row, or to the first row its from names when it has none; a
slot's directory a row of the no-test layer, and a group's README.md and
module.json one. A row's dependencies are its library's (depends) and its
tests' (test_depends). A table file that does not parse, lacks or adds a
key for its status, gives a field of a wrong type or a value this check
does not know, or a layer or a name not its directory's, is refused; so
are two table files or two rows of one name, a from or an of naming no
row, a row's path holding no tracked file or given twice, a tracked file
in a slot's directory, or directly in a group's, but its README.md and
module.json, a built one's contract naming no tracked file. Each table
directory's README.md repeats its module.json's fields in one form, each
line refused that differs: line 1 `# <name>`, line 3 `layer <layer>,
status <status>`, and a `- **Contract:**`, `- **Parameters:**` and
`- **May depend on:**` line for each of those keys the file has; a line
naming the consumers is refused (the table's reverse edges give them).

Each tracked file in one module; each module's dependencies, followed
transitively, covering its includes (each #include "..." resolved to the
tracked file it names, from its directory, and each #include <qiven/...>
or <qiven3/...> without it, from the build's include directories
and each module directory's include/ under foundation/, kits/ and
products/; one resolving to none refused but in third_party/, where they
are counted on one line) and, given a configured build tree, its links
(read from CMake's file API, the codemodel the root CMakeLists.txt asks
for): a library file's and target's by depends alone, a file and a target
under tests/ by test_depends too, a module whose every file is under tests/
its own library. A cycle in depends refused; a foundation module depending
on a product module refused, but the table's listed exceptions; a module or
a dependency the table does not name, or a layer it does not know, refused.
One line an error, naming the
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
TREES = ("src", "tests", "tools", "third_party", *TOPS)  # the trees every check that walks the checkout walks
FILE_LAYERS = ("devkit", "core", "system", "services", "kits", "products", "foundation", "tools", "repository")
KEYS = {  # a table file's keys by its status; a built one with no present rows has from, a slot may have of
    "built": ("name", "layer", "status", "platforms", "contract", "adopted", "parameters", "present"),
    "slot": ("name", "layer", "status", "kind", "may_depend", "platforms", "adopted", "parameters"),
    "group": ("name", "layer", "status", "adopted"),
}
ROW = ("name", "layer", "depends", "test_depends", "paths")  # a present row's keys
PLATFORMS, KINDS = ("windows",), ("adapter", "module")  # the known values of platforms and of kind
MAY_DEPEND = ("devkit", "core", "system", "services", "kits")  # of may_depend
PARAMETER = ("name", "default", "built")  # a parameter's keys
LAYERS = ("foundation", "product", "tooling", "support", "whole-build", "no-test")
SOURCES = (".c", ".cpp", ".h", ".hpp")
INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<((?:qiven|qiven3)/[^>]+)>)', re.M)  # angle: ours
ROOTS = ("src", "tests", "third_party/sqlite")  # the include directories the build gives, and include_roots()'
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


def strings(value, known=None):
    """Whether value is a list of strings, each of known, when given."""
    return isinstance(value, list) and all(isinstance(v, str) and (known is None or v in known) for v in value)


def fields(where, data, shallow):
    """A table file's fields of a wrong type or an unknown value, and its
    layer or name not its directory's: foundation/<layer>/... gives its
    layer, kits/ and products/ theirs, the root, foundation/ and tools/
    their own; a directory's name its name, but the deeper of two
    directories of one name takes its parent's as a prefix."""
    out, parts = [], where.split("/")
    for key, known in (("platforms", PLATFORMS), ("may_depend", MAY_DEPEND), ("from", None)):
        if key in data and not strings(data[key], known):
            out.append(f"{key} {data[key]!r} is not a list of " + (", ".join(known) if known else "strings"))
    for key, known in (("kind", KINDS), ("of", None), ("name", None), ("layer", None)):
        if key in data and not (isinstance(data[key], str) and (known is None or data[key] in known)):
            out.append(f"{key} {data[key]!r} is not " + (f"one of {', '.join(known)}" if known else "a string"))
    if not isinstance(data.get("contract"), (str, type(None))):
        out.append(f"contract {data['contract']!r} is not a string or null")
    if "parameters" in data and not (isinstance(data["parameters"], list) and all(
            isinstance(p, dict) and set(p) == set(PARAMETER) and isinstance(p["name"], str)
            and isinstance(p["built"], bool) for p in data["parameters"])):
        out.append(f"parameters are not a list of objects of keys {', '.join(PARAMETER)}, its name a string, "
                   "built true or false")
    if not isinstance(data.get("present", []), list):
        out.append(f"present {data['present']!r} is not a list")
    elif any(not isinstance(row, dict) or set(row) != set(ROW) for row in data.get("present", [])):
        out.append(f"a present row lacks or adds a key of {', '.join(ROW)}")
    else:
        out += [f"present row {i}: {key} {row[key]!r} is not " + ("a string" if key in ("name", "layer") else
                                                                  "a list of strings")
                for i, row in enumerate(data.get("present", [])) for key in ROW
                if not (isinstance(row[key], str) if key in ("name", "layer") else strings(row[key]))]
    if out:
        return out
    layer = {"": "repository", "foundation": "foundation", "tools": "tools"}.get(where) or (
        parts[1] if parts[0] == "foundation" else parts[0])
    name = f"{parts[-2]}_{parts[-1]}" if len(parts) > 1 and where.count("/") > shallow[parts[-1]] else parts[-1]
    out += [f"layer {data['layer']} is not its directory's, {layer}"] if data["layer"] != layer else []
    return out + ([f"name {data['name']} is not its directory's, {name}"]
                  if where not in ("", "tools") and data["name"] != name else [])


def readme(data, text):
    """A README.md's lines that differ from its module.json: line 1 `# name`,
    line 3 `layer L, status S`; one line of each key it has of contract,
    parameters (each one not built so marked) and may_depend; a line naming
    the consumers, which the table's reverse edges give, refused."""
    lines, out = text.splitlines(), []
    for n, want in ((1, f"# {data['name']}"), (3, f"layer {data['layer']}, status {data['status']}")):
        out += [] if lines[n - 1:n] == [want] else [f"line {n} is not '{want}'"]
    shown = {"Contract": "none" if data.get("contract") is None else f"`{data['contract']}`",
             "Parameters": "; ".join(f"`{p['name']}`" + ("" if p["built"] else " (not built yet)")
                                     for p in data.get("parameters", ())) or "none",
             "May depend on": ", ".join(data.get("may_depend", ())) or "none"}
    for label, key in (("Contract", "contract"), ("Parameters", "parameters"), ("May depend on", "may_depend")):
        found = [line for line in lines if line.startswith(f"- **{label}:**")]
        if key in data and found != [f"- **{label}:** {shown[label]}"]:
            out.append(f"its {label} line is not '- **{label}:** {shown[label]}'")
    return out + [f"names its consumers: {line}" for line in lines
                  if line.startswith(("- **Consumers:**", "- **Expected consumers:**"))]


def table_of(texts, files=None, readmes=None):
    """The table, {"modules": [rows]}, and its errors, from the table's
    files {path: text}; given the tracked files, what a slot's or a group's
    directory holds and a contract checked too; given {path: text} of the
    table directories' README.md, each checked against its module.json."""
    errors, rows, names, later, dirs, slots = [], [], {}, [], [], []
    shallow = {}  # a directory's name: the fewest parts of a table file's directory of that name
    for where in (p.rpartition("/")[0] for p in texts if "/" in p):
        shallow[where.rpartition("/")[2]] = min(shallow.get(where.rpartition("/")[2], 99), where.count("/"))
    for path in sorted(texts):
        where = path.rpartition("/")[0]
        own = [f"{where}/README.md", f"{where}/module.json"] if where else [path]
        try:
            data = json.loads(texts[path])
        except ValueError as error:
            errors.append(f"table file {path}: does not parse: {error}")
            continue
        status = data.get("status") if isinstance(data, dict) else None
        if not isinstance(status, str) or status not in KEYS:
            errors.append(f"table file {path}: " + (f"status {status} is none of {', '.join(KEYS)}"
                                                    if isinstance(data, dict) else "is not a JSON object"))
            continue
        want = set(KEYS[status]) | ({"from"} if status == "built" and data.get("present") == [] else set())
        lacks, adds = want - set(data), set(data) - want - ({"of"} if status == "slot" else set())
        if lacks or adds:
            errors.append(f"table file {path}: " + "; ".join(
                f"{how} {', '.join(sorted(keys))}" for how, keys in (("lacks", lacks), ("adds", adds)) if keys))
            continue
        wrong = fields(where, data, shallow)
        if wrong:
            errors += [f"table file {path}: {line}" for line in wrong]
            continue
        if files is not None and data.get("contract") is not None and data["contract"] not in files:
            errors.append(f"table file {path}: contract {data['contract']} names no tracked file")
        doc = f"{where}/README.md" if where else "README.md"
        errors += [f"readme {doc}: {line}" for line in readme(data, readmes[doc])] if doc in (readmes or {}) else []
        if "of" in data:
            slots.append((path, data["of"]))
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
            rows.append((path, {"name": data["name"], "layer": "no-test", "depends": [], "test_depends": [],
                                "paths": paths}))
            dirs.append((status, data["name"], where, own))
    named = {}
    for path, row in rows:
        if row["name"] in named:
            errors.append(f"row {row['name']}: in {named[row['name']][0]} and {path}")
        named.setdefault(row["name"], (path, row))
        errors += [f"row {row['name']}: path {p} twice" for p in sorted(set(row["paths"]))
                   if row["paths"].count(p) > 1]
        errors += [f"row {row['name']}: path {p} holds no tracked file" for p in dict.fromkeys(row["paths"])
                   if files is not None and not any(holds(p, f) for f in files)]
    errors += [f"table file {path}: of {of} names no row" for path, of in slots if of not in named]
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
    """table_of() over a checkout's table files and, given the tracked
    files, their directories' tracked README.md."""
    root = Path(root)
    paths = table_files(root)
    readmes = {p: (root / p).read_text(encoding="utf-8") for p in (q.replace("module.json", "README.md") for q in paths)
               if files is not None and p in files}
    return table_of({p: (root / p).read_text(encoding="utf-8") for p in paths}, files, readmes)


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


def reach(table, keys=("depends", "test_depends")):
    """{module: every module its dependencies reach, transitively, itself
    not included}: its library's and its tests', or those keys alone."""
    depends = {m["name"]: {o for key in keys for o in m.get(key, ())} for m in table["modules"]}
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
    """The errors of the table alone: names, layers, the foundation line, a
    cycle in depends, named by its modules in order."""
    errors = []
    layer = {m["name"]: m["layer"] for m in table["modules"]}
    state = {}  # a module's walk: 1 while its dependencies are walked, 2 once they are

    def walk(path):
        state[path[-1]] = 1
        for other in next(m["depends"] for m in table["modules"] if m["name"] == path[-1]):
            if state.get(other) == 1:
                errors.append("a cycle in depends: " + " -> ".join(path[path.index(other):] + [other]))
            elif other in layer and other not in state:
                walk(path + [other])
        state[path[-1]] = 2

    for name in sorted(layer):
        if name not in state:
            walk([name])
    for m in table["modules"]:
        if m["layer"] not in LAYERS:
            errors.append(f"module {m['name']}: layer {m['layer']} is none of {', '.join(LAYERS)}")
        for other in dict.fromkeys(m["depends"] + m.get("test_depends", [])):
            if other not in layer:
                errors.append(f"edge {m['name']} -> {other}: no module {other}")
            elif m["layer"] == "foundation" and layer[other] == "product" and not excepted(table, m["name"], other):
                errors.append(f"edge {m['name']} -> {other}: foundation depending on product")
    return errors


def include_roots(files):
    """ROOTS, then each module directory's include/ under TOPS that holds a tracked file."""
    homes = {f.rpartition("/")[0] for f in files if is_table_file(f) and "/" in f and f.split("/", 1)[0] in TOPS}
    return ROOTS + tuple(sorted({f[:f.index("/include/")] + "/include" for f in files
                                 if "/include/" in f and f[:f.index("/include/")] in homes}))


def resolve(including, name, tracked, roots=ROOTS, angle=False):
    """The tracked file an #include "name" of the file including names, its
    .. steps taken, or None; an angle include from the roots alone."""
    for base in (() if angle else (posixpath.dirname(including),)) + tuple(roots):
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
    closure = {False: reach(table, ("depends",)), True: reach(table)}
    # A file under tests/ is its module's tests' when its module holds files outside tests/, its README.md and
    # module.json apart; a module whose every file is under tests/ is its own library.
    elsewhere = {m["name"] for m in table["modules"]
                 if any(not p.startswith("tests/") and not p.endswith(("README.md", "module.json")) for p in m["paths"])}

    def of_tests(f):
        return f.startswith("tests/") and module.get(f) in elsewhere

    shared = {m["name"]: set(m["depends"]) for m in table["modules"] if m["layer"] == "support"}

    def covered(source, target, tests):
        """The shared support is every module's, and reaches its own
        dependencies alone, not theirs. A library reaches its library's
        dependencies; its tests, its tests' too."""
        if source in shared:
            return source == target or target in shared[source]
        return source == target or target in closure[tests].get(source, ()) or target in shared

    tracked, roots = set(files), include_roots(files)
    for f in sorted(files):
        if not f.endswith(SOURCES) or f not in module:
            continue
        data = read(f)
        for match in INCLUDE.finditer(data):
            angle = match.group(1) is None
            name = (match.group(2) if angle else match.group(1)).decode("utf-8", "replace")
            found, shown = resolve(f, name, tracked, roots, angle), f"<{name}>" if angle else f'"{name}"'
            if found is None:
                line = data.count(b"\n", 0, match.start()) + 1
                if not f.startswith(VENDORED):
                    errors.append(f"include {f}:{line} {shown}: resolves to no tracked file")
                elif left is not None:
                    left.append(f"{f}:{line} {shown}")
            elif found in module and not covered(module[f], module[found], of_tests(f)):
                errors.append(f"include {f} -> {found}: module {module[f]} does not depend on {module[found]}")
    of_target, tests_target = {}, set()
    for name, sources, _ in targets or ():
        mods = sorted({module[s] for s in sources if s in module})
        if len(mods) > 1:
            errors.append(f"target {name}: sources in modules {', '.join(mods)}")
        elif mods:
            of_target[name] = mods[0]
            tests_target |= {name} if all(of_tests(s) for s in sources if s in module) else set()
    for name, _, links in targets or ():
        for other in links:
            if name in of_target and other in of_target and not covered(of_target[name], of_target[other],
                                                                        name in tests_target):
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
