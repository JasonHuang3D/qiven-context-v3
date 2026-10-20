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
BUILD/structure/plant/, one file or target per rule broken (PLANTS below),
builds it and writes its records the same way, so that the tests run the
same checker over it. third_party/sqlite/sqlite3.c, the one vendored file,
has its object's names recorded, for what it defines, and no includes.
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
# The runtimes a /MD build links; oldnames.lib, POSIX names, is not one.
RUNTIME_LIBS = ("msvcrt.lib", "msvcprt.lib", "vcruntime.lib", "ucrt.lib")
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
    plant(tree)
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


# The planted project. Each rule broken in one file or target; CLEAN files
# name what the rules find in comments and strings, and must pass.
PLANT_CMAKE = """cmake_minimum_required(VERSION 3.28)
project(plant LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc;/utf-8;/permissive-;/W0>")
include_directories(src tests third_party/sqlite)
get_filename_component(outside "${CMAKE_SOURCE_DIR}/../outside" ABSOLUTE)
add_library(qiven3_flags INTERFACE)
add_library(qiven3_platform STATIC src/platform/platform.cpp)
add_library(qiven3_platform_win STATIC src/platform/win/windows.cpp src/platform/win/process.cpp)
target_link_libraries(qiven3_platform_win PUBLIC qiven3_platform PRIVATE kernel32)
add_library(qiven3_sqlite STATIC third_party/sqlite/sqlite3.c)
add_library(qiven3_forget STATIC src/forget/system.cpp)
add_library(qiven3_store STATIC src/store/sqlite.cpp src/store/hal_include.cpp src/store/rev_parse.cpp
  src/store/forget_include.cpp src/store/create_process.cpp)
target_link_libraries(qiven3_store PUBLIC qiven3_platform qiven3_sqlite qiven3_forget)
file(GLOB entry_sources CONFIGURE_DEPENDS src/entry/*.cpp)
list(REMOVE_ITEM entry_sources "${CMAKE_SOURCE_DIR}/src/entry/secret.cpp")
add_library(qiven3_entry STATIC ${entry_sources})
target_include_directories(qiven3_entry PRIVATE "${outside}")
target_link_libraries(qiven3_entry PUBLIC qiven3_platform qiven3_sqlite qiven3_flags)
add_library(qiven3_secret STATIC src/entry/secret.cpp)
add_library(qiven3_reader STATIC src/reader/secret_include.cpp src/reader/replace_call.cpp)
target_link_libraries(qiven3_reader PUBLIC qiven3_store)
add_library(qiven3_mapcheck STATIC src/qiven3/mapcheck.cpp)
add_library(qiven3_base STATIC src/qiven3/clean.cpp)
target_link_libraries(qiven3_base PUBLIC qiven3_platform qiven3_flags)
add_library(qiven3_location STATIC src/qiven3/location.cpp)
target_link_libraries(qiven3_location PUBLIC qiven3_mapcheck qiven3_platform_win ws2_32 ole32)
add_library(qiven3_app_instances STATIC src/app/instances.cpp)
add_library(qiven3_reader_instances STATIC src/reader/instances.cpp)
target_link_libraries(qiven3_reader_instances PRIVATE qiven3_platform_win)
add_library(qiven3_unit_plant STATIC tests/unit/sqlite_text_test.cpp tests/unit/long_test.cpp
  tests/unit/unended_test.cpp tests/unit/edge_test.cpp tests/support/goldens/goldens.cpp
  tests/support/goldens/write.cpp)
"""

DLLIMPORT = 'extern "C" __declspec(dllimport) '
PLANTS = {
    "third_party/sqlite/sqlite3.h": "#pragma once\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
        "int sqlite3_libversion_number(void);\n#ifdef __cplusplus\n}\n#endif\n",
    "third_party/sqlite/sqlite3.c": "#include <windows.h>\n#include <process.h>\n#include \"sqlite3.h\"\n"
        "int sqlite3_libversion_number(void) {\n    HANDLE h = CreateFileW(L\"x\", 0, 0, 0, 3, 0, 0);\n"
        "    return h == INVALID_HANDLE_VALUE ? 0 : (int)GetACP();\n}\n",
    "src/platform/platform.hpp": "#pragma once\nnamespace qiven3::platform { int contract(); }\n",
    "src/platform/platform.cpp": "#include \"platform/platform.hpp\"\nint qiven3::platform::contract() { return 0; }\n",
    "src/platform/win/windows.hpp": "#pragma once\nnamespace qiven3::platform::win {\n"
        "struct WindowsPlatform { unsigned code_page() const; };\n}\n",
    "src/platform/win/windows.cpp": "#include \"platform/win/windows.hpp\"\n#include <windows.h>\n"
        "unsigned qiven3::platform::win::WindowsPlatform::code_page() const { return GetACP(); }\n",
    "src/platform/win/process.cpp": DLLIMPORT + "int __stdcall CreateProcessW(const wchar_t*, wchar_t*, void*, "
        "void*, int, unsigned long, void*, const wchar_t*, void*, void*);\n"
        "int start() { return CreateProcessW(nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, "
        "nullptr); }\nconst char* git() { return \"git\"; }\n",
    "src/hal/hal.hpp": "#pragma once\nnamespace qiven3::hal { inline int devices() { return 0; } }\n",
    "src/forget/forget.hpp": "#pragma once\nint forget_plan();\n",
    "src/forget/system.cpp": "#include <cstdlib>\n#include \"forget/forget.hpp\"\n"
        "int forget_plan() { return std::system(\"x\"); }\n",
    "src/store/sqlite.cpp": "#include \"sqlite3.h\"\nint store_version() { return sqlite3_libversion_number(); }\n",
    "src/store/hal_include.cpp": "#include \"hal/hal.hpp\"\nint store_devices() { return qiven3::hal::devices(); }\n",
    "src/store/rev_parse.cpp": "const char* store_rev() { return \"rev-parse\"; }\n",
    "src/store/forget_include.cpp": "#include \"forget/forget.hpp\"\nint store_forget() { return forget_plan(); }\n",
    "src/store/create_process.cpp": DLLIMPORT + "int __stdcall CreateProcessW(const wchar_t*, wchar_t*, void*, "
        "void*, int, unsigned long, void*, const wchar_t*, void*, void*);\n"
        "int store_start() { return CreateProcessW(nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr, "
        "nullptr, nullptr); }\n",
    "src/entry/sqlite_include.cpp": "#include \"sqlite3.h\"\nint entry_version() { return 0; }\n",
    "src/entry/load_library.cpp": DLLIMPORT + "void* __stdcall LoadLibraryW(const wchar_t*);\n"
        "void* entry_load() { return LoadLibraryW(L\"x.dll\"); }\n",
    "src/entry/git.cpp": "const char* entry_git() { return \"git\"; }\n",
    "src/entry/windows_include.cpp": "#include <windows.h>\nunsigned entry_acp() { return GetACP(); }\n",
    "src/entry/get_acp.cpp": "extern \"C\" unsigned __stdcall GetACP();\nunsigned entry_acp_by_hand() "
        "{ return GetACP(); }\n",
    "src/entry/io_include.cpp": "#include <io.h>\nint entry_io() { return 0; }\n",
    "src/entry/locking.cpp": "extern \"C\" int _locking(int, int, long);\nint entry_lock() "
        "{ return _locking(0, 0, 1); }\n",
    "src/entry/winsock.cpp": "#include <winsock2.h>\nint entry_socket() { return 0; }\n",
    "src/entry/outside.cpp": "#include \"outside.hpp\"\nint entry_outside() { return outside(); }\n",
    "src/entry/dotdot.hpp": "#pragma once\ninline int dotdot() { return 0; }\n",
    "src/entry/dotdot.cpp": "#include \"../entry/dotdot.hpp\"\nint entry_dotdot() { return dotdot(); }\n",
    "src/entry/mapcheck_include.cpp": "#include \"qiven3/mapcheck.hpp\"\nint entry_marks() "
        "{ return qiven3::marks(); }\n",
    "src/entry/goldens.cpp": "const char* entry_goldens() { return \"tests/goldens/v1/registry.json\"; }\n",
    "src/entry/cstdio_include.cpp": "#include <cstdio>\nint entry_eof() { return EOF; }\n",
    "src/entry/cstdio_again.cpp": "#include <string>\n#include <cstdio>\nint entry_again() { return EOF; }\n",
    "src/entry/rename.cpp": "#include <cstdio>\n#include <cstdlib>\nint entry_rename() "
        "{ return std::rename(\"a\", \"b\") + std::system(\"c\"); }\n",
    "src/entry/fs_remove.cpp": "#include <filesystem>\nbool entry_remove() "
        "{ return std::filesystem::remove(std::filesystem::path(u8\"x\")); }\n",
    "src/entry/wfopen.cpp": DLLIMPORT + "int __cdecl _wfopen_s(void**, const wchar_t*, const wchar_t*);\n"
        "int entry_open() { void* f = nullptr; return _wfopen_s(&f, L\"x\", L\"w\"); }\n",
    "src/entry/ofstream.cpp": "#include <fstream>\nvoid entry_write() { std::ofstream f(\"x\"); f << 1; }\n",
    "src/entry/stream_mode.cpp": "#include <fstream>\nbool entry_mode() "
        "{ std::ifstream f(\"x\", std::ios::out); return f.good(); }\n",
    "src/entry/secret.hpp": "#pragma once\nint secret_check();\n",
    "src/entry/secret.cpp": "#include \"entry/secret.hpp\"\nint secret_check() { return 0; }\n",
    "src/reader/secret_include.cpp": "#include \"entry/secret.hpp\"\nint reader_secret() { return secret_check(); }\n",
    "src/reader/replace_call.cpp": "template <class P>\nvoid reader_write(P& p) { p.replace(\"a\", \"b\", 1); }\n",
    "src/qiven3/mapcheck.hpp": "#pragma once\nnamespace qiven3 { int marks(); }\n",
    "src/qiven3/mapcheck.cpp": "#include \"qiven3/mapcheck.hpp\"\nconst char* mapcheck_git() "
        "{ return \"rev-parse\"; }\nint qiven3::marks() { return 0; }\n",
    "src/qiven3/location.cpp": "#include <string>\nint location_size() { return int(std::string(\"x\").size()); }\n",
    "src/qiven3/clean.cpp": "// Names each rule's finding in comments and strings alone: <windows.h>,\n"
        "// GetACP, std::rename, std::system, std::filesystem::remove, std::ios::out,\n"
        "// LoadLibraryW, CreateProcessW, _locking, <io.h>, <winsock2.h>, \"../x.hpp\",\n"
        "// hal/hal.hpp, qiven3/mapcheck.hpp, entry/secret.hpp, p.replace(a, b).\n"
        "#include <string>\n#include <string_view>\n#include \"platform/platform.hpp\"\n"
        "std::string clean_names() {\n"
        "    std::string s = \"<windows.h> GetACP std::rename std::system std::ios::out LoadLibraryW\";\n"
        "    s += \"CreateProcessW _locking <io.h> <winsock2.h> ../x.hpp hal/hal.hpp qiven3/mapcheck.hpp\";\n"
        "    s += R\"(std::filesystem::remove std::ofstream p.replace(a, b) .git digit gitx)\";\n"
        "    std::string_view parts[] = {\".git\", \"digit\", \"tests/x/goldens\", \"Git\", \"refs\"};\n"
        "    for (std::string_view part : parts) { s += part; }\n"
        "    return s + std::to_string(qiven3::platform::contract());\n}\n",
    "src/app/instances.cpp": "#include <cstdio>\nvoid* app_open() { return std::fopen(\"x\", \"w\"); }\n",
    "src/reader/instances.cpp": "#include <string>\n#include \"platform/win/windows.hpp\"\n"
        "unsigned reader_code_page() { return qiven3::platform::win::WindowsPlatform().code_page(); }\n",
    "tests/unit/sqlite_text_test.cpp": "// A unit test naming sqlite3 in a comment.\nint sqlite_text() { return 0; }\n",
    "tests/unit/long_test.cpp": "// 601 lines.\n" + "//\n" * 599 + "int long_test() { return 0; }\n",
    "tests/unit/unended_test.cpp": "// 600 lines and one unended.\n" + "//\n" * 599 + "int unended() { return 0; }",
    "tests/unit/edge_test.cpp": "// 600 lines, each ended.\n" + "//\n" * 598 + "int edge() { return 0; }\n",
    "tests/support/goldens/goldens.cpp": "const char* goldens_registry() { return \"tests/goldens/v1/registry.json\"; }\n",
    "tests/support/goldens/write.cpp": "#include <cstdio>\nvoid* goldens_write() "
        "{ return std::fopen(\"tests/goldens/v1/x\", \"w\"); }\n",
    "../outside/outside.hpp": "#pragma once\ninline int outside() { return 0; }\n",
}

# Each plant's finding, (rule, file or target, kind), the kind B17's.
PLANTED = [
    ("B1", "src/entry/sqlite_include.cpp", ""), ("B1", "tests/unit/sqlite_text_test.cpp", ""),
    ("B1", "qiven3_entry", ""),
    ("B2", "src/entry/git.cpp", ""), ("B2", "src/store/rev_parse.cpp", ""),
    ("B2", "src/store/create_process.cpp", ""),
    ("B5", "src/entry/windows_include.cpp", ""), ("B5", "qiven3_location", ""),
    ("B6", "src/entry/io_include.cpp", ""), ("B6", "src/entry/locking.cpp", ""),
    ("B7", "src/forget/system.cpp", ""), ("B7", "src/store/create_process.cpp", ""),
    ("B8", "src/entry/winsock.cpp", ""), ("B8", "qiven3_location", ""),
    ("B9", "src/entry/outside.cpp", ""),
    ("B10", "src/entry/load_library.cpp", ""),
    ("B11", "src/entry/dotdot.cpp", ""),
    ("B12", "src/store/forget_include.cpp", ""), ("B12", "qiven3_store", ""),
    ("B13", "src/reader/secret_include.cpp", ""), ("B13", "src/reader/replace_call.cpp", ""),
    ("B13", "qiven3_reader", ""),
    ("B14", "src/entry/goldens.cpp", ""), ("B14", "tests/support/goldens/write.cpp", ""),
    ("B15", "tests/unit/long_test.cpp", ""), ("B15", "tests/unit/unended_test.cpp", ""),
    ("B16", "src/entry/mapcheck_include.cpp", ""), ("B16", "qiven3_location", ""),
    ("B17", "src/store/hal_include.cpp", "include"), ("B17", "src/entry/cstdio_include.cpp", "include"),
    ("B17", "src/entry/cstdio_again.cpp", "include"),
    ("B17", "src/entry/windows_include.cpp", "os"), ("B17", "src/entry/rename.cpp", "name"),
    ("B17", "src/entry/fs_remove.cpp", "name"), ("B17", "src/entry/wfopen.cpp", "name"),
    ("B17", "src/entry/get_acp.cpp", "name"), ("B17", "src/entry/ofstream.cpp", "name"),
    ("B17", "src/entry/stream_mode.cpp", "mode"), ("B17", "src/app/instances.cpp", "include"),
    ("B17", "src/app/instances.cpp", "name"), ("B17", "qiven3_location", "link"),
]
# Files and targets with no finding: the clean file, the allowances, the exemptions.
CLEAN = [
    "src/qiven3/clean.cpp", "tests/unit/edge_test.cpp", "src/reader/instances.cpp",
    "src/platform/win/process.cpp", "src/platform/win/windows.cpp", "src/qiven3/mapcheck.cpp",
    "third_party/sqlite/sqlite3.c", "src/store/sqlite.cpp", "tests/support/goldens/goldens.cpp",
    "qiven3_base", "qiven3_platform_win", "qiven3_sqlite",
]

# The safe subset's plants (subset.py), headers no target compiles: each of
# B18 to B31 but B26 broken, files of tests/ outside the subset's scope and of
# tests/support/ inside it, the allowances, and a clean file naming each
# finding in comments and strings.
SUBSET_PLANTS = {
    "src/entry/typeid.hpp": "#pragma once\n#include <typeinfo>\n"
        "inline const char* entry_type(int x) { return typeid(x).name(); }\n",
    "tests/unit/typeid_test.hpp": "#pragma once\ninline bool same(int x) { return typeid(x) == typeid(int); }\n",
    "src/entry/new.hpp": "#pragma once\ninline int* entry_new() { return new int(1); }\n"
        "inline void entry_delete(int* p) { delete p; }\n",
    "src/entry/malloc.hpp": "#pragma once\n#include <cstdlib>\n#include <new>\n"
        "inline void* entry_malloc() { return std::malloc(8); }\ninline void entry_free(void* p) { free(p); }\n"
        "inline void* entry_operator() { return ::operator new(8); }\n",
    "tests/support/plant/support_new.hpp": "#pragma once\ninline int* support_new() { return new int(2); }\n",
    "src/entry/placement.hpp": "#pragma once\n#include <memory>\n#include <new>\n"
        "inline int* entry_place(void* p) { return new (p) int(1); }\n"
        "inline void entry_end(int* p) { std::destroy_at(p); }\n",
    "src/entry/casts.hpp": "#pragma once\ninline long entry_bits(const double* d) "
        "{ return *reinterpret_cast<const long*>(d); }\n"
        "inline int* entry_unconst(const int* p) { return const_cast<int*>(p); }\n",
    "src/entry/varargs.hpp": "#pragma once\n#include <cstdarg>\ninline int entry_first(int n, ...) {\n"
        "    va_list args;\n    va_start(args, n);\n    int v = va_arg(args, int);\n    va_end(args);\n"
        "    return v;\n}\n",
    "src/entry/union.hpp": "#pragma once\nunion EntryBits { float f; unsigned u; };\n",
    "src/entry/thread.hpp": "#pragma once\n#include <thread>\n"
        "inline void entry_spawn() { std::thread t([] {}); t.detach(); }\n",
    "src/entry/globals.hpp": "#pragma once\ninline thread_local int entry_depth = 0;\nint entry_count = 0;\n"
        "struct EntryCounter { static int made; };\n",
    "src/entry/nolint.hpp": "#pragma once\ninline int entry_lint() { return 0; }  // NOLINT(bugprone-*)\n"
        "#pragma warning(disable : 26400)\n[[gsl::suppress(\"26400\")]] inline int entry_quiet() { return 0; }\n",
    "src/entry/command.hpp": "#pragma once\n#include <string_view>\n"
        "struct EntryCommand { std::string_view cwd; };\n",
    "src/entry/mutex.hpp": "#pragma once\n#include <mutex>\nstruct EntryGuarded { std::mutex m; };\n"
        "inline void entry_wait(std::timed_mutex& m) { m.lock(); m.unlock(); }\n"
        "inline unsigned long entry_forever() { return INFINITE; }\n",
    "src/entry/clock.hpp": "#pragma once\n#include <chrono>\n"
        "inline auto entry_now() { return std::chrono::system_clock::now(); }\n",
    "src/entry/abort.hpp": "#pragma once\n#include <cassert>\n#include <cstdlib>\n"
        "inline void entry_stop(int x) { assert(x > 0); if (x > 1) { std::abort(); } std::exit(1); }\n",
    "tests/unit/scope_test.hpp": "#pragma once\n#include <cstdlib>\n"
        "inline int* scope_new() { int* p = new int(1); if (!p) { std::abort(); } return p; }\n",
    "src/memory/arena.hpp": "#pragma once\n#include <cstddef>\n"
        "inline void* arena_block(std::size_t n) { return ::operator new(n, std::align_val_t{64}); }\n"
        "inline void arena_drop(void* p) { ::operator delete(p, std::align_val_t{64}); }\n",
    "src/memory/construct.hpp": "#pragma once\n#include <memory>\n"
        "inline int* memory_place(void* p) { return new (p) int(1); }\n"
        "inline void memory_end(int* p) { std::destroy_at(p); }\n",
    "src/memory/fail.hpp": "#pragma once\n#include <exception>\n"
        "[[noreturn]] inline void fail_fast() noexcept { std::terminate(); }\n",
    "src/platform/win/clock.hpp": "#pragma once\n#include <chrono>\n"
        "inline auto win_now() { return std::chrono::system_clock::now(); }\n",
    "src/qiven3/subset_clean.hpp": "#pragma once\n"
        "// Names each subset rule's finding in comments and strings alone: typeid,\n"
        "// dynamic_cast, new, delete, malloc, free, reinterpret_cast, const_cast,\n"
        "// va_list, union, detach, std::thread, thread_local, a std::string_view\n"
        "// member, std::mutex, lock(), INFINITE, system_clock, assert, abort, exit,\n"
        "// std::terminate; and the forms each rule lets pass.\n"
        "#include <chrono>\n#include <cstddef>\n#include <mutex>\n#include <span>\n"
        "#include <string_view>\n#include <thread>\n#include <variant>\n"
        "namespace qiven3::clean {\n"
        "inline constexpr std::string_view names = \"typeid dynamic_cast new delete malloc free "
        "reinterpret_cast const_cast va_list union detach std::thread thread_local std::mutex lock() "
        "INFINITE system_clock assert abort exit\";\n"
        "const int limit = 4;\nconst char* const label = \"x\";\nconstexpr std::chrono::seconds wait{5};\n"
        "struct Holder {\n    Holder() = default;\n    Holder(const Holder&) = delete;\n"
        "    Holder& operator=(const Holder&) = delete;\n    void free(void* p) noexcept;\n"
        "    static constexpr std::string_view name = \"holder\";\n    static const int count;\n"
        "    std::variant<int, float> value;\n    std::size_t exit_code = 0;\n};\n"
        "inline void Holder::free(void* p) noexcept { static_cast<void>(p); }\n"
        "struct TextView {\n    std::string_view text;\n    std::span<const std::byte> bytes;\n};\n"
        "inline bool take(std::timed_mutex& m, Holder& h, void* p) {\n    h.free(p);\n"
        "    std::jthread worker([] {});\n    if (!m.try_lock_for(wait)) {\n        return false;\n    }\n"
        "    m.unlock();\n    const auto start = std::chrono::steady_clock::now();\n"
        "    return std::chrono::steady_clock::now() >= start + wait && limit > 0;\n}\n"
        "}  // namespace qiven3::clean\n",
}
SUBSET_PLANTED = [
    ("B18", "src/entry/typeid.hpp", ""), ("B18", "tests/unit/typeid_test.hpp", ""),
    ("B19", "src/entry/new.hpp", "new"), ("B19", "src/entry/new.hpp", "delete"),
    ("B19", "src/entry/malloc.hpp", "c heap"), ("B19", "src/entry/malloc.hpp", "operator"),
    ("B19", "tests/support/plant/support_new.hpp", "new"),
    ("B20", "src/entry/placement.hpp", "placement"), ("B20", "src/entry/placement.hpp", "construct"),
    ("B21", "src/entry/casts.hpp", ""), ("B22", "src/entry/varargs.hpp", ""), ("B23", "src/entry/union.hpp", ""),
    ("B24", "src/entry/thread.hpp", ""),
    ("B25", "src/entry/globals.hpp", "thread_local"), ("B25", "src/entry/globals.hpp", "object"),
    ("B25", "src/entry/globals.hpp", "static"),
    ("B27", "src/entry/nolint.hpp", "nolint"), ("B27", "src/entry/nolint.hpp", "pragma"),
    ("B27", "src/entry/nolint.hpp", "suppress"),
    ("B28", "src/entry/command.hpp", ""),
    ("B29", "src/entry/mutex.hpp", ""), ("B29", "src/entry/mutex.hpp", "lock"),
    ("B29", "src/entry/mutex.hpp", "infinite"),
    ("B30", "src/entry/clock.hpp", ""), ("B31", "src/entry/abort.hpp", ""),
]
SUBSET_CLEAN = [
    "tests/unit/scope_test.hpp", "src/memory/arena.hpp", "src/memory/construct.hpp", "src/memory/fail.hpp",
    "src/platform/win/clock.hpp", "src/qiven3/subset_clean.hpp",
]
PLANTS.update(SUBSET_PLANTS)
PLANTED += SUBSET_PLANTED
CLEAN += SUBSET_CLEAN


def plant(tree):
    shutil.rmtree(tree.parent, ignore_errors=True)
    for name, text in {"CMakeLists.txt": PLANT_CMAKE, **PLANTS}.items():
        path = Path(os.path.normpath(tree / name))
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.encode("utf-8"))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
