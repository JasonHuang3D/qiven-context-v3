"""The checker of the structure rules, B1, B2, B5 to B17 and B32, whose
lists lists.py holds; it also runs the safe subset's token rules, B18 to B31
(subset.py).

A rule reads includes, names and link edges from the compiler's and the
build's records (records.py writes and reads them), and strings,
identifiers and #include directives from the sources by the rules' lexer
(lexer.py), and the tree's files by tree.py; no rule reads C++ by a
regular expression. The text rules are
B1's, a substring search for sqlite3, and B32's, a search of each text
file's bytes for those git cannot fix. check() applies every rule to v3's
tree and gives its findings.
"""

import os
from collections import namedtuple
from pathlib import Path

from foundation.devkit.structure import layers, subset
from foundation.devkit.structure.lexer import lex
from foundation.devkit.structure.lists import (
    ADAPTER_HEADERS, ALLOWANCES, ALLOWED, B12_FILES, B12_HEADERS, B12_LINKS, B12_TARGETS, B13_FILES, B13_HEADERS,
    B13_LINKS, B13_OPEN, B13_SECRET, B13_TARGETS, B17_HEADERS, B17_NAMES, B32_BYTES, CONTRACT, CORE_NAMED,
    CRT_IO_HEADERS, CXX_SOURCES, ENGINE, EXEMPT, FS_PREFIX, FS_READS, GIT_COMMANDS, GIT_LITERALS, GIT_LOWER_PREFIXES,
    LIBRARY_SHAPES, LINKER_NAMES, LOAD_NAMES, LOAD_PREFIX, NET_HEADERS, NET_LIBS, NOT_CORE, NOT_CORE_TARGETS,
    PLATFORM_WRITES, PROCESS_NAMES, SDK_DIRS, SPAWN_EXEC, STD_HEADERS, STREAM_MODES, STREAM_TYPES, WRITE_CLASSES,
    WRITE_NAMES)
from foundation.devkit.structure.records import Records, command_include_dirs
# The tree's files (tree.py), which the size, line-end, content and import tests read here too.
from foundation.devkit.structure.tree import is_text, line_count, source_files, text_attributes

ROOT = Path(__file__).resolve().parents[3]
VENDORED = "foundation/system/sqlite/src/sqlite3.c"
# The runner's directory, from the table: its headers every case includes.
RUNNER = layers.of(ROOT).directory("runner")

Finding = namedtuple("Finding", "rule where kind detail")


def under(path, prefixes):
    """A path under one of prefixes; a prefix with no / is a target's name, matched whole."""
    return path is not None and any(path == p or ("/" in p and path.startswith(p)) for p in prefixes)


def allowed(rule, where, what=None):
    if under(where, ALLOWED.get(rule, ())):
        return True
    return any(r == rule and match(where, f) and match(what, w) for r, f, w in ALLOWANCES)


def match(value, key):
    return value is not None and (value == key or value.startswith(key + "::")
                                  or (key.endswith(("/", ".")) and value.startswith(key)))


def is_instantiation(path):
    return path is not None and path.endswith(layers.SOURCES) and path.rsplit("/", 1)[-1].startswith("instances")


def is_core(path, tree):
    """Core logic, by layer in the tree's table: a library's file of a
    module of core to products (layers.Layers.library()), not devkit's, layer
    0, nor test code nor a vendor's; the adapters, current, compute and
    diag's files that call the system apart (NOT_CORE), and the
    instantiation files."""
    return tree.library(path) and not under(path, NOT_CORE) and not is_instantiation(path)


# Reading the records.


def classify(path, root):
    """An included file: ("v3", relative path), ("sdk"|"std"|"other", its name)."""
    normal = os.path.normpath(path)
    rel = relative(normal, root)
    if rel is not None:
        return "v3", rel
    parts = normal.replace("\\", "/").lower().split("/")
    name = parts[-1]
    if "windows kits" in parts and "include" in parts:
        sub = parts[parts.index("include") + 2] if len(parts) > parts.index("include") + 2 else ""
        if sub in SDK_DIRS:
            return "sdk", name
    if name in STD_HEADERS:
        return "std", name
    return "other", "/".join(parts[-2:]) if parts[-2:-1] == ["sys"] else name


def relative(path, root):
    try:
        return Path(path).relative_to(root).as_posix()
    except ValueError:
        return None


# The checker.
class Findings:
    """The findings over one tree, and that tree's table (layers.Layers),
    v3's when none is given."""

    def __init__(self, tree=None):
        self.found = set()
        self.tree = tree or layers.of(ROOT)

    def add(self, rule, where, kind="", detail="", what=None, force=False):
        """A finding, but where the rule allows it of where or what, as they
        stand or as the top-level tests/ would hold them."""
        places = (where, self.tree.as_tests(where))
        if force or not (any(allowed(rule, w, x) for w in places for x in (what, self.tree.as_tests(what)))
                         or any(under(w, EXEMPT.get(rule, ())) for w in places)):
            self.found.add(Finding(rule, where, kind, detail))


def is_goldens(value):
    parts = value.replace("\\", "/").split("/")
    return any(a == "tests" and b == "goldens" for a, b in zip(parts, parts[1:]))


def is_git(value):
    """B2's string: git's program, a git command or an environment name, in
    any case, as Windows finds a program ("Git", "GIT.EXE")."""
    value = value.lower()
    return value in GIT_LITERALS or value.startswith(GIT_LOWER_PREFIXES) or value in GIT_COMMANDS


def app_namespace(tokens, i):
    """Whether tokens[i], `app` after `::`, names the binary's namespace,
    qiven3::app, not a stream's mode: `qiven3` before the `::`, written as
    one qualified name (namespace qiven3::app, qiven3::app::main); nested,
    `app` follows no `::`."""
    return tokens[i][1] == "app" and i >= 2 and tokens[i - 2][0:2] == ("id", "qiven3")


def source_rules(root, files, out):
    """B1's text, B2's and B14's strings, B13's and B17's tokens, B15's lines,
    B32's bytes in every file .gitattributes treats as text.
    Gives the files holding a goldens path, for B14's writes."""
    goldens = set()
    attributes = text_attributes(root)
    for rel in files:
        data = (root / rel).read_bytes()
        name = rel.rsplit("/", 1)[-1]
        if name.endswith((*CXX_SOURCES, ".py", ".cmake")) or name == "CMakeLists.txt":
            if line_count(data) > 600 and rel not in layers.VENDORED:
                out.add("B15", rel, detail=f"{line_count(data)} lines")
        found = B32_BYTES.search(data) if is_text(rel, attributes) else None
        if found:
            line = data.count(b"\n", 0, found.start()) + 1
            what = "a lone CR" if found.group() == b"\r" else f"the control byte 0x{found.group()[0]:02x}"
            out.add("B32", rel, detail=f"line {line}: {what}")
        text = data.decode("utf-8", "replace")
        if layers.layered(rel) and "sqlite3" in text:
            out.add("B1", rel, detail="the text sqlite3", what="sqlite3")
        if not rel.endswith(CXX_SOURCES) or rel == VENDORED:
            continue
        tokens = lex(text)[0]
        for i, (kind, value, line) in enumerate(tokens):
            if kind == "str" and is_git(value):
                out.add("B2", rel, detail=f"line {line}: {value!r}")
            if kind == "str" and is_goldens(value):
                goldens.add(rel)
                out.add("B14", rel, detail=f"line {line}: {value!r}")
            if kind != "id" or i == 0:
                continue
            before = tokens[i - 1][1]
            after = tokens[i + 1][1] if i + 1 < len(tokens) else ""
            if rel.startswith("src/reader/") and value in PLATFORM_WRITES and before in (".", "->") \
                    and after == "(":
                out.add("B13", rel, detail=f"line {line}: a platform call that writes, {value}")
            if (is_core(rel, out.tree) or is_instantiation(rel)) and (
                    (value in STREAM_MODES and before == "::" and not app_namespace(tokens, i))
                    or value in STREAM_TYPES):
                out.add("B17", rel, "mode", f"line {line}: a stream's writing mode, {value}")
    return goldens


def include_rules(records, out):
    root, seen = records.root, set()
    for obj in records.objects:
        if obj.vendored:
            continue
        checked = is_core_object(obj, out.tree) or is_instantiation(obj.source)
        system = []
        for includer, path, depth in obj.includes:
            kind, name = classify(path, root)
            if kind == "sdk":
                system.append((depth, name))
            by = relative(os.path.normpath(includer), root)
            if by is None or by == VENDORED or (by, path) in seen:
                continue
            seen.add((by, path))
            direct_include(by, path, kind, name, out)
        if checked and system:
            depth, name = min(system)
            out.add("B17", obj.source, "os", f"{len(system)} operating system headers, {name} at depth {depth}")
        for directory in command_include_dirs(obj.command):
            if ".." in directory.replace("\\", "/").split("/"):
                out.add("B11", obj.source, detail=f"the include directory {directory}")


def directives(tokens):
    """Each #include's header as written: ('"', name) or ('<', name)."""
    found = []
    for i in range(len(tokens) - 2):
        if tokens[i][1] != "#" or tokens[i + 1][1] != "include" or tokens[i][2] != tokens[i + 1][2]:
            continue
        head = tokens[i + 2]
        if head[0] == "header":
            found.append(('"', head[1]))
        elif head[1] == "<":
            parts = []
            for kind, value, line in tokens[i + 3:]:
                if value == ">" or line != head[2]:
                    break
                parts.append(value)
            found.append(("<", "".join(parts)))
    return found


def directive_rules(records, root, files, out):
    """The direct-include rules on each file's #include directives, read by
    the lexer: /showIncludes prints a header at its first inclusion in a
    translation unit alone, so a directive naming a header already reached
    leaves no record. A quoted name resolves from the file's directory, then
    from the build's include directories under the tree; a name the compiler
    resolved elsewhere takes the kind of the file it resolved to."""
    roots = sorted({rel for obj in records.objects for d in command_include_dirs(obj.command)
                    if (rel := relative(os.path.normpath(d), root)) is not None})
    resolved = {}
    for obj in records.objects:
        for _, path, _ in obj.includes:
            resolved.setdefault(path.replace("\\", "/").lower().rsplit("/", 1)[-1], path)
    for by in files:
        if not by.endswith(CXX_SOURCES) or by == VENDORED:
            continue
        for form, name in directives(lex((root / by).read_text(encoding="utf-8", errors="replace"))[0]):
            here = by.rsplit("/", 1)[0] if "/" in by else "."
            candidates = [here] + roots if form == '"' else []
            found = next((root / base / name for base in candidates if (root / base / name).is_file()), None)
            if found is not None:
                kind, what = classify(str(found), root)
            elif name.lower() in STD_HEADERS or name.lower().rsplit("/", 1)[-1] not in resolved:
                kind, what = ("std", name.lower()) if name.lower() in STD_HEADERS else ("other", name.lower())
            else:
                kind, what = classify(resolved[name.lower().rsplit("/", 1)[-1]], root)
            direct_include(by, name, kind, what, out)


def direct_include(by, path, kind, name, out):
    """The rules on one include by one of v3's files."""
    base = name.rsplit("/", 1)[-1]
    rel = name if kind == "v3" else None
    if base == "sqlite3.h":
        out.add("B1", by, detail="includes sqlite3.h")
    if kind == "sdk":
        out.add("B5", by, detail=f"a Windows SDK header, {name}", what=base)
    if kind != "v3" and base in CRT_IO_HEADERS:
        out.add("B6", by, detail=f"the C runtime's {name}", what=base)
    if kind != "v3" and name.endswith(NET_HEADERS):
        out.add("B8", by, detail=f"the network's {name}")
    if kind == "other" and base != "sqlite3.h":
        out.add("B9", by, detail=f"an include from outside, {name}", what=base)
    if rel is not None and not (out.tree.library(rel) or rel.endswith("/sqlite3.h")):
        out.add("B9", by, detail=f"an include outside a library of core to products, {rel}", what=rel)
    if ".." in path.replace("\\", "/").split("/"):
        out.add("B11", by, detail=f"an include path holding .., {path}")
    if rel is not None:
        if under(by, B12_FILES) and under(rel, B12_HEADERS):
            out.add("B12", by, detail=f"includes {rel}")
        if under(by, B13_FILES) and not under(by, B13_SECRET) and under(rel, B13_HEADERS) \
                and not rel.startswith(B13_OPEN):
            out.add("B13", by, detail=f"includes {rel}")
        if rel == "src/qiven3/mapcheck.hpp":
            out.add("B16", by, detail="includes the map check")
    if is_core(by, out.tree) or is_instantiation(by):
        if kind == "std":
            ok = name in B17_HEADERS
        elif kind == "v3":
            ok = (rel == CONTRACT or (is_instantiation(by) and rel in ADAPTER_HEADERS)
                  or (rel.endswith("/sqlite3.h") and by.startswith(ENGINE + "src/"))
                  or ((out.tree.library(rel) or out.tree.test_code(rel))
                      and not rel.startswith(("foundation/system/platform/", "foundation/services/compute/"))))
        else:
            ok = False
        if not ok:
            out.add("B17", by, "include", f"a direct include off the list, {name}", what=name)


def strip_imp(name):
    return name[len("__imp_"):] if name.startswith("__imp_") else name


def is_core_object(obj, tree):
    return is_core(obj.source, tree) and obj.target not in NOT_CORE_TARGETS


def b17_name(full):
    bare = strip_imp(full)
    return (bare in B17_NAMES or bare.startswith(SPAWN_EXEC) or any(c in full for c in WRITE_CLASSES)
            or (bare.startswith(FS_PREFIX) and bare not in FS_READS))


def name_rules(records, goldens, out):
    objects = records.objects
    core_defined = set().union(*(o.defined for o in objects if is_core_object(o, out.tree) or o.vendored))
    every_defined = set().union(*(o.defined for o in objects))
    by_target = {}
    for o in objects:
        by_target.setdefault(o.target, set()).update(o.defined)
    for obj in objects:
        if obj.vendored:
            continue
        src, names = obj.source, obj.undefined | obj.weak
        for full in sorted(names):
            bare = strip_imp(full)
            if bare == "CreateProcessW":
                out.add("B2", src, detail="CreateProcessW")
                if not under(src, ("foundation/system/platform/src/windows/process.",)):
                    out.add("B7", src, detail="CreateProcessW outside win/process", force=True)
            if bare == "_locking":
                out.add("B6", src, detail="_locking")
            if bare in PROCESS_NAMES or bare.startswith(SPAWN_EXEC):
                out.add("B7", src, detail=f"a process by another route, {bare}")
            if bare in LOAD_NAMES or bare.startswith(LOAD_PREFIX):
                out.add("B10", src, detail=bare)
            if full.startswith("__imp_") and full not in records.runtime and full not in every_defined:
                out.add("B5", src, detail=f"an import from a Windows library, {bare}", what=full)
            if full in records.network:
                out.add("B8", src, detail=f"an import from the network's libraries, {bare}")
            if src in goldens and (bare in WRITE_NAMES or b17_name(full)):
                out.add("B14", src, detail=f"a write in a file naming the goldens, {bare}", force=True)
        if not (is_core_object(obj, out.tree) or is_instantiation(src)):
            continue
        known = core_defined | records.runtime
        if is_instantiation(src):
            for target in records.closure(obj.target) | {obj.target}:
                known = known | by_target.get(target, set())
        for full in sorted(names):
            if b17_name(full):
                out.add("B17", src, "name", f"a name off the list, {strip_imp(full)}")
            elif full in obj.undefined and full not in known and full not in LINKER_NAMES:
                out.add("B17", src, "name", f"a name no core object or runtime defines, {full}")


def link_rules(records, out):
    objects, shapes = records.objects, records.shapes
    core = {o.target for o in objects if is_core_object(o, out.tree) and shapes.get(o.target) in LIBRARY_SHAPES}
    core = (core | CORE_NAMED) - NOT_CORE_TARGETS
    owners = {}
    for o in objects:
        owners.setdefault(o.target, set()).add(o.source)
    child = {t for t, sources in owners.items() if any(under(out.tree.as_tests(s), (layers.CHILD,)) for s in sources)}
    mapcheck = {t for t, sources in owners.items() if any(under(s, ALLOWED["B16"]) for s in sources)}
    for target, deps in sorted(records.edges.items()):
        for dep in sorted(deps):
            if dep == "qiven_sqlite" and target != "qiven_storage_sqlite":
                out.add("B1", target, detail="links qiven_sqlite")
            if shapes.get(dep) == "septagon" and target not in child:
                out.add("B5", target, detail=f"links the system library {dep}")
            if dep.lower().removesuffix(".lib") in NET_LIBS:
                out.add("B8", target, detail=f"links {dep}")
            if dep == "qiven3_mapcheck" and target not in mapcheck:
                out.add("B16", target, detail="links qiven3_mapcheck")
            if target in core and dep != "qiven_platform" and dep not in core:
                out.add("B17", target, "link", f"a core target links {dep}", force=True)
    for target in sorted(shapes):
        reached = records.closure(target)
        if target in B12_TARGETS and any(d in B12_LINKS for d in reached):
            out.add("B12", target, detail="reaches backup or forgetting")
        if target in B13_TARGETS and any(d in B13_LINKS for d in reached):
            out.add("B13", target, detail="the reader's target reaches " + ", ".join(
                sorted(d for d in reached if d in B13_LINKS)))


def check(records, root=None):
    """Every rule over one tree: its findings."""
    root = Path(root or records.root)
    out = Findings(layers.of(root))
    files = source_files(root)
    goldens = source_rules(root, files, out)
    include_rules(records, out)
    directive_rules(records, root, files, out)
    name_rules(records, goldens, out)
    link_rules(records, out)
    subset.check(root, files, lambda rule, where, kind, detail, what: out.add(rule, where, kind, detail, what),
                 out.tree)
    return out.found


def records_directory():
    """The structure build's records: this build's, else the structure preset's."""
    build = os.environ.get("QIVEN_BUILD")
    if build and (Path(build) / "structure" / "index.json").is_file():
        return Path(build) / "structure"
    return ROOT / "build" / "structure" / "structure"


def findings():
    """The findings over v3's tree, from the structure build's records,
    which must be there and current."""
    directory = records_directory()
    if not (directory / "index.json").is_file():
        raise AssertionError(f"no structure records at {directory}: build the structure preset first")
    records = Records(directory)
    stale = records.stale()
    if stale:
        raise AssertionError("the structure records are older than " + ", ".join(stale[:5])
                             + ": build the structure preset again")
    return check(records, ROOT)
