"""The structure rules' lists, B1, B2, B5 to B11, B13, B14, B17 and B32, as
data, which rules.py's checker reads. STATED holds the allowances the text
states beyond the rows' cells, each with its reason; an allowance the text
lacks has no place here.
"""

import re

from foundation.devkit.structure import layers, subset

STRUCTURE = "foundation/devkit/structure/"

# The content rule's documented example values, each exempt by exact value,
# with its source: a drive-letter path into a profile's root.
EXAMPLES = {
    "C:/Users/ABC": "normcase's first vector, by CPython's ntpath and LCMapStringEx "
                    "(foundation/system/platform/tests/win_platform_test.cpp)",
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
# v3's own headers outside foundation/system/platform/ and foundation/services/compute/, a
# library's of core to products or the test tree's (layers.py), with the
# contract's.
B17_HEADERS = frozenset("""
algorithm array atomic bit charconv chrono cmath compare concepts condition_variable cstddef cstdint
cstring deque exception filesystem format fstream functional initializer_list iterator limits map
memory mutex numeric optional queue ranges set shared_mutex span sstream stdexcept stop_token string
string_view system_error thread tuple type_traits unordered_map unordered_set utility variant vector
""".split())
CONTRACT = "foundation/system/platform/include/qiven/platform/platform.hpp"
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
GIT_LOWER_PREFIXES = tuple(p.lower() for p in GIT_PREFIXES)  # is_git's, read in any case
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
PLATFORM_WRITES = frozenset(("replace", "stage", "create", "remove_directory", "remove", "rename", "lock"))
RECORDLOG, ENGINE = "foundation/services/storage/recordlog/", "foundation/services/storage/sqlite/"  # store, wrapper
DIAG = "foundation/system/diag/"  # the diagnostic entry

# Where each rule allows what it finds: path prefixes and target names.
ALLOWED = {
    "B1": (ENGINE + "include/qiven/storage_sqlite/", ENGINE + "src/", "qiven_storage_sqlite"),
    "B2": ("foundation/system/platform/src/windows/process.",),
    # Private sources alone, so that a Windows SDK header in a public header is
    # a finding. diag: the diagnostic entry's Windows headers in its src/, its
    # link to dbghelp and its variants' (foundation/system/diag/CMakeLists.txt).
    "B5": ("foundation/system/platform/src/windows/", layers.CHILD, "qiven_platform_windows", DIAG + "src/",
           "qiven_diag", "qiven_diag_crtdbg"),
    **subset.ALLOWED,
}
# The allowances the text states beyond the rows' cells, (rule, file, what),
# each with its reason; the subset's cells' are subset.CELLS. A file or a
# what ending in / is a prefix, a file ending in . a module's files, and a
# what a type's its members too. B1's what is the text, B1's include and link
# rules taking none; B5's the header or the import, B6's and B9's the header,
# B17's the header its include finding names.
STATED = {
    ("B1", "foundation/system/sqlite/README.md", "sqlite3"): "the vendored SQLite's module's page, which names its "
                                                             "contract, the vendored header",
    ("B1", "foundation/system/sqlite/module.json", "sqlite3"): "that module's table file, whose contract is the "
                                                               "vendored header's path",
    ("B1", "foundation/system/sqlite/CMakeLists.txt", "sqlite3"): "that module's build file: the pin of the "
                                                                  "vendored source's and header's digests, and "
                                                                  "its one target over them",
    ("B1", "foundation/system/sqlite/src/README.md", "sqlite3"): "that module's vendoring record: the vendored "
                                                                 "files' version, source, digests and options",
    ("B9", DIAG + "src/", "crtdbg.h"): "the diagnostic entry: the debug runtime's reports to standard error, "
                                       "and its report hook",
    ("B9", DIAG + "src/", "execinfo.h"): "the diagnostic entry's backtrace on Linux",
    ("B9", DIAG + "src/", "unistd.h"): "the diagnostic entry's write and _exit on Linux",
    ("B9", DIAG + "src/", "mman.h"): "the diagnostic entry's alternate stack on Linux",
    ("B9", DIAG + "src/", "prctl.h"): "the diagnostic entry's PR_SET_PTRACER for the watcher on Linux",
    ("B17", "foundation/core/memory/src/arena.cpp", "new"): "operator new and delete with std::align_val_t: the system "
                                            "heap's blocks B19 allows memory/arena and memory/pool",
    ("B17", "foundation/core/memory/src/pool.cpp", "new"): "as arena.cpp's, and std::nothrow",
    ("B17", "foundation/core/memory/include/qiven/memory/memory.hpp", "new"): "placement new for "
                                                                       "begin_lifetimes, which B20 "
                                                                       "allows memory alone",
    ("B9", "foundation/core/memory/src/poison.hpp", "asan_interface.h"): "AddressSanitizer's poisoning calls on what "
                                                         "memory's allocators hold, "
                                                         "sanitizer/asan_interface.h, included under the "
                                                         "sanitizer alone",
    ("B17", "foundation/core/memory/src/poison.hpp", "sanitizer/asan_interface.h"): "as B9's",
    ("B17", "foundation/system/platform/include/qiven/platform/names.hpp",
     "foundation/system/platform/include/qiven/platform/files.hpp"): "files.hpp's list, remove and rename, moved "
                                                                     "out of it for the 600-line bound; included "
                                                                     "only by the adapter, the fakes and their "
                                                                     "tests",
    ("B6", "tests/driver/common.cpp", "io.h"): "_setmode, the drives' binary standard streams: the C "
                                               "runtime opens them in text mode, and freopen with no "
                                               "path fails",
    ("B9", "tests/driver/common.cpp", "io.h"): "as B6's",
    ("B9", "tests/driver/common.cpp", "fcntl.h"): "_O_BINARY, _setmode's mode, as io.h's",
    ("B9", DIAG + "tests/faults/fault_assert.cpp", "crtdbg.h"): "_ASSERTE, the debug runtime's assertion, the "
                                                                "one that reaches the diagnostic entry's report "
                                                                "hook",
    ("B9", DIAG + "src/out.cpp", "fcntl.h"): "open, for the file QIVEN_LOG names, on Linux",
}
ALLOWANCES = (*subset.CELLS, *STATED)
# The directories a rule reads none of, every rule of it, each by prefix with
# its reason; every other allowance above is by exact path, a prefix ending in
# / or . or a target's name.
EXEMPT = {
    "B1": {STRUCTURE: "the structure checks, which name what they forbid: sqlite3 in B1's text rule, its include "
                      "and its link, as data and in their cases' scratch files"},
}
# B13 names what exists alone: a module not built yet comes back with its
# paths and targets with its row. A target is matched by its exact name, a
# variant by its own. B12, what never reaches backup or forgetting, has no
# list and no rule until those modules are built.
# B13: the reader's includes, its targets' links and its calls of the
# platform's writes (PLATFORM_WRITES, rules.py's token pass), the reader's
# part built today the entry: its files by path, the secret check's out of
# them (structure's records give a target's compiled sources alone, not its
# headers).
B13_FILES = ("src/entry/", "products/qiven3/entry/")
B13_SECRET = ("src/entry/secret.",)
B13_HEADERS = (RECORDLOG, ENGINE + "include/qiven/storage_sqlite/", "src/entry/secret.")
B13_OPEN = (RECORDLOG + "include/qiven/recordlog/keys.",  # under B13_HEADERS, open to it
            RECORDLOG + "include/qiven/recordlog/values.")
B13_TARGETS = frozenset(("qiven3_entry", "qiven_matcher", "qiven_text", "qiven_time", "qiven_digest",
                         "qiven3_format"))
B13_LINKS = frozenset(("qiven_sqlite", "qiven_storage_sqlite", "qiven_recordlog", "qiven3_secret"))
# Core logic: every product target but the adapters, current and compute's,
# and diag's files that call the system; the ring and the rest of diag are core.
NOT_CORE = ("foundation/system/platform/src/windows/", "foundation/system/platform/include/qiven/platform/windows/",
            "foundation/system/platform/src/fake/", "foundation/system/platform/include/qiven/platform/fake/",
            "foundation/system/platform/include/qiven/platform/current.", "foundation/system/platform/src/current.",
            "foundation/services/compute/", DIAG + "src/diag_linux.cpp", DIAG + "src/diag_windows.cpp",
            DIAG + "src/entry.cpp", DIAG + "src/out.cpp", DIAG + "src/signal.")
NOT_CORE_TARGETS = frozenset(("qiven_platform_windows", "qiven_platform_current", "qiven_platform_fake",
                              "qiven_compute", "qiven_compute_fake", "qiven_diag", "qiven_diag_entry",
                              "qiven_diag_crtdbg", "qiven_diag_entry_crtdbg", "qiven_diag_poisoned",
                              "qiven_diag_entry_poisoned",
                              # memory again on the debug runtime, for qiven_diag_fault_crtdbg
                              # alone: its objects name that runtime, which the records do not
                              # read; the release copy is read.
                              "qiven_memory_crtdbg"))
# B17's core targets that hold no object, by name: time a header alone, of
# core by its table (foundation/core/time/module.json).
CORE_NAMED = frozenset(("qiven_build", "qiven_sqlite", "qiven_recordlog_keys", "qiven_time"))
# An instantiation file may add its binary's one adapter header.
ADAPTER_HEADERS = frozenset(("foundation/system/platform/include/qiven/platform/current.hpp",
                             "foundation/system/platform/include/qiven/platform/windows/windows.hpp",
                             "foundation/system/platform/include/qiven/platform/fake/fake_platform.hpp"))
LIBRARY_SHAPES = frozenset(("octagon", "doubleoctagon", "tripleoctagon", "pentagon", "hexagon"))
CXX_SOURCES = layers.CXX
