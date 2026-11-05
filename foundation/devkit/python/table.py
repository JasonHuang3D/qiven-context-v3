"""The module table, read from its files under a checkout's root: the one reader, of no check. Every path here is
given by root or by root's tools/trees.json, the repository's data, which also names the trees the root
CMakeLists.txt reads.

- trees(root): that file, a Trees: the fields it gives; every, the trees every walk of the checkout walks;
  cxx_suffixes; test_path(path, homes), the one reader of test code, trees.cmake's qiven_test_path in CMake;
  is_table_file(path); table_files(root).
- tree(root): (the table, its errors). load(root): the table; ValueError naming its first error.
- owners(table, files): each file's modules. holds(pattern, path): whether a table's path holds a file.

The table is {"modules": [rows], "homes": {row: its table file}, "exceptions": [...], "files": {table file: its
data}, "trees": its Trees}. A row's keys are ROW. A table file that does not parse, lacks or adds a key for its
status, or gives a field of a wrong type is an error, and gives no row; so are two table files or two rows of one
name, a row not in the table file of its name, and a from or an of naming no row. Its rows: each built file's
present rows, its own README.md and module.json (the root's module.json alone) added to its first row, or to the
first row its from names when it has none; a slot's directory a row of the no-test layer, and a group's README.md
and module.json one. What the table's words mean against the tree, the build and the base is the modules' check's
(foundation/devkit/structure/modules.py).
"""

import fnmatch
import json
from collections import namedtuple
from pathlib import Path

TREES_FILE = "tools/trees.json"
KEYS = {  # a table file's keys by its status; a built one with no present rows has from, a slot may have of
    "built": ("name", "layer", "status", "platforms", "contract", "adopted", "parameters", "present"),
    "slot": ("name", "layer", "status", "kind", "may_depend", "platforms", "adopted", "parameters"),
    "group": ("name", "layer", "status", "adopted"),
}
ROW = ("name", "layer", "depends", "test_depends", "paths")  # a present row's keys
PARAMETER = ("name", "default", "built")  # a parameter's keys
EXCEPTION = ("from", "to", "until")  # an exception's keys: its row, the edges' targets, what removes it


class Trees(namedtuple("Trees", ("layers", "walked", "analysed", "included", "tests", "support", "child", "vendored",
                                 "tables", "source_suffixes", "header_suffixes"))):
    """A checkout's trees.json, each list a tuple: layers, the trees whose every module.json is a table file; walked,
    the old trees; tests, the top-level test tree and the name of a module's own; vendored, a vendor's own files;
    tables, the table files outside the layers' trees."""

    __slots__ = ()

    @property
    def every(self):
        """The trees every check that walks the checkout walks."""
        return (*self.walked, *self.layers)

    @property
    def cxx_suffixes(self):
        """A C++ source's and a header's."""
        return self.source_suffixes + self.header_suffixes

    def test_path(self, path, homes=()):
        """A path under the top-level tests/ as it stands; under the tests/ of a module directory, one of homes (each
        holding a table file), as the top-level tree would hold it, so that a file keeps its checks as it moves; any
        other path, None."""
        at = 0 if path.startswith(f"{self.tests}/") else path.find(f"/{self.tests}/")
        while at > 0 and path[:at] not in homes:
            at = path.find(f"/{self.tests}/", at + 1)
        return None if at < 0 else path[at + (at > 0):]

    def is_table_file(self, path):
        """Whether a checkout's path, from its root, is one of the table's files."""
        return path in self.tables or (path.endswith("/module.json") and path.split("/", 1)[0] in self.layers)

    def table_files(self, root):
        """The table's files under a checkout's root, from it, sorted."""
        root = Path(root)
        found = [p.relative_to(root).as_posix() for top in self.layers if (root / top).is_dir()
                 for p in (root / top).rglob("module.json") if p.is_file()]
        return sorted(found + [p for p in self.tables if (root / p).is_file()])


def trees(root):
    """root's trees.json, a Trees. The system's OSError where it cannot be read; ValueError where it does not parse
    or lacks or adds a field."""
    text = (Path(root) / TREES_FILE).read_text(encoding="utf-8")
    try:
        return Trees(**{key: tuple(value) if isinstance(value, list) else value
                        for key, value in json.loads(text).items()})
    except (ValueError, TypeError, AttributeError) as error:
        raise ValueError(f"table: {TREES_FILE}: {error}") from error


def _strings(value):
    """Whether value is a list of strings."""
    return isinstance(value, list) and all(isinstance(v, str) for v in value)


def _wrong_shape(data):
    """A table file's fields of a wrong type: nothing else is read of them."""
    out = []
    for key in ("platforms", "may_depend", "from"):
        if key in data and not _strings(data[key]):
            out.append(f"{key} {data[key]!r} is not a list of strings")
    for key in ("kind", "of", "name", "layer"):
        if key in data and not isinstance(data[key], str):
            out.append(f"{key} {data[key]!r} is not a string")
    if not isinstance(data.get("contract"), (str, type(None))):
        out.append(f"contract {data['contract']!r} is not a string or null")
    if "parameters" in data and not (isinstance(data["parameters"], list) and all(
            isinstance(p, dict) and set(p) == set(PARAMETER) and isinstance(p["name"], str)
            and isinstance(p["built"], bool) for p in data["parameters"])):
        out.append(f"parameters are not a list of objects of keys {', '.join(PARAMETER)}, its name a string, "
                   "built true or false")
    if "exceptions" in data and not (isinstance(data["exceptions"], list) and all(
            isinstance(e, dict) and set(e) == set(EXCEPTION) and isinstance(e["from"], str) and _strings(e["to"])
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
                if not (isinstance(row[key], str) if key in ("name", "layer") else _strings(row[key]))]
    return out


def own_paths(path):
    """A table file's own: its directory's README.md and itself; the root's module.json alone."""
    where = path.rpartition("/")[0]
    return [f"{where}/README.md", f"{where}/module.json"] if where else [path]


def _read(texts, given):
    """The table and its errors, from the table's files {path: text}."""
    errors, rows, names, later, files, slots, excepted = [], [], {}, [], {}, [], []
    for path in sorted(texts):
        where, own = path.rpartition("/")[0], own_paths(path)
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
        wrong = _wrong_shape(data)
        if wrong:
            errors += [f"table file {path}: {line}" for line in wrong]
            continue
        files[path] = data
        if "of" in data:
            slots.append((path, data["of"]))
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
            if mine:
                mine[0]["paths"] += own
            else:
                later.append((path, data["from"], own))
            rows += [(path, row) for row in mine]
        else:
            paths = [where + "/"] if status == "slot" else own
            rows.append((path, {"name": data["name"], "layer": "no-test", "depends": [], "test_depends": [],
                                "paths": paths}))
    named = {}
    for path, row in rows:
        if row["name"] in named:
            errors.append(f"row {row['name']}: in {named[row['name']][0]} and {path}")
        elif names.get(row["name"], path) != path:
            errors.append(f"row {row['name']}: in {path}, not in {names[row['name']]}, the table file of its name")
        named.setdefault(row["name"], (path, row))
    errors += [f"table file {path}: of {of} names no row" for path, of in slots if of not in named]
    for path, names_from, own in later:
        missing = [name for name in names_from if name not in named]
        errors += [f"table file {path}: from {name} names no row" for name in missing]
        if names_from and not missing:
            named[names_from[0]][1]["paths"] += own
    homes = {}  # a row's table file, whose directory gives its layer in the tree
    for path, row in rows:
        homes.setdefault(row["name"], path)
    return {"modules": [row for _, row in rows], "homes": homes, "exceptions": excepted, "files": files,
            "trees": given}, errors


def tree(root):
    """(a checkout's table, the errors its table files give): a file in error gives no row, and the rest are read."""
    root, given = Path(root), trees(root)
    return _read({p: (root / p).read_text(encoding="utf-8") for p in given.table_files(root)}, given)


def load(root):
    """A checkout's table; ValueError naming its first error, when it has any."""
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


def owners(table, files):
    """{file: [the names of the modules whose paths hold it]}."""
    return {f: [m["name"] for m in table["modules"] if any(holds(p, f) for p in m["paths"])] for f in files}
