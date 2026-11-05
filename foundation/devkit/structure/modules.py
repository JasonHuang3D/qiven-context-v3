"""The modules' check: the modules' table kept true of the checkout. tools/loop_modules.py is its command line.

The table is read by foundation/devkit/python/table.py, whose errors it adds to. A table file's known values
(platforms, kind, may_depend, layer) and its layer or name not its directory's are refused; so are a row's path
holding no tracked file, given twice, or outside its table file's directory but in tools/trees.json's walked trees
(the old places) and a whole-build row's root files, a path of the root module.json's rows not the repository's
own at its root (whole-build rows alone: a file there or a directory of no tree), a row's path in the old places
outside its table file's directory that its row in that file at the base commit (BASE, tools/loop_base.py) neither
gave nor holds, so that they only shrink, a new module's, a new row's or a moved one's among them, a tracked file
in a slot's directory, or directly in a group's, but its README.md and module.json, and a built one's contract
naming no tracked file. Each table directory's README.md repeats its module.json's fields in one form, each line
refused that differs: line 1 `# <name>`, line 3 `layer <layer>, status <status>`, and a `- **Contract:**`,
`- **Parameters:**` and `- **May depend on:**` line for each of those keys the file has; a line naming the
consumers is refused (the table's reverse edges give them).

Each tracked file in one module; each module's dependencies, followed
transitively, covering its includes (each #include "..." resolved to the
tracked file it names, from its directory, a module directory's own
tests' from that directory's src/ too, and each #include <qiven/...>
or <qiven3/...> without it, from the build's include directories
and each module directory's include/ under foundation/, kits/ and
products/; one resolving to none refused but in a vendored file, tools/trees.json's
vendored, where they are counted on one line) and, given a configured build tree, its links
(read from CMake's file API, the codemodel the root CMakeLists.txt asks
for, an INTERFACE target's its CMakeLists.txt's when it has no source; the
build's own not read): a library file's and target's by depends alone,
its closure stopping at the test kit and at test code alone, test code's by
test_depends too (a file under tests/, or under the tests/ of a directory
holding a table file; a target of such files alone), a module whose every
file is test code, the kit's apart, test code: its edges onto the kit in
test_depends, a library one refused; library code including or linking test
code refused; test code under a module directory's own tests/ including
another module's test code refused, but the test kit's and a fake's (a file
under a directory named fake or fake_*); the support layer's rows reached
with no edge by test code alone, and reaching their own dependencies alone, their library's or their
tests'. A cycle in depends refused. The layer rule: a row's layer
its table file's in the tree (devkit, core, system, services, kits,
products, in that order; tools/ outside the link graph, the root's
whole-build rows below it, depending on none of it),
never its row's word; an edge in depends or test_depends going up, from
one product onto another's, or onto a module outside the link graph,
refused, but the test kit's (the rows of the table files under
foundation/devkit/test/) up to core, system and services, and the
exceptions a built table file lists for its own rows (from, to, until:
what removes it), each refused that names no edge the table has or one the
rule allows; the support layer outside the test kit refused; a depends
edge onto the test kit, or onto a module whose every file is test code
(its path on to the kit named), refused, with no exception, but the kit's
own and such a module's onto another, as test code alone reaches the kit; a
library linking the kit's target refused. A module or a dependency the
table does not name, or a layer it does not know, refused. One line an
error, naming the file or the edge, then the count of vendored includes
left unresolved; exit 0 when none, 1 when any, 2 when the check could not
run.
"""

import json
import posixpath
import re
from pathlib import Path

from foundation.devkit.python import git
from foundation.devkit.python import table as module_table

FILE_LAYERS = ("devkit", "core", "system", "services", "kits", "products", "foundation", "tools", "repository")
PLATFORMS, KINDS = ("windows",), ("adapter", "module")  # the known values of platforms and of kind
MAY_DEPEND = ("devkit", "core", "system", "services", "kits")  # of may_depend
BASE = "tools/loop_base.json"  # each table file's rows' old paths at the base commit it names (tools/loop_base.py)
LAYERS = ("foundation", "product", "tooling", "support", "whole-build", "no-test")
ORDER = ("devkit", "core", "system", "services", "kits", "products")  # the link graph's layers, lowest first
KIT = "foundation/devkit/test/"  # the test kit's table files: the runner, the case main, the faults kit, the scan
INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<((?:qiven|qiven3)/[^>]+)>)', re.M)  # angle: ours


def old_paths(home, row, trees):
    """A row's paths in the old places, the walked trees, outside its table file's directory, home that file."""
    where = home.rpartition("/")[0]
    return [p for p in row["paths"] if where and p.startswith(tuple(f"{tree}/" for tree in trees.walked))
            and not p.startswith(where + "/")]


def old_rows(texts, trees):
    """{table file: {row: its old paths}} of the table's files {path: text}, the rows of none left out: BASE's
    rows, of the files at the base. Their text at a commit, which the table's reader, of a checkout, never reads."""
    found = {path: {row["name"]: old_paths(path, row, trees) for row in json.loads(text).get("present", ())}
             for path, text in sorted(texts.items())}
    return {path: {n: p for n, p in rows.items() if p} for path, rows in found.items() if any(rows.values())}


def tree_layer(where):
    """A table file's directory's layer: foundation/<layer>/... gives its
    layer, kits/ and products/ theirs, the root, foundation/ and tools/
    their own."""
    parts = where.split("/")
    return {"": "repository", "foundation": "foundation", "tools": "tools"}.get(where) or (
        parts[1] if parts[0] == "foundation" else parts[0])


def fields(where, data, shallow):
    """A table file's values this check does not know, and its layer or name not its directory's (tree_layer()); a
    directory's name its name, but the deeper of two directories of one name takes its parent's as a prefix."""
    out, parts = [], where.split("/")
    for key, known in (("platforms", PLATFORMS), ("may_depend", MAY_DEPEND)):
        if key in data and not set(data[key]) <= set(known):
            out.append(f"{key} {data[key]!r} is not a list of {', '.join(known)}")
    if "kind" in data and data["kind"] not in KINDS:
        out.append(f"kind {data['kind']!r} is not one of {', '.join(KINDS)}")
    if data["layer"] not in FILE_LAYERS:
        out.append(f"layer {data['layer']} is none of {', '.join(FILE_LAYERS)}")
    layer = tree_layer(where)
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


def file_errors(table, files, readmes, base):
    """The errors of the table's files against the tree: their known values and directories (fields()), the tracked
    files, the table directories' README.md {path: text} and BASE's rows, when given."""
    errors, trees = [], table["trees"]
    old = tuple(f"{tree}/" for tree in trees.walked)
    shallow = {}  # a directory's name: the fewest parts of a table file's directory of that name
    for where in (p.rpartition("/")[0] for p in table["files"] if "/" in p):
        shallow[where.rpartition("/")[2]] = min(shallow.get(where.rpartition("/")[2], 99), where.count("/"))
    for path, data in sorted(table["files"].items()):
        where = path.rpartition("/")[0]
        errors += [f"table file {path}: {line}" for line in fields(where, data, shallow)]
        if data.get("contract") is not None and data["contract"] not in files:
            errors.append(f"table file {path}: contract {data['contract']} names no tracked file")
        doc = f"{where}/README.md" if where else "README.md"
        errors += [f"readme {doc}: {line}" for line in readme(data, readmes[doc])] if doc in readmes else []
        for row in data.get("present", ()):
            errors += [f"table file {path}: row {row['name']}: path {p} lies outside {where}/" for p in row["paths"]
                       if where and not p.startswith((where + "/", *old))
                       and not ("/" not in p and row["layer"] == "whole-build")]
            errors += [f"table file {path}: row {row['name']}: path {p} is not the repository's own, at its root, "
                       "as a whole-build row's" for p in row["paths"]
                       if not where and ("/" in p.rstrip("/") or p.rstrip("/") in trees.every)]
            if base is not None:
                given = base.get(path, {}).get(row["name"], ())
                errors += [f"row {row['name']}: path {p} in the old places, in {path}, not given it there at the base"
                           for p in old_paths(path, row, trees) if not any(module_table.holds(q, p) for q in given)]
        if data["status"] != "built":
            own = module_table.own_paths(path)
            for f in files:
                inside = f.startswith(where + "/") if data["status"] == "slot" else f.rpartition("/")[0] == where
                if inside and f not in own:
                    errors.append(f"{data['status']} {data['name']}: its directory holds {f}")
    for row in table["modules"]:
        errors += [f"row {row['name']}: path {p} twice" for p in sorted(set(row["paths"]))
                   if row["paths"].count(p) > 1]
        errors += [f"row {row['name']}: path {p} holds no tracked file" for p in dict.fromkeys(row["paths"])
                   if not any(module_table.holds(p, f) for f in files)]
    return errors


def tree(root, files):
    """The table of a checkout, read by its reader, and its errors, those of file_errors() added, given the tracked
    files; BASE's rows, a tracked file, else refused."""
    root = Path(root)
    table, errors = module_table.tree(root)
    docs = (f"{where}/README.md" if where else "README.md" for where in (p.rpartition("/")[0] for p in table["files"]))
    readmes = {doc: (root / doc).read_text(encoding="utf-8") for doc in docs if doc in files}
    held = BASE in files and (root / BASE).is_file()
    base = json.loads((root / BASE).read_text(encoding="utf-8"))["rows"] if held else None
    errors += file_errors(table, files, readmes, base)
    return table, errors if held else [f"{BASE}: not a tracked file: the old places unchecked", *errors]


def fake(path):
    """Whether a path lies under a directory named fake or fake_*: a fake's."""
    return any(part == "fake" or part.startswith("fake_") for part in path.split("/")[:-1])


def reach(table, keys=("depends", "test_depends"), stop=frozenset()):
    """{module: every module its dependencies reach, transitively, itself
    not included}: its library's and its tests', or those keys alone; one of stop by one of stop alone."""
    depends = {m["name"]: {o for key in keys for o in m.get(key, ())} for m in table["modules"]}
    closure = {}
    for name in depends:
        seen, stack = set(), list(depends[name])
        while stack:
            other = stack.pop()
            if other not in seen and other in depends and (name in stop or other not in stop):
                seen.add(other)
                stack.extend(depends[other])
        closure[name] = seen - {name}
    return closure


def test_alone(table):
    """The test kit's rows (their table files under KIT) and the rows whose
    every path is test code, their README.md and module.json apart."""
    homes = table["homes"]
    dirs = {home.rpartition("/")[0] for home in homes.values()}
    return {name for name, home in homes.items() if home.startswith(KIT)}, {
        m["name"] for m in table["modules"] if m["paths"] and all(
            table["trees"].test_path(p, dirs) is not None or p.endswith(("README.md", "module.json"))
            for p in m["paths"])}


def layer_rule(table):
    """refused(source, target): why the layer rule refuses an edge, or None.
    A row's layer is its table file's in the tree, never its own word; an edge goes down or stays in its
    layer, never from one product onto another's; the test kit's edges up to core, system and services are
    allowed, as nothing but a test reaches the kit; tools/ and the repository are outside the link graph.
    Test code alone reaches the test kit: a library's edge, in depends, onto the kit or onto test code
    alone refused (its path on to the kit named), but the kit's own and test code's."""
    homes = table["homes"]
    place = {name: tree_layer(home.rpartition("/")[0]) for name, home in homes.items()}
    product = {name: home.split("/")[1] for name, home in homes.items() if home.startswith("products/")}
    kit, tested = test_alone(table)
    depends = {m["name"]: m["depends"] + (m["test_depends"] if m["name"] in tested - kit else [])
               for m in table["modules"]}  # test code alone onto the kit by its test edges

    def onward(start):  # the shortest depends path from start onto the kit, or []
        paths, seen = [[start]], {start}
        for path in paths:  # breadth first, each module once
            paths += [] if path[-1] in kit else [path + [o] for o in depends.get(path[-1], ()) if not seen & {o}]
            seen |= set(depends.get(path[-1], ()))
        return next((path for path in paths if path[-1] in kit), [])

    def refused(source, target, library=False):
        low, high = place.get(source), place.get(target)
        if library and target in kit | tested and source not in kit | tested:
            way = " -> ".join(onward(target))
            return "a library on the test kit, which test code alone reaches" if target in kit else (
                f"a library on {target}, test code alone" + (f", onto the test kit by {way}" if way else ""))
        if library and target in kit and source in tested - kit:  # test code alone: its edges test edges
            return "a library on the test kit from test code alone, whose edge onto it is a test edge"
        if low not in ("repository", *ORDER) or high is None:  # tools/, or a row of no table file (a test's)
            return None
        if high not in ORDER:
            return f"{target} is outside the link graph"
        if low == "repository" or ORDER.index(high) > ORDER.index(low) and not (source in kit and high in ORDER[1:4]):
            return f"up from {low} to {high}" + (", past the test kit's reach" if source in kit else "")
        if low == high == "products" and product[source] != product[target]:
            return f"a product on another product, {product[target]}"
        return None

    return refused


def check_table(table):
    """The errors of the table alone: names, layers, a cycle in depends,
    named by its modules in order; each edge by the layer rule
    (layer_rule()), but the table's exceptions, each one naming an edge the
    table has and the rule refuses; the support layer outside the test kit."""
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
    refused, homes = layer_rule(table), table["homes"]
    excepted = [(e["from"], to) for e in table["exceptions"] for to in e["to"]]
    edges = {m["name"]: m["depends"] + m.get("test_depends", []) for m in table["modules"]}
    library = {m["name"]: m["depends"] for m in table["modules"]}
    for m in table["modules"]:
        if m["layer"] not in LAYERS:
            errors.append(f"module {m['name']}: layer {m['layer']} is none of {', '.join(LAYERS)}")
        if m["layer"] == "support" and m["name"] in homes and not homes[m["name"]].startswith(KIT):
            errors.append(f"module {m['name']}: layer support outside the test kit, {KIT}")
        for other in dict.fromkeys(edges[m["name"]]):
            why = refused(m["name"], other, other in m["depends"]) if other in layer else f"no module {other}"
            if why and (other not in layer or (m["name"], other) not in excepted or why.startswith("a library on")):
                errors.append(f"edge {m['name']} -> {other}: {why}")
    for source, target in dict.fromkeys(excepted):
        if excepted.count((source, target)) > 1:
            errors.append(f"exception {source} -> {target}: listed twice")
        if target not in edges.get(source, ()):
            errors.append(f"exception {source} -> {target}: the table has no such edge")
        elif not refused(source, target, target in library[source]):
            errors.append(f"exception {source} -> {target}: the layer rule allows the edge")
    return errors


def include_roots(trees, files):
    """The include directories the build gives, trees.included, then each module directory's include/ under the
    layers' trees that holds a tracked C++ file."""
    homes = {f.rpartition("/")[0] for f in files
             if trees.is_table_file(f) and "/" in f and f.split("/", 1)[0] in trees.layers}
    return trees.included + tuple(sorted({f[:f.index("/include/")] + "/include" for f in files
                                          if "/include/" in f and f.endswith(trees.cxx_suffixes)
                                          and f[:f.index("/include/")] in homes}))


def resolve(trees, including, name, tracked, roots, angle=False, homes=()):
    """The tracked file an #include "name" of the file including names, its
    .. steps taken, or None; an angle include from the roots alone. A file
    under the tests/ of a module directory, one of homes, resolves from that
    directory's src/ after its own, as a module's shape in LAYERS.md
    forbids src/ from outside alone."""
    under = trees.test_path(including, homes)
    own = (including[:-len(under) - 1] + "/src",) if under and under != including else ()
    for base in (() if angle else (posixpath.dirname(including),) + own) + tuple(roots):
        candidate = posixpath.normpath(posixpath.join(base, name))
        if candidate in tracked:
            return candidate
    return None


def check(table, files, read, targets=None, left=None):
    """Every error: the table's, each file's ownership, each include edge and
    unresolved include and, with targets [(name, [tracked sources], [linked
    target names])], each link edge. read(path) gives a tracked file's bytes;
    a vendored file's unresolved include goes to the list left, if given."""
    errors, trees = check_table(table), table["trees"]
    owner = module_table.owners(table, files)
    for f, names in sorted(owner.items()):
        if not names:
            errors.append(f"file {f}: in no module")
        elif len(names) > 1:
            errors.append(f"file {f}: in modules {', '.join(names)}")
    module = {f: names[0] for f, names in owner.items() if len(names) == 1}
    kit, alone = test_alone(table)  # a library's closure stops at them: test code alone reaches them
    closure = {False: reach(table, ("depends",), kit | alone), True: reach(table)}
    whole = {m["name"] for m in table["modules"] if m["layer"] == "whole-build"}  # the build's, linked by all
    homes = {f.rpartition("/")[0] for f in files if trees.is_table_file(f) and "/" in f}

    def test_code(f):
        return trees.test_path(f, homes) is not None

    # A test file is its module's tests' when its module holds files outside test code, its README.md and
    # module.json apart, or when its every file is test code, the kit's apart, whose edges are test edges.
    elsewhere = {name for f, name in module.items() if not test_code(f) and not f.endswith(("README.md", "module.json"))}

    def of_tests(f):
        return test_code(f) and module.get(f) in elsewhere | (alone - kit)

    shared = {m["name"]: {False: set(m["depends"]), True: set(m["depends"] + m.get("test_depends", []))}
              for m in table["modules"] if m["layer"] == "support"}

    def covered(source, target, tests, code):
        """The shared support is every module's test code's, and reaches its
        own dependencies alone, not theirs. A library reaches its library's
        dependencies; its tests, its tests' too."""
        if source in shared:
            return source == target or target in shared[source][tests]
        return source == target or target in closure[tests].get(source, ()) or (code and target in shared)

    tracked, roots = set(files), include_roots(trees, files)
    for f in sorted(files):
        if not f.endswith(trees.cxx_suffixes) or f not in module:
            continue
        data = read(f)
        for match in INCLUDE.finditer(data):
            angle = match.group(1) is None
            name = (match.group(2) if angle else match.group(1)).decode("utf-8", "replace")
            found, shown = resolve(trees, f, name, tracked, roots, angle, homes), f"<{name}>" if angle else f'"{name}"'
            if found is None:
                line = data.count(b"\n", 0, match.start()) + 1
                if f not in trees.vendored:
                    errors.append(f"include {f}:{line} {shown}: resolves to no tracked file")
                elif left is not None:
                    left.append(f"{f}:{line} {shown}")
            elif found in module and not test_code(f) and test_code(found):
                errors.append(f"include {f} -> {found}: library code including test code")
            elif (found in module and test_code(f) and not f.startswith(f"{trees.tests}/") and test_code(found)
                  and module[found] not in kit | {module[f]} and not fake(found)):
                errors.append(f"include {f} -> {found}: module {module[f]}'s tests including module "
                              f"{module[found]}'s test code, not the test kit's or a fake's")
            elif found in module and not covered(module[f], module[found], of_tests(f), test_code(f)):
                errors.append(f"include {f} -> {found}: module {module[f]} does not depend on {module[found]}")
    of_target, tests_target, code_target = {}, set(), set()
    for name, sources, _ in targets or ():
        mods = sorted({module[s] for s in sources if s in module})
        if len(mods) > 1:
            errors.append(f"target {name}: sources in modules {', '.join(mods)}")
        elif mods:
            of_target[name] = mods[0]
            tests_target |= {name} if all(of_tests(s) for s in sources if s in module) else set()
            code_target |= {name} if all(test_code(s) for s in sources if s in module) else set()
    for name, _, links in targets or ():
        for other in links:
            if name not in of_target or other not in of_target or of_target[other] in whole:
                continue
            if name not in code_target and other in code_target:
                errors.append(f"link {name} -> {other}: library code linking test code")
            elif name not in code_target and of_target[other] in kit and of_target[name] not in kit | alone:
                errors.append(f"link {name} -> {other}: a library on the test kit, which test code alone reaches")
            elif not covered(of_target[name], of_target[other], name in tests_target, name in code_target):
                errors.append(f"link {name} -> {other}: module {of_target[name]} does not depend on {of_target[other]}")
    return errors


def codemodel(build, source):
    """[(target, [its tracked sources, relative to source], [the targets it
    links or depends on])] of a configured build tree's codemodel reply; an
    INTERFACE target too, its sources its own or else its CMakeLists.txt."""
    reply = Path(build) / ".cmake" / "api" / "v1" / "reply"
    indices = sorted(reply.glob("index-*.json"))
    if not indices:
        raise FileNotFoundError(f"{reply}: no codemodel reply; configure the tree first")
    index = json.loads(indices[-1].read_text(encoding="utf-8"))
    model = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text(encoding="utf-8"))
    conf = model["configurations"][0]
    loaded = [t for t in (json.loads((reply / ref["jsonFile"]).read_text(encoding="utf-8"))
                          for ref in conf["targets"] + conf.get("abstractTargets", [])) if not t.get("imported")]
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
        if not sources and t["type"] == "INTERFACE_LIBRARY":
            sources.append(posixpath.normpath(posixpath.join(t["paths"]["source"], "CMakeLists.txt")))
        links = [*t.get("dependencies", ()), *t.get("linkLibraries", ()), *t.get("interfaceLinkLibraries", ())]
        rows.append((t["name"], sources, list(dict.fromkeys(names[d["id"]] for d in links if d.get("id") in names))))
    return rows


def run(source, build=None):
    """The check's errors on a checkout, its table read from its tree, and,
    given one, its build tree, the vendored includes left unresolved, and its Trees."""
    source = Path(source)
    files = git.tracked(source)
    table, errors = tree(source, files)
    targets = codemodel(build, source) if build else None
    left = []
    return errors + check(table, files, lambda f: (source / f).read_bytes(), targets, left), left, table["trees"]
