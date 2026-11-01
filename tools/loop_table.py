"""The modules' table, read from its files: the modules' check's
reader (tools/loop_modules.py), which loads it by runpy and takes its
names.

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
are two table files or two rows of one name, a row not in the table file
of its name, a from or an of naming no row, a row's path holding no
tracked file, given twice, or outside its table file's directory but in
src/, tests/, third_party/, tools/ (the old places) and a whole-build
row's root files, a path of the root module.json's rows not the
repository's own at its root (whole-build rows alone: a file there or a
directory of no tree), a row's path in the old places outside its table
file's directory that its row in that file at the base commit (BASE,
tools/loop_base.py) neither gave nor holds, so that they only shrink,
a new module's, a new row's or a moved one's among them, a tracked file in a slot's
directory, or directly in a group's, but its README.md and module.json,
a built one's contract naming no tracked file,
an exception's until blank or its edge listed twice. Each table
directory's README.md repeats its module.json's fields in one form, each
line refused that differs: line 1 `# <name>`, line 3 `layer <layer>,
status <status>`, and a `- **Contract:**`, `- **Parameters:**` and
`- **May depend on:**` line for each of those keys the file has; a line
naming the consumers is refused (the table's reverse edges give them).
"""

import fnmatch
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]  # the checkout this file is in
# The trees, named once in tools/trees.json, which the root CMakeLists.txt also reads: its layers and
# analysed, the roots of the analysis preset's targets.
TREE_FILE = json.loads((ROOT / "tools" / "trees.json").read_text(encoding="utf-8"))
TOPS = tuple(TREE_FILE["layers"])  # the trees whose every module.json is a table file
TREES = (*TREE_FILE["walked"], *TOPS)  # the trees every check that walks the checkout walks
# The C++ suffixes, named there once too: a source's, a header's, and either; every rule and walker's.
SOURCE_SUFFIXES, HEADER_SUFFIXES = tuple(TREE_FILE["source_suffixes"]), tuple(TREE_FILE["header_suffixes"])
CXX_SUFFIXES = SOURCE_SUFFIXES + HEADER_SUFFIXES
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
EXCEPTION = ("from", "to", "until")  # an exception's keys: its row, the edges' targets, what removes it
OLD = ("src/", "tests/", "third_party/", "tools/")  # the places a row's paths may still lie in, outside its directory
BASE = "tools/loop_base.json"  # each table file's rows' old paths at the base commit it names (tools/loop_base.py)


def root_own(path):
    """Whether a path is the repository's own at its root: a file there, or a directory of none of the trees."""
    return "/" not in path.rstrip("/") and path.rstrip("/") not in TREES


def old_paths(home, row):
    """A row's paths in the old places (OLD) outside its table file's directory, home that file."""
    where = home.rpartition("/")[0]
    return [p for p in row["paths"] if where and p.startswith(OLD) and not p.startswith(where + "/")]


def old_rows(texts):
    """{table file: {row: its old paths}} of the table's files {path: text}, the rows of none left out: BASE's
    rows, of the files at the base."""
    found = {path: {row["name"]: old_paths(path, row) for row in json.loads(text).get("present", ())}
             for path, text in sorted(texts.items())}
    return {path: {n: p for n, p in rows.items() if p} for path, rows in found.items() if any(rows.values())}

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


def tree_layer(where):
    """A table file's directory's layer: foundation/<layer>/... gives its
    layer, kits/ and products/ theirs, the root, foundation/ and tools/
    their own."""
    parts = where.split("/")
    return {"": "repository", "foundation": "foundation", "tools": "tools"}.get(where) or (
        parts[1] if parts[0] == "foundation" else parts[0])


def fields(where, data, shallow):
    """A table file's fields of a wrong type or an unknown value, and its
    layer or name not its directory's (tree_layer()); a directory's name its
    name, but the deeper of two directories of one name takes its parent's
    as a prefix."""
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
    if "exceptions" in data and not (isinstance(data["exceptions"], list) and all(
            isinstance(e, dict) and set(e) == set(EXCEPTION) and isinstance(e["from"], str) and strings(e["to"])
            and e["to"] and isinstance(e["until"], str) and e["until"].strip() for e in data["exceptions"])):
        out.append(f"exceptions are not a list of objects of keys {', '.join(EXCEPTION)}, from a string, to a "
                   "list of strings, until words, neither empty")
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


def table_of(texts, files=None, readmes=None, base=None):
    """The table, {"modules": [rows]}, and its errors, from the table's
    files {path: text}; given the tracked files, what a slot's or a group's
    directory holds and a contract checked too; given {path: text} of the
    table directories' README.md, each checked against its module.json;
    given the base's old_rows(), each row's old paths held by its own there."""
    errors, rows, names, later, dirs, slots, excepted = [], [], {}, [], [], [], []
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
        lacks, adds = want - set(data), set(data) - want - {"slot": {"of"}, "built": {"exceptions"}}.get(status, set())
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
            for e in data.get("exceptions", ()):
                if e["from"] in [row["name"] for row in data["present"]]:
                    excepted.append({**e, "to": list(e["to"])})
                else:
                    errors.append(f"table file {path}: exception from {e['from']} names no row of its file")
            mine = [dict(row, paths=list(row["paths"])) for row in data["present"]]
            errors += [f"table file {path}: row {row['name']}: path {p} lies outside {where}/" for row in mine
                       for p in row["paths"] if where and not p.startswith((where + "/", *OLD))
                       and not ("/" not in p and row["layer"] == "whole-build")]
            errors += [f"table file {path}: row {row['name']}: path {p} is not the repository's own, at its root, "
                       "as a whole-build row's" for row in mine for p in row["paths"] if not where and not root_own(p)]
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
        elif names.get(row["name"], path) != path:
            errors.append(f"row {row['name']}: in {path}, not in {names[row['name']]}, the table file of its name")
        elif base is not None:
            given = base.get(path, {}).get(row["name"], ())
            errors += [f"row {row['name']}: path {p} in the old places, in {path}, not given it there at the base"
                       for p in old_paths(path, row) if not any(holds(q, p) for q in given)]
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
    homes = {}  # a row's table file, whose directory gives its layer in the tree
    for path, row in rows:
        homes.setdefault(row["name"], path)
    return {"modules": [row for _, row in rows], "homes": homes, "exceptions": excepted}, errors


def tree(root=ROOT, files=None):
    """table_of() over a checkout's table files and, given the tracked
    files, their directories' tracked README.md; and its BASE's rows, a
    tracked file when the tracked files are given, else refused (a tree
    without BASE, read with no tracked files, a test's, unchecked so)."""
    root = Path(root)
    paths = table_files(root)
    readmes = {p: (root / p).read_text(encoding="utf-8") for p in (q.replace("module.json", "README.md") for q in paths)
               if files is not None and p in files}
    held = (root / BASE).is_file() and (files is None or BASE in files)
    base = json.loads((root / BASE).read_text(encoding="utf-8"))["rows"] if held else None
    table, errors = table_of({p: (root / p).read_text(encoding="utf-8") for p in paths}, files, readmes, base)
    return table, errors if held or files is None else [f"{BASE}: not a tracked file: the old places unchecked",
                                                       *errors]


def load(root=ROOT):
    """A checkout's table, {"modules": [...]}; ValueError naming its first
    error, when it has any."""
    table, errors = tree(root)
    if errors:
        raise ValueError(f"the table: {errors[0]}" + (f", and {len(errors) - 1} more" if len(errors) > 1 else ""))
    return table


def holds(pattern, path):
    """Whether a table's path holds a file, or a path the base gave holds a row's: a directory's prefix, or a
    pattern, matched name by name, so that its * and ? never cross a / (a new directory under it held by none)."""
    if pattern.endswith("/"):
        return path.startswith(pattern)
    names, parts = pattern.split("/"), path.split("/")
    return len(names) == len(parts) and all(map(fnmatch.fnmatchcase, parts, names))
