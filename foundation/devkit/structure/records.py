"""The structure records: what the structure rules read, each written by a
step of the build that records it (steps.py), into the build's structure/
directory. The standard library, Python's lowest layer
(foundation/devkit/python, its process for the tools, its system's
output_text for their output), and the trees every check walks, its table's
(foundation/devkit/python/table.py).

- An object's record, objects/<key>.json, written by its compile: its
  includes as the compile's own /showIncludes reports them, its symbols
  (dumpbin /symbols), its source, target and compile command. A vendored
  file, tools/trees.json's vendored (foundation/system/sqlite/src/sqlite3.c
  the one compiled), has its object's names recorded, for what it defines,
  and no includes.
- names.json: the C and C++ runtimes' public names and the network's import
  libraries' (dumpbin /linkermember:1).
- targets.dot: the targets' link edges, CMake's --graphviz over a configure
  of the tree's sources with the tree's own cache, in a directory of its own.

Each file is written only where its content changes, so that a step reading
it reruns at a change of what it reads alone.
"""

import json
import os
import shutil
from collections import namedtuple
from pathlib import Path

from foundation.devkit.python import process
from foundation.devkit.python import system

# The runtimes a /MT build links, the static C runtime's (the trees with
# records); oldnames.lib, POSIX names, is not one.
RUNTIME_LIBS = ("libcmt.lib", "libcpmt.lib", "libvcruntime.lib", "libucrt.lib")
# The network's import libraries, so that an import from one is named.
NETWORK_LIBS = ("ws2_32.lib", "winhttp.lib", "wininet.lib")
TOOL_SECONDS = 900  # one tool's run: a compile, dumpbin, or CMake's configure

Object = namedtuple("Object", "source target includes undefined weak defined command vendored")


def run(command, cwd=None):
    """A tool's standard output, as text; SystemExit where it fails or passes its limit."""
    try:
        done = process.run(command, cwd=cwd, limit=TOOL_SECONDS)
    except process.TimedOut as e:
        raise SystemExit(f"records: {e}") from e
    if done.status != 0:
        text = system.output_text(done.out + done.err, "replace")
        raise SystemExit(f"records: {command[:3]} gave {done.status}:\n{text}")
    return system.output_text(done.out, "replace")


def rel_posix(path, root):
    try:
        return Path(os.path.normpath(path)).relative_to(root).as_posix()
    except ValueError:
        return None


def target_of(obj):
    """The target an object path's <target>.dir names."""
    return next((p[:-4] for p in Path(obj).as_posix().split("/") if p.endswith(".dir")), "")


def key_of(target, source, obj):
    return target + "__" + (source or Path(obj).name).replace("/", "_")


def write_if_changed(path, text):
    """Writes text to path where it is not there already; whether it wrote."""
    path = Path(path)
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")
    return True


def write_json(path, value):
    return write_if_changed(path, json.dumps(value, indent=1, sort_keys=True) + "\n")


def object_record(root, source, absolute, obj, command, shown, dumpbin, vendored_files):
    """One compiled object's record: its includes, from its compile's
    /showIncludes output (shown), and its symbols."""
    rel = rel_posix(source, root)
    vendored = rel in vendored_files
    undefined, weak, defined = read_symbols(run([dumpbin, "/nologo", "/symbols", str(obj)]))
    return {
        "source": rel, "absolute": absolute, "target": target_of(obj), "command": command, "vendored": vendored,
        "includes": [] if vendored else [list(i) for i in read_includes(shown, absolute)],
        "undefined": sorted(undefined), "weak": sorted(weak), "defined": sorted(defined),
    }


def read_object(path):
    entry = json.loads(Path(path).read_text(encoding="utf-8"))
    return Object(entry["source"], entry["target"], [tuple(i) for i in entry["includes"]], set(entry["undefined"]),
                  set(entry["weak"]), set(entry["defined"]), entry["command"], bool(entry["vendored"]))


def objects_by_target(root, out):
    """{target: [its objects' record paths]}, from the build's compile commands (the steps' copy in OUT)."""
    found = {}
    for entry in json.loads((Path(out) / "compile_commands.json").read_text(encoding="utf-8")):
        obj = Path(entry["directory"], entry["output"])
        target = target_of(obj)
        name = key_of(target, rel_posix(entry["file"], root), obj) + ".json"
        found.setdefault(target, []).append(Path(out) / "objects" / name)
    return found


def tree_records(root, out):
    """The Records of every object of the build whose structure/ directory
    is OUT, by its compile commands; None where the build makes none. An
    object with no record yet is left out."""
    if not (Path(out) / "compile_commands.json").is_file():
        return None
    paths = [p for listed in objects_by_target(root, out).values() for p in listed]
    return Records(root, [read_object(p) for p in paths if p.is_file()])


def find_lib(name):
    for directory in os.environ.get("LIB", "").split(os.pathsep):
        if directory and (Path(directory) / name).is_file():
            return Path(directory) / name
    raise SystemExit(f"records: {name} is on no directory of LIB; run from the toolset's environment")


def library_names(dumpbin):
    """{"runtime": names, "network": names}, each library's public names."""
    found = {}
    for part, names in (("runtime", RUNTIME_LIBS), ("network", NETWORK_LIBS)):
        found[part] = sorted(set().union(*(read_linker_member(run([dumpbin, "/nologo", "/linkermember:1",
                                                                   str(find_lib(n))])) for n in names)))
    return found


def graph(source, build_dir, scratch, cmake, defines):
    """The link edges' text: CMake's --graphviz over a configure of source
    in scratch, given defines (-DNAME:TYPE=VALUE), the tree's cache."""
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    # The compiler found once: CMake reads its own record of each compiler.
    for found in Path(build_dir, "CMakeFiles").glob("[0-9]*.[0-9]*"):
        if found.is_dir():
            shutil.copytree(found, scratch / "CMakeFiles" / found.name)
    dot = scratch / "targets.dot"
    run([cmake, "-S", str(source), "-B", str(scratch), "-G", "Ninja", "--graphviz=" + str(dot)] + list(defines))
    return dot.read_text(encoding="utf-8")


def read_includes(text, absolute):
    """(includer, included as written, depth) for each include, from /showIncludes."""
    found, stack = [], [absolute]
    prefix = "Note: including file:"
    for line in text.splitlines():
        if not line.startswith(prefix):
            continue
        rest = line[len(prefix):]
        depth = len(rest) - len(rest.lstrip(" "))
        path = rest.strip()
        if depth < 1 or depth > len(stack):
            raise ValueError(f"an include at depth {depth} under {absolute}")
        found.append((stack[depth - 1], path, depth))
        del stack[depth:]
        stack.append(path)
    return found


def read_symbols(text):
    undefined, weak, defined = set(), set(), set()
    for line in text.splitlines():
        if "|" not in line:
            continue
        left, _, right = line.partition("|")
        fields = left.split()
        if len(fields) < 4 or not right.split():
            continue
        name = right.split()[0]
        if "UNDEF" in fields:
            (weak if "WeakExternal" in fields else undefined).add(name)
        elif "External" in fields:
            defined.add(name)
    return undefined, weak, defined


def read_linker_member(text):
    names = set()
    for line in text.splitlines():
        fields = line.split()
        if len(fields) == 2 and all(c in "0123456789ABCDEF" for c in fields[0]):
            names.add(fields[1])
    return names


def read_graph(text):
    """Each node's shape, by name, and the direct edges."""
    nodes, labels, edges = {}, {}, {}
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('"node') and "label = " in line:
            node = line.split('"')[1]
            label = line.split('label = "', 1)[1].split('"', 1)[0]
            labels[node] = label
            nodes[label] = line.split("shape = ", 1)[1].split()[0].rstrip("];,")
        elif line.startswith('"node') and " -> " in line:
            a, b = line.split('"')[1], line.split('"')[3]
            edges.setdefault(labels[a], set()).add(labels[b])
    return nodes, edges


def command_include_dirs(command):
    """The /I and -I directories of a compile command."""
    words, word, quoted = [], [], False
    for c in command + " ":
        if c == '"':
            quoted = not quoted
        elif c == " " and not quoted:
            if word:
                words.append("".join(word))
            word = []
        else:
            word.append(c)
    return [w[2:] for w in words if w[:2] in ("/I", "-I") and len(w) > 2]


class Records:
    """Records of a set of objects, with the runtimes' and the network's
    names and the link edges where a rule reads them."""

    def __init__(self, root, objects, names=None, graph_text=None):
        self.root = Path(root)
        self.objects = list(objects)
        self.runtime = set((names or {}).get("runtime", ()))
        self.network = set((names or {}).get("network", ()))
        self.shapes, self.edges = read_graph(graph_text) if graph_text else ({}, {})

    def closure(self, target):
        seen, todo = set(), [target]
        while todo:
            for dep in self.edges.get(todo.pop(), ()):
                if dep not in seen:
                    seen.add(dep)
                    todo.append(dep)
        return seen
