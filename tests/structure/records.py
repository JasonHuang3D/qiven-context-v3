"""The structure records: what the structure tests read, written by the
structure preset's build. Standard library only.

Run after every target is built, as

    python -I tests/structure/records.py SOURCE BUILD CMAKE DUMPBIN

it writes BUILD/structure/: for each compile command of
compile_commands.json, its includes as the compiler reports them
(the command rerun with /showIncludes /Zs: Ninja consumes the build's own)
and its object's symbols (dumpbin /symbols); the C and C++ runtimes'
public names (dumpbin /linkermember:1); the targets' link edges (CMake's
--graphviz, from a configure of SOURCE in a directory of its own); and
index.json, naming each. It then plants a CMake project in
BUILD/structure/plant/, one file or target per rule broken (plants.py, run
as a script of its own), builds it and writes its records the same way, so
that the tests run the same checker over it. third_party/sqlite/sqlite3.c,
the one vendored file, has its object's names recorded, for what it
defines, and no includes.
"""

import concurrent.futures
import hashlib
import json
import os
import shutil
import subprocess
import sys
from collections import namedtuple
from pathlib import Path

VENDORED = "third_party/sqlite/sqlite3.c"
# The runtimes a /MT build links, the static C runtime's (the structure
# build's); oldnames.lib, POSIX names, is not one.
RUNTIME_LIBS = ("libcmt.lib", "libcpmt.lib", "libvcruntime.lib", "libucrt.lib")
# The network's import libraries, so that an import from one is named.
NETWORK_LIBS = ("ws2_32.lib", "winhttp.lib", "wininet.lib")


def run(command, cwd=None, shell_text=False):
    done = subprocess.run(command, cwd=cwd, capture_output=True, shell=False)
    if done.returncode != 0:
        text = (done.stdout + done.stderr).decode("mbcs", "replace")
        raise SystemExit(f"records: {command if shell_text else command[:3]} gave {done.returncode}:\n{text}")
    return done.stdout.decode("mbcs", "replace")


def rel_posix(path, root):
    try:
        return Path(os.path.normpath(path)).relative_to(root).as_posix()
    except ValueError:
        return None


def record_one(entry, root, out, dumpbin):
    """One compile command: its includes and its object's symbols."""
    source = rel_posix(entry["file"], root)
    obj = Path(entry["directory"], entry["output"])
    parts = obj.as_posix().split("/")
    target = next((p[:-4] for p in parts if p.endswith(".dir")), "")
    key = target + "__" + (source or obj.name).replace("/", "_")
    vendored = source == VENDORED
    if not vendored:
        includes = run(entry["command"] + " /showIncludes /Zs", cwd=entry["directory"], shell_text=True)
        (out / "includes" / (key + ".txt")).write_text(includes, encoding="utf-8")
    symbols = run([dumpbin, "/nologo", "/symbols", str(obj)])
    (out / "symbols" / (key + ".txt")).write_text(symbols, encoding="utf-8")
    return {
        "source": source, "absolute": entry["file"], "target": target,
        "command": entry["command"], "includes": "includes/" + key + ".txt",
        "symbols": "symbols/" + key + ".txt", "vendored": vendored,
    }


def find_lib(name):
    for directory in os.environ.get("LIB", "").split(os.pathsep):
        if directory and (Path(directory) / name).is_file():
            return Path(directory) / name
    raise SystemExit(f"records: {name} is on no directory of LIB; run from the toolset's environment")


def graph(source, build_dir, out, cmake, cache):
    """The link edges: CMake's --graphviz over a configure of its own."""
    scratch = out / "graph-build"
    shutil.rmtree(scratch, ignore_errors=True)
    scratch.mkdir(parents=True)
    # The compiler found once: CMake reads its own record of each compiler.
    version = Path(build_dir, "CMakeFiles")
    for found in version.glob("[0-9]*.[0-9]*"):
        if found.is_dir():
            shutil.copytree(found, scratch / "CMakeFiles" / found.name)
    (out / "graph").mkdir(exist_ok=True)
    run([cmake, "-S", str(source), "-B", str(scratch), "-G", "Ninja",
         "--graphviz=" + str(out / "graph" / "targets.dot")] + [f"-D{k}={v}" for k, v in cache.items()])


def cache_values(build_dir, names):
    found = {}
    for line in Path(build_dir, "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        name, _, rest = line.partition(":")
        if name in names and "=" in rest:
            found[name] = rest.split("=", 1)[1]
    return found


def record(source, build_dir, out, dumpbin):
    """The include and symbol records of every compile command of build_dir."""
    shutil.rmtree(out, ignore_errors=True)
    for sub in ("includes", "symbols", "runtime"):
        (out / sub).mkdir(parents=True)
    root = Path(os.path.normpath(source))
    db = json.loads(Path(build_dir, "compile_commands.json").read_text(encoding="utf-8"))
    entries = list(db)
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        objects = list(pool.map(lambda e: record_one(e, root, out, dumpbin), entries))
    libs = {}
    for name in RUNTIME_LIBS + NETWORK_LIBS:
        text = run([dumpbin, "/nologo", "/linkermember:1", str(find_lib(name))])
        (out / "runtime" / (name + ".txt")).write_text(text, encoding="utf-8")
        libs[name] = "runtime/" + name + ".txt"
    return {"source_root": str(root), "objects": objects, "libraries": libs,
            "runtime": list(RUNTIME_LIBS), "network": list(NETWORK_LIBS),
            "graph": "graph/targets.dot"}


def digests(root):
    """Each source file's SHA-256, so that a test finds the records stale."""
    found = {}
    for top in ("src", "tests", "third_party"):
        for path in sorted(Path(root, top).rglob("*")):
            if path.suffix in (".cpp", ".hpp", ".c", ".h") and path.is_file():
                found[path.relative_to(root).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    return found


def write_index(out, index):
    (out / "index.json").write_text(json.dumps(index, indent=1, sort_keys=True), encoding="utf-8")


def main(argv):
    source, build_dir, cmake, dumpbin = (Path(a) for a in argv[1:5])
    out = build_dir / "structure"
    index = record(source, build_dir, out, dumpbin)
    names = ("CMAKE_BUILD_TYPE", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_MAKE_PROGRAM", "QIVEN3_HAL")
    graph(source, build_dir, out, cmake, cache_values(build_dir, names))
    index["digests"] = digests(source)
    plant_out = out / "plant"
    tree = plant_out / "tree"
    run([sys.executable, "-I", str(Path(__file__).with_name("plants.py")), str(tree)])
    plant_build = plant_out / "build"
    run([cmake, "-S", str(tree), "-B", str(plant_build), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "--graphviz=" + str(plant_out / "graph.dot")]
        + [f"-D{k}={v}" for k, v in cache_values(build_dir, names[1:4]).items()])
    run([cmake, "--build", str(plant_build)])
    plant_index = record(tree, plant_build, plant_out / "records", dumpbin)
    (plant_out / "records" / "graph").mkdir()
    shutil.copyfile(plant_out / "graph.dot", plant_out / "records" / "graph" / "targets.dot")
    write_index(plant_out / "records", plant_index)
    write_index(out, index)
    print(f"records: {len(index['objects'])} objects, the plant's {len(plant_index['objects'])}")
    return 0


# Reading the records, for the rules (rules.py).
Object = namedtuple("Object", "source target includes undefined weak defined command vendored")


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
    def __init__(self, directory):
        self.directory = Path(directory)
        index = json.loads((self.directory / "index.json").read_text(encoding="utf-8"))
        self.root = Path(index["source_root"])
        self.digests = index.get("digests", {})
        self.objects = []
        for entry in index["objects"]:
            undefined, weak, defined = read_symbols(self.read(entry["symbols"]))
            includes = [] if entry.get("vendored") else read_includes(self.read(entry["includes"]),
                                                                       entry["absolute"])
            self.objects.append(Object(entry["source"], entry["target"], includes, undefined, weak, defined,
                                       entry["command"], bool(entry.get("vendored"))))
        self.runtime, self.network = set(), set()
        for name, path in index["libraries"].items():
            (self.runtime if name in index["runtime"] else self.network).update(
                read_linker_member(self.read(path)))
        self.shapes, self.edges = read_graph(self.read(index["graph"]))

    def read(self, name):
        return (self.directory / name).read_text(encoding="utf-8")

    def closure(self, target):
        seen, todo = set(), [target]
        while todo:
            for dep in self.edges.get(todo.pop(), ()):
                if dep not in seen:
                    seen.add(dep)
                    todo.append(dep)
        return seen

    def stale(self):
        """The recorded files changed or gone since the records were written."""
        return sorted(rel for rel, digest in self.digests.items()
                      if not (self.root / rel).is_file() or sha256(self.root / rel) != digest)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
