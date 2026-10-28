"""The structure rules, B1, B2, B5 to B17 and B32, as data, and one checker,
which also runs the safe subset's token rules, B18 to B31 (subset.py).

A rule reads includes, names and link edges from the compiler's and the
build's records (records.py writes and reads them), and strings,
identifiers and #include directives from the sources by the rules' lexer
(lexer.py); no rule reads C++ by a regular expression. The text rules are
B1's, a substring search for sqlite3, and B32's, a search of each text
file's bytes for those git cannot fix. check() applies every rule to one
tree, v3's or the planted one, and gives its findings. STATED holds the
allowances the text states beyond the rows' cells, each with its reason;
an allowance the text lacks has no place here.
"""

import os
import re
from collections import namedtuple
from fnmatch import fnmatchcase
from pathlib import Path

from tests.structure import subset
from tests.structure.lexer import lex
from tests.structure.records import Records, command_include_dirs

ROOT = Path(__file__).resolve().parents[2]
VENDORED = "third_party/sqlite/sqlite3.c"
STRUCTURE = "tests/structure/"

Finding = namedtuple("Finding", "rule where kind detail")

# The content rule's documented example values, each exempt by exact value,
# with its source: a drive-letter path into a profile's root.
EXAMPLES = {
    "C:/Users/ABC": "normcase's first vector, by CPython's ntpath and LCMapStringEx "
                    "(tests/contract/platform_last.py)",
    "c:\\users\\abc": "that vector's normcase, as the case module writes it",
}

# The C++ standard library's headers, C++20's and the C library's in both forms.
STD_HEADERS = frozenset("""
algorithm any array atomic barrier bit bitset charconv chrono codecvt compare complex concepts
condition_variable coroutine deque exception execution filesystem format forward_list fstream
functional future initializer_list iomanip ios iosfwd iostream istream iterator latch limits list
locale map memory memory_resource mutex new numbers numeric optional ostream queue random ranges
ratio regex scoped_allocator semaphore set shared_mutex source_location span sstream stack stdexcept
stop_token streambuf string string_view strstream syncstream system_error thread tuple type_traits
typeindex typeinfo unordered_map unordered_set utility valarray variant vector version
cassert cctype cerrno cfenv cfloat cinttypes climits clocale cmath csetjmp csignal cstdarg cstddef
cstdint cstdio cstdlib cstring ctime cuchar cwchar cwctype
assert.h ctype.h errno.h fenv.h float.h inttypes.h limits.h locale.h math.h setjmp.h signal.h
stdarg.h stddef.h stdint.h stdio.h stdlib.h string.h time.h uchar.h wchar.h wctype.h
""".split())
# The Windows SDK's directories of operating system headers (B5, B17).
SDK_DIRS = ("um", "shared", "winrt", "cppwinrt")

# B17: core logic's direct includes, copied from the platform's include
# boundary, which changes first; then sqlite3.h in B1's module alone, and
# v3's own headers outside src/platform/ and src/hal/, with the contract's.
B17_HEADERS = frozenset("""
algorithm array atomic bit charconv chrono cmath compare concepts condition_variable cstddef cstdint
cstring deque exception filesystem format fstream functional initializer_list iterator limits map
memory mutex numeric optional queue ranges set shared_mutex span sstream stdexcept stop_token string
string_view system_error thread tuple type_traits unordered_map unordered_set utility variant vector
""".split())
CONTRACT = "src/platform/platform.hpp"
# B17's names: the C runtime's that open a file to write, write, rename,
# delete, start a process or read or set the environment, each with its
# wide form; not fwrite or fputc, which every reading object names.
B17_NAMES = frozenset("""
fopen fopen_s _fsopen freopen freopen_s _fdopen tmpfile _open _sopen _sopen_s _creat _write _chsize
_chsize_s _locking rename remove _unlink _mkdir _rmdir system _popen getenv _dupenv_s _putenv _putenv_s
_wfopen _wfopen_s _wfsopen _wfreopen _wfreopen_s _wfdopen _wopen _wsopen _wsopen_s _wcreat _wrename
_wremove _wunlink _wmkdir _wrmdir _wsystem _wpopen _wgetenv _wdupenv_s _wputenv _wputenv_s
""".split())
SPAWN_EXEC = ("_spawn", "_wspawn", "_exec", "_wexec")
FS_PREFIX = "__std_fs_"
# Core's reads' six __std_fs_ names.
FS_READS = frozenset("__std_fs_" + n for n in (
    "get_stats", "directory_iterator_open", "directory_iterator_advance", "directory_iterator_close",
    "convert_narrow_to_wide", "convert_wide_to_narrow"))
# Names the linker defines itself, which no object or library does: the
# image's base, which an x64 jump table at /O2 names.
LINKER_NAMES = frozenset(("__ImageBase",))
WRITE_CLASSES = ("basic_ofstream", "basic_fstream", "_Recursive_dir_enum_impl")
STREAM_MODES = frozenset(("out", "app", "trunc", "noreplace"))
STREAM_TYPES = frozenset(("openmode", "filebuf", "basic_filebuf"))
# B14's writes: B17's file names but the environment's and the processes'.
WRITE_NAMES = frozenset(n for n in B17_NAMES if "env" not in n and "system" not in n and "popen" not in n)

# B2, B6, B7, B8, B10.
GIT_LITERALS = frozenset(("git", "git.exe"))
GIT_PREFIXES = ("git ", "refs/", "GIT_", "GCM_")
GIT_COMMANDS = frozenset(("cat-file", "rev-parse", "hash-object", "ls-tree", "commit-tree", "ls-remote",
                          "update-ref", "fast-import"))
# B32: in a text file, the bytes git cannot fix: a CR before no LF, and each control byte but tab and LF.
B32_BYTES = re.compile(rb"\r(?!\n)|[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]")
CRT_IO_HEADERS = frozenset(("io.h", "direct.h", "process.h", "conio.h"))
PROCESS_NAMES = frozenset(("system", "_wsystem", "_popen", "_wpopen", "ShellExecuteW", "ShellExecuteExW",
                           "WinExec", "fork", "posix_spawn"))
NET_HEADERS = ("winsock2.h", "ws2tcpip.h", "winhttp.h", "wininet.h", "sys/socket.h", "netdb.h")
NET_LIBS = frozenset(("ws2_32", "winhttp", "wininet"))
LOAD_NAMES = frozenset(("GetProcAddress", "dlopen", "dlsym", "sqlite3_load_extension"))
LOAD_PREFIX = "LoadLibrary"
# B13: the platform's calls that write.
PLATFORM_WRITES = frozenset(("replace", "stage", "create", "remove_directory", "lock"))

# Where each rule allows what it finds: path prefixes and target names.
ALLOWED = {
    "B1": ("src/store/engine/", "third_party/sqlite/", "qiven3_store"),
    "B2": ("src/platform/win/process.", "src/qiven3/mapcheck.", "tests/unit/mapcheck_test.",
           "tests/machine/test_mapcheck", "tests/machine/mapcheck"),
    # src/diag/: the diagnostic entry's Windows headers, its link to dbghelp
    # and its variants' (src/diag/CMakeLists.txt).
    "B5": ("src/platform/win/", "tests/child/", "qiven3_platform_win", "src/diag/", "qiven3_diag",
           "qiven3_diag_crtdbg", "qiven3_diag_poisoned"),
    "B11": ("tests/compat/r",),
    "B14": ("tests/support/goldens",),
    "B16": ("src/app/main.", "src/qiven3/mapcheck.", "tests/unit/mapcheck_test.", "tests/machine/mapcheck",
            "qiven3"),
    **subset.ALLOWED,
}
# The allowances the text states beyond the rows' cells, (rule, file, what),
# each with its reason; the subset's cells' are subset.CELLS. A file or a
# what ending in / is a prefix, and a file ending in . a module's files.
# B5's what is the header or the import, B6's and B9's the header, B17's
# the header its include finding names.
STATED = {
    ("B9", "src/diag/", "crtdbg.h"): "the diagnostic entry: the debug runtime's reports to standard error, "
                                     "and its report hook",
    ("B9", "src/diag/", "signal.h"): "the diagnostic entry's handlers on Linux",
    ("B9", "src/diag/", "execinfo.h"): "the diagnostic entry's backtrace on Linux",
    ("B9", "src/diag/", "unistd.h"): "the diagnostic entry's write and _exit on Linux",
    ("B9", "src/diag/", "mman.h"): "the diagnostic entry's alternate stack on Linux",
    ("B9", "src/diag/", "prctl.h"): "the diagnostic entry's PR_SET_PTRACER for the watcher on Linux",
    ("B9", "tests/", "tests/runner/"): "the test tree's own headers, which its sources include",
    ("B9", "tests/", "tests/support/"): "as tests/runner/'s",
    ("B9", "tests/", "tests/driver/"): "as tests/runner/'s",
    ("B9", "tests/", "tests/child/"): "as tests/runner/'s",
    ("B17", "src/memory/arena.cpp", "new"): "operator new and delete with std::align_val_t: the system "
                                            "heap's blocks B19 allows memory/arena and memory/pool",
    ("B17", "src/memory/pool.cpp", "new"): "as arena.cpp's, and std::nothrow",
    ("B9", "src/memory/poison.hpp", "asan_interface.h"): "AddressSanitizer's poisoning calls on what "
                                                         "memory's allocators hold, "
                                                         "sanitizer/asan_interface.h, included under the "
                                                         "sanitizer alone",
    ("B17", "src/memory/poison.hpp", "sanitizer/asan_interface.h"): "as B9's",
    ("B6", "tests/driver/common.cpp", "io.h"): "_setmode, the drives' binary standard streams: the C "
                                               "runtime opens them in text mode, and freopen with no "
                                               "path fails",
    ("B9", "tests/driver/common.cpp", "io.h"): "as B6's",
    ("B9", "tests/driver/common.cpp", "fcntl.h"): "_O_BINARY, _setmode's mode, as io.h's",
    ("B9", "tests/faults/fault_assert.cpp", "crtdbg.h"): "_ASSERTE, the debug runtime's assertion, the one "
                                                         "that reaches the diagnostic entry's report hook",
    ("B9", "src/diag/out.cpp", "fcntl.h"): "open, for the file QIVEN3_LOG names, on Linux",
}
ALLOWANCES = (*subset.CELLS, *STATED)
EXEMPT = {
    "B1": (STRUCTURE, "tests/audit/read_audit"),
    "B2": (STRUCTURE,),
    "B14": (STRUCTURE,),
}
# B12: the modules and targets that never reach backup or forgetting.
B12_FILES = ("src/store/", "src/entry/", "src/bundle/", "src/reader/", "src/platform/", "src/qiven3/canon.",
             "src/qiven3/digest.", "src/qiven3/location.", "src/wire/")
B12_TARGETS = frozenset(("qiven3_store", "qiven3_entry", "qiven3_matcher", "qiven3_secret", "qiven3_layout",
                         "qiven3_bundle", "qiven3_reader", "qiven3_platform", "qiven3_platform_win",
                         "qiven3_platform_current", "qiven3_base", "qiven3_location", "qiven3_wire"))
B12_HEADERS = ("src/backup/", "src/forget/")
B12_LINKS = ("qiven3_forget", "qiven3_backup")
# B13: the reader's includes and its targets' links.
B13_HEADERS = ("src/store/", "src/backup/", "src/forget/", "src/qiven3/mapcheck.", "src/entry/secret.",
               "src/bundle/")
B13_TARGETS = frozenset(("qiven3_reader", "qiven3_layout", "qiven3_entry", "qiven3_matcher", "qiven3_base"))
B13_LINKS = ("qiven3_sqlite", "qiven3_store", "qiven3_forget", "qiven3_secret", "qiven3_mapcheck",
             "qiven3_bundle", "qiven3_backup")
# Core logic: every product target but the adapters, current and the HAL's.
NOT_CORE = ("src/platform/win/", "src/platform/current.", "src/hal/", "src/diag/")
NOT_CORE_TARGETS = frozenset(("qiven3_platform_win", "qiven3_platform_current", "qiven3_platform_fake",
                              "qiven3_hal", "qiven3_hal_fake", "qiven3_diag", "qiven3_diag_entry",
                              "qiven3_diag_crtdbg", "qiven3_diag_entry_crtdbg", "qiven3_diag_poisoned",
                              "qiven3_diag_entry_poisoned",
                              # memory, the base and the log again on the debug runtime, for
                              # qiven3-fault-crtdbg alone: their objects name that runtime, which the
                              # records do not read; the release copies are read.
                              "qiven3_memory_crtdbg", "qiven3_base_crtdbg", "qiven3_log_crtdbg"))
CORE_NAMED = frozenset(("qiven3_flags", "qiven3_sqlite"))
# An instantiation file may add its binary's one adapter header.
ADAPTER_HEADERS = frozenset(("src/platform/current.hpp", "src/platform/win/windows.hpp",
                             "tests/support/fake_platform/fake_platform.hpp"))
LIBRARY_SHAPES = frozenset(("octagon", "doubleoctagon", "tripleoctagon", "pentagon", "hexagon"))
CXX_SOURCES = (".cpp", ".hpp", ".c", ".h")


def under(path, prefixes):
    """A path under one of prefixes; a prefix with no / is a target's name, matched whole."""
    return path is not None and any(path == p or ("/" in p and path.startswith(p)) for p in prefixes)


def allowed(rule, where, what=None):
    if under(where, ALLOWED.get(rule, ())):
        return True
    return any(r == rule and match(where, f) and match(what, w) for r, f, w in ALLOWANCES)


def match(value, key):
    return value is not None and (value == key or (key.endswith(("/", ".")) and value.startswith(key)))


def is_instantiation(path):
    return path is not None and path.endswith(".cpp") and path.rsplit("/", 1)[-1].startswith("instances")


def is_core(path):
    return (path is not None and path.startswith("src/") and not under(path, NOT_CORE)
            and not is_instantiation(path))


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
    def __init__(self):
        self.found = set()

    def add(self, rule, where, kind="", detail="", what=None, force=False):
        if force or not (allowed(rule, where, what) or under(where, EXEMPT.get(rule, ()))):
            self.found.add(Finding(rule, where, kind, detail))


def ignored(rel, name, is_dir, patterns):
    for pattern in patterns:
        if pattern.endswith("/") and not is_dir:
            continue
        pattern = pattern.rstrip("/")
        if fnmatchcase(rel, pattern.lstrip("/")) if "/" in pattern else fnmatchcase(name, pattern):
            return True
    return False


def source_files(root):
    """Every file of the tree but .git and what .gitignore and the checkout's
    own exclude file name."""
    patterns = []
    for name in (".gitignore", ".git/info/exclude"):
        if (root / name).is_file():
            patterns += [line.strip() for line in (root / name).read_text(encoding="utf-8").splitlines()
                         if line.strip() and not line.startswith(("#", "!"))]
    found = []
    for directory, dirs, files in os.walk(root):
        base = Path(directory).relative_to(root).as_posix()
        prefix = "" if base == "." else base + "/"
        dirs[:] = sorted(d for d in dirs if d != ".git" and not ignored(prefix + d, d, True, patterns))
        found += [prefix + f for f in files if not ignored(prefix + f, f, False, patterns)]
    return sorted(found)


def line_count(data):
    return data.count(b"\n") + (1 if data and not data.endswith(b"\n") else 0)


def is_goldens(value):
    parts = value.replace("\\", "/").split("/")
    return any(a == "tests" and b == "goldens" for a, b in zip(parts, parts[1:]))


def is_git(value):
    return value in GIT_LITERALS or value.startswith(GIT_PREFIXES) or value in GIT_COMMANDS


def app_namespace(tokens, i):
    """Whether tokens[i], `app` after `::`, names the binary's namespace,
    qiven3::app, not a stream's mode: `qiven3` before the `::`, written as
    one qualified name (namespace qiven3::app, qiven3::app::main); nested,
    `app` follows no `::`."""
    return tokens[i][1] == "app" and i >= 2 and tokens[i - 2][0:2] == ("id", "qiven3")


def text_attributes(root):
    """The lines of the root's .gitattributes that set the text attribute,
    in order: [(pattern, text)]. -text and binary unset it; text, text=...
    and !text leave the file text to B32, git's own content check aside."""
    path = root / ".gitattributes"
    lines = [x.split() for x in path.read_text(encoding="utf-8").splitlines()] if path.is_file() else []
    return [(f[0], a not in ("-text", "binary")) for f in lines if f and not f[0].startswith("#") for a in f[1:]
            if a in ("-text", "binary", "text", "!text") or a.startswith("text=")]


def is_text(rel, attributes):
    """Whether .gitattributes treats rel as text: the last line matching it
    decides, a pattern with a slash matched against the path from the root,
    one without against the name; fnmatch's * crosses a slash, as ** does."""
    matched = [text for pattern, text in attributes
               if fnmatchcase(rel if "/" in pattern else rel.rsplit("/", 1)[-1], pattern.lstrip("/"))]
    return matched[-1] if matched else True


def source_rules(root, files, out):
    """B1's text, B2's and B14's strings, B13's and B17's tokens, B15's lines,
    B32's bytes in every file .gitattributes treats as text.
    Gives the files holding a goldens path, for B14's writes."""
    goldens = set()
    attributes = text_attributes(root)
    for rel in files:
        data = (root / rel).read_bytes()
        name = rel.rsplit("/", 1)[-1]
        if name.endswith((".hpp", ".cpp", ".py", ".cmake")) or name == "CMakeLists.txt":
            if line_count(data) > 600:
                out.add("B15", rel, detail=f"{line_count(data)} lines")
        found = B32_BYTES.search(data) if is_text(rel, attributes) else None
        if found:
            line = data.count(b"\n", 0, found.start()) + 1
            what = "a lone CR" if found.group() == b"\r" else f"the control byte 0x{found.group()[0]:02x}"
            out.add("B32", rel, detail=f"line {line}: {what}")
        text = data.decode("utf-8", "replace")
        if rel.startswith(("src/", "tests/")) and "sqlite3" in text:
            out.add("B1", rel, detail="the text sqlite3")
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
            if (is_core(rel) or is_instantiation(rel)) and (
                    (value in STREAM_MODES and before == "::" and not app_namespace(tokens, i))
                    or value in STREAM_TYPES):
                out.add("B17", rel, "mode", f"line {line}: a stream's writing mode, {value}")
    return goldens


def include_rules(records, out):
    root, seen = records.root, set()
    for obj in records.objects:
        if obj.vendored:
            continue
        checked = is_core_object(obj) or is_instantiation(obj.source)
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
    if rel is not None and not (rel.startswith("src/") or rel.endswith("/sqlite3.h")):
        out.add("B9", by, detail=f"an include outside src/, {rel}", what=rel)
    if ".." in path.replace("\\", "/").split("/"):
        out.add("B11", by, detail=f"an include path holding .., {path}")
    if rel is not None:
        if under(by, B12_FILES) and under(rel, B12_HEADERS):
            out.add("B12", by, detail=f"includes {rel}")
        if by.startswith("src/reader/") and under(rel, B13_HEADERS) and not rel.startswith("src/bundle/layout."):
            out.add("B13", by, detail=f"includes {rel}")
        if rel == "src/qiven3/mapcheck.hpp":
            out.add("B16", by, detail="includes the map check")
    if is_core(by) or is_instantiation(by):
        if kind == "std":
            ok = name in B17_HEADERS
        elif kind == "v3":
            ok = (rel == CONTRACT or (is_instantiation(by) and rel in ADAPTER_HEADERS)
                  or (rel.endswith("/sqlite3.h") and by.startswith("src/store/engine/"))
                  or (rel.startswith(("src/", "tests/")) and not rel.startswith(("src/platform/", "src/hal/"))))
        else:
            ok = False
        if not ok:
            out.add("B17", by, "include", f"a direct include off the list, {name}", what=name)


def strip_imp(name):
    return name[len("__imp_"):] if name.startswith("__imp_") else name


def is_core_object(obj):
    return is_core(obj.source) and obj.target not in NOT_CORE_TARGETS


def b17_name(full):
    bare = strip_imp(full)
    return (bare in B17_NAMES or bare.startswith(SPAWN_EXEC) or any(c in full for c in WRITE_CLASSES)
            or (bare.startswith(FS_PREFIX) and bare not in FS_READS))


def name_rules(records, goldens, out):
    objects = records.objects
    core_defined = set().union(*(o.defined for o in objects if is_core_object(o) or o.vendored))
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
                if not under(src, ("src/platform/win/process.",)):
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
        if not (is_core_object(obj) or is_instantiation(src)):
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
    core = {o.target for o in objects if is_core_object(o) and shapes.get(o.target) in LIBRARY_SHAPES}
    core = (core | CORE_NAMED) - NOT_CORE_TARGETS
    owners = {}
    for o in objects:
        owners.setdefault(o.target, set()).add(o.source)
    child = {t for t, sources in owners.items() if any(under(s, ("tests/child/",)) for s in sources)}
    mapcheck = {t for t, sources in owners.items() if any(under(s, ALLOWED["B16"]) for s in sources)}
    for target, deps in sorted(records.edges.items()):
        for dep in sorted(deps):
            if dep == "qiven3_sqlite" and target != "qiven3_store":
                out.add("B1", target, detail="links qiven3_sqlite")
            if shapes.get(dep) == "septagon" and target not in child:
                out.add("B5", target, detail=f"links the system library {dep}")
            if dep.lower().removesuffix(".lib") in NET_LIBS:
                out.add("B8", target, detail=f"links {dep}")
            if dep == "qiven3_mapcheck" and target not in mapcheck:
                out.add("B16", target, detail="links qiven3_mapcheck")
            if target in core and dep != "qiven3_platform" and dep not in core:
                out.add("B17", target, "link", f"a core target links {dep}", force=True)
    for target in sorted(shapes):
        reached = records.closure(target)
        if target in B12_TARGETS and any(d.startswith(B12_LINKS) for d in reached):
            out.add("B12", target, detail="reaches backup or forgetting")
        if target in B13_TARGETS and any(d.startswith(B13_LINKS) for d in reached):
            out.add("B13", target, detail="the reader's target reaches " + ", ".join(
                sorted(d for d in reached if d.startswith(B13_LINKS))))


def check(records, root=None):
    """Every rule over one tree: its findings."""
    root = Path(root or records.root)
    out = Findings()
    files = source_files(root)
    goldens = source_rules(root, files, out)
    include_rules(records, out)
    directive_rules(records, root, files, out)
    name_rules(records, goldens, out)
    link_rules(records, out)
    subset.check(root, files, lambda rule, where, kind, detail, what: out.add(rule, where, kind, detail, what))
    return out.found


def records_directory():
    """The structure build's records: this build's, else the structure preset's."""
    build = os.environ.get("QIVEN3_BUILD")
    if build and (Path(build) / "structure" / "index.json").is_file():
        return Path(build) / "structure"
    return ROOT / "build" / "structure" / "structure"


def findings(which):
    """The findings over v3's tree ("tree") or the planted project ("plant"),
    from the structure build's records, which must be there and current."""
    directory = records_directory()
    if not (directory / "index.json").is_file():
        raise AssertionError(f"no structure records at {directory}: build the structure preset first")
    if which == "plant":
        planted = Records(directory / "plant" / "records")
        return check(planted, planted.root)
    records = Records(directory)
    stale = records.stale()
    if stale:
        raise AssertionError("the structure records are older than " + ", ".join(stale[:5])
                             + ": build the structure preset again")
    return check(records, ROOT)
