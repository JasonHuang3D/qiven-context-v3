"""The modules' check: the modules' table kept true of the checkout.

    py -3.15 -I tools/loop_modules.py SOURCE [BUILD]
    py -3.15 -I tools/loop_modules.py --help

The table is read by tools/loop_table.py. Each tracked file in one module; each module's dependencies, followed
transitively, covering its includes (each #include "..." resolved to the
tracked file it names, from its directory, and each #include <qiven/...>
or <qiven3/...> without it, from the build's include directories
and each module directory's include/ under foundation/, kits/ and
products/; one resolving to none refused but in third_party/, where they
are counted on one line) and, given a configured build tree, its links
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
import runpy
import sys
from pathlib import Path

# git's one door (tools/loop_git.py), by runpy: under -I this directory is off sys.path.
GIT = runpy.run_path(str(Path(__file__).resolve().with_name("loop_git.py")))
# The table's reading (tools/loop_table.py), by runpy too: its constants and functions this file's.
TABLE = runpy.run_path(str(Path(__file__).resolve().with_name("loop_table.py")))
globals().update((k, v) for k, v in TABLE.items() if k.isupper() or getattr(v, "__module__", "") == TABLE["__name__"])
LAYERS = ("foundation", "product", "tooling", "support", "whole-build", "no-test")
ORDER = ("devkit", "core", "system", "services", "kits", "products")  # the link graph's layers, lowest first
KIT = "foundation/devkit/test/"  # the test kit's table files: the runner, the case main, the faults kit, the scan
INCLUDE = re.compile(rb'^[ \t]*#[ \t]*include[ \t]*(?:"([^"]+)"|<((?:qiven|qiven3)/[^>]+)>)', re.M)  # angle: ours
ROOTS = tuple(TREE_FILE["included"])  # the include directories the build gives, and include_roots()'


def test_path(path, homes=()):
    """The one reader of test code, trees.cmake's qiven3_test_path in CMake:
    a path under the top-level tests/ as it stands; under the tests/ of a
    module directory, one of homes (each holding a table file), as the
    top-level tree would hold it, so that a file keeps its checks as it
    moves; any other path, None."""
    at = 0 if path.startswith(f"{TESTS}/") else path.find(f"/{TESTS}/")
    while at > 0 and path[:at] not in homes:
        at = path.find(f"/{TESTS}/", at + 1)
    return None if at < 0 else path[at + (at > 0):]


def under_tests(path, homes=()):
    """Whether a path is test code."""
    return test_path(path, homes) is not None


def fake(path):
    """Whether a path lies under a directory named fake or fake_*: a fake's."""
    return any(part == "fake" or part.startswith("fake_") for part in path.split("/")[:-1])


def owners(table, files):
    """{file: [the names of the modules whose paths hold it]}."""
    return {f: [m["name"] for m in table["modules"] if any(holds(p, f) for p in m["paths"])] for f in files}


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
    homes = table.get("homes", {})
    dirs = {home.rpartition("/")[0] for home in homes.values()}
    return {name for name, home in homes.items() if home.startswith(KIT)}, {
        m["name"] for m in table["modules"]
        if m["paths"] and all(under_tests(p, dirs) or p.endswith(("README.md", "module.json")) for p in m["paths"])}


def layer_rule(table):
    """refused(source, target): why the layer rule refuses an edge, or None.
    A row's layer is its table file's in the tree, never its own word; an edge goes down or stays in its
    layer, never from one product onto another's; the test kit's edges up to core, system and services are
    allowed, as nothing but a test reaches the kit; tools/ and the repository are outside the link graph.
    Test code alone reaches the test kit: a library's edge, in depends, onto the kit or onto test code
    alone refused (its path on to the kit named), but the kit's own and test code's."""
    homes = table.get("homes", {})
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
    refused, homes = layer_rule(table), table.get("homes", {})
    excepted = [(e["from"], to) for e in table.get("exceptions", ()) for to in e["to"]]
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


def include_roots(files):
    """ROOTS, then each module directory's include/ under TOPS that holds a tracked C++ file."""
    homes = {f.rpartition("/")[0] for f in files if is_table_file(f) and "/" in f and f.split("/", 1)[0] in TOPS}
    return ROOTS + tuple(sorted({f[:f.index("/include/")] + "/include" for f in files
                                 if "/include/" in f and f.endswith(CXX_SUFFIXES)
                                 and f[:f.index("/include/")] in homes}))


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
    kit, alone = test_alone(table)  # a library's closure stops at them: test code alone reaches them
    closure = {False: reach(table, ("depends",), kit | alone), True: reach(table)}
    whole = {m["name"] for m in table["modules"] if m["layer"] == "whole-build"}  # the build's, linked by all
    homes = {f.rpartition("/")[0] for f in files if is_table_file(f) and "/" in f}

    def test_code(f):
        return under_tests(f, homes)

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

    tracked, roots = set(files), include_roots(files)
    for f in sorted(files):
        if not f.endswith(CXX_SUFFIXES) or f not in module:
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
            elif found in module and not test_code(f) and test_code(found):
                errors.append(f"include {f} -> {found}: library code including test code")
            elif (found in module and test_code(f) and not f.startswith(f"{TESTS}/") and test_code(found)
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


def tracked(source):
    """The checkout's tracked files, by git's door."""
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
        print(__doc__.strip() + "\n\n" + TABLE["__doc__"].strip())
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
