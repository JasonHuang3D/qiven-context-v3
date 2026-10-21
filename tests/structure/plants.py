"""The planted project the structure tests run the checker over: one file
or target per rule broken (PLANTED), and the clean files, the allowances
and the exemptions, which must pass (CLEAN). Standard library only.

records.py runs it, as

    python -I tests/structure/plants.py TREE

which writes the project into TREE, after clearing TREE's parent; records.py
then builds it and writes its records there. The tests read PLANTED and
CLEAN from it.
"""

import os
import shutil
import sys
from pathlib import Path


# The planted project. Each rule broken in one file or target; CLEAN files
# name what the rules find in comments and strings, and must pass.
PLANT_CMAKE = """cmake_minimum_required(VERSION 3.28)
project(plant LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
  tests/support/goldens/write.cpp tests/runner/runner.cpp)
add_library(qiven3_diag STATIC src/diag/diag_windows.cpp)
target_link_libraries(qiven3_diag PRIVATE dbghelp)
"""

DLLIMPORT = 'extern "C" __declspec(dllimport) '
PLANTS = {
    "third_party/sqlite/sqlite3.h": "#pragma once\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
        "int sqlite3_libversion_number(void);\nint sqlite3_config(int, ...);\n#ifdef __cplusplus\n}\n#endif\n",
    "third_party/sqlite/sqlite3.c": "#include <windows.h>\n#include <process.h>\n#include \"sqlite3.h\"\n"
        "int sqlite3_libversion_number(void) {\n    HANDLE h = CreateFileW(L\"x\", 0, 0, 0, 3, 0, 0);\n"
        "    return h == INVALID_HANDLE_VALUE ? 0 : (int)GetACP();\n}\n"
        "int sqlite3_config(int op, ...) { return op; }\n",
    "src/platform/platform.hpp": "#pragma once\n#include <span>\n#include <string_view>\nnamespace qiven3::platform {\n"
        "int contract();\nstruct Command { std::span<const std::string_view> argv; std::string_view cwd; };\n}\n",
    "src/platform/platform.cpp": "#include \"platform/platform.hpp\"\nint qiven3::platform::contract() { return 0; }\n",
    "src/platform/win/windows.hpp": "#pragma once\nnamespace qiven3::platform::win {\n"
        "struct WindowsPlatform { unsigned code_page() const; };\n}\n",
    "src/platform/win/windows.cpp": "#include \"platform/win/windows.hpp\"\n#include <windows.h>\n"
        "unsigned qiven3::platform::win::WindowsPlatform::code_page() const { return GetACP(); }\n",
    "src/platform/win/process.cpp": DLLIMPORT + "int __stdcall CreateProcessW(const wchar_t*, wchar_t*, void*, "
        "void*, int, unsigned long, void*, const wchar_t*, void*, void*);\n"
        "int start() { return CreateProcessW(nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr, nullptr, "
        "nullptr); }\nconst char* git() { return \"git\"; }\n",
    "src/hal/hal.hpp": "#pragma once\n#include <string_view>\nnamespace qiven3::hal {\n"
        "inline int devices() { return 0; }\ntemplate <class B> struct Launch { std::string_view kernel; };\n}\n",
    "src/forget/forget.hpp": "#pragma once\nint forget_plan();\n",
    "src/forget/system.cpp": "#include <cstdlib>\n#include \"forget/forget.hpp\"\n"
        "int forget_plan() { return std::system(\"x\"); }\n",
    "src/store/sqlite.cpp": "#include \"sqlite3.h\"\nint store_version() "
        "{ return sqlite3_libversion_number() + sqlite3_config(1); }\n",
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
    "src/entry/load_library_ex.cpp": DLLIMPORT + "void* __stdcall LoadLibraryExW(const wchar_t*, void*, unsigned);\n"
        "void* entry_load_ex() { return LoadLibraryExW(L\"x.dll\", nullptr, 0); }\n",
    "src/entry/shell_execute.cpp": DLLIMPORT + "int __stdcall ShellExecuteExW(void*);\n"
        "int entry_shell() { return ShellExecuteExW(nullptr); }\n",
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
        "extern \"C\" char __ImageBase;\nconst void* clean_base() { return &__ImageBase; }\n"
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
    "tests/runner/runner.cpp": "#include <windows.h>\n#include <crtdbg.h>\n#include \"support/plant/joined.hpp\"\n"
        "#include \"runner/runner.hpp\"\n#include \"driver/drive.hpp\"\n"
        "int runner_quiet() { _CrtSetReportMode(0, 0); return int(SetErrorMode(1)); }\n",
    # The runner's error mode and reports are the diagnostic entry's now.
    "src/diag/diag_windows.cpp": "#include <windows.h>\n#include <crtdbg.h>\n#include <dbghelp.h>\n"
        "int diag_quiet() { _CrtSetReportMode(0, 0); SymSetOptions(0); return int(SetErrorMode(1)); }\n",
    "tests/unit/own_test.cpp": "#include \"unit/own.hpp\"\n", "tests/unit/own.hpp": "\n",
    "tests/runner/runner.hpp": "#pragma once\n", "tests/driver/drive.hpp": "#pragma once\n",
    "tests/unit/dbg_test.cpp": "#include <crtdbg.h>\n",
    ".clang-tidy": "Checks: '-*,cppcoreguidelines-*,-cppcoreguidelines-pro-bounds-pointer-arithmetic'\n",
    "src/entry/.clang-tidy": "InheritParentConfig: true\nChecks: >-\n  -cppcoreguidelines-pro-bounds-*,\n"
        "  -bugprone-*\n",
    "src/qiven3/.clang-tidy": "# clang-tidy's defaults beneath, pointer arithmetic's check off\n"
        "Checks: '-bugprone-*'\n",
    "src/platform/.clang-tidy": "InheritParentConfig: true\nChecks: '-bugprone-*'\n",
    "src/memory/.clang-tidy": "# B26\nChecks: -cppcoreguidelines-pro-bounds-pointer-arithmetic\n",
    "analysis.ruleset": '<RuleSet Name="plant"><Rules><Rule Id="C26481" Action="None" /></Rules></RuleSet>\n',
    "../outside/outside.hpp": "#pragma once\ninline int outside() { return 0; }\n",
    # The stated allowances by file and header, the drives' binary streams
    # and memory's poisoning; the same headers in another file are caught.
    "tests/driver/common.cpp": "#include <fcntl.h>\n#include <io.h>\n#include \"driver/drive.hpp\"\n"
        "int drive_binary() { return _setmode(0, _O_BINARY); }\n",
    "tests/driver/io_other.cpp": "#include <io.h>\nint drive_other() { return 0; }\n",
    "tests/driver/fcntl_other.cpp": "#include <fcntl.h>\nint drive_mode() { return _O_BINARY; }\n",
    # B9's two more by file and header: the debug runtime's assertion and
    # the log's open on Linux; the same headers in another file are caught.
    "tests/faults/fault_assert.cpp": "#include <crtdbg.h>\nint fault_assert(int x) { _ASSERTE(x > 0); return x; }\n",
    "tests/faults/assert_other.cpp": "#include <crtdbg.h>\nint fault_other() { return 0; }\n",
    "src/diag/out.cpp": "#include <fcntl.h>\nint diag_open_mode() { return O_WRONLY; }\n",
    "src/diag/fcntl_other.cpp": "#include <fcntl.h>\nint diag_other_mode() { return O_RDONLY; }\n",
    "src/memory/poison.hpp": "#pragma once\n#if defined(__SANITIZE_ADDRESS__)\n"
        "#include <sanitizer/asan_interface.h>\n#endif\n",
    "src/memory/unpoison.hpp": "#pragma once\n#if defined(__SANITIZE_ADDRESS__)\n"
        "#include <sanitizer/asan_interface.h>\n#endif\n",
}
# B17's names the C runtime adds to fopen's, each declared by hand.
B17_ADDED = ("freopen_s", "_fdopen", "tmpfile", "_sopen", "_chsize")
PLANTS.update({f"src/entry/crt_{n.lstrip('_')}.cpp": DLLIMPORT + f"int __cdecl {n}(int);\n"
               f"int entry_{n.lstrip('_')}() {{ return {n}(0); }}\n" for n in B17_ADDED})

# Each plant's finding, (rule, file or target, kind), the kind B17's.
PLANTED = [
    ("B1", "src/entry/sqlite_include.cpp", ""), ("B1", "tests/unit/sqlite_text_test.cpp", ""),
    ("B1", "qiven3_entry", ""),
    ("B2", "src/entry/git.cpp", ""), ("B2", "src/store/rev_parse.cpp", ""),
    ("B2", "src/store/create_process.cpp", ""),
    ("B5", "src/entry/windows_include.cpp", ""), ("B5", "qiven3_location", ""),
    ("B6", "src/entry/io_include.cpp", ""), ("B6", "src/entry/locking.cpp", ""),
    ("B6", "tests/driver/io_other.cpp", ""), ("B9", "tests/driver/io_other.cpp", ""),
    ("B9", "tests/driver/fcntl_other.cpp", ""), ("B9", "tests/faults/assert_other.cpp", ""),
    ("B9", "src/diag/fcntl_other.cpp", ""),
    ("B9", "src/memory/unpoison.hpp", ""), ("B17", "src/memory/unpoison.hpp", "include"),
    ("B7", "src/forget/system.cpp", ""), ("B7", "src/store/create_process.cpp", ""),
    ("B7", "src/entry/shell_execute.cpp", ""), ("B8", "src/entry/winsock.cpp", ""), ("B8", "qiven3_location", ""),
    ("B9", "src/entry/outside.cpp", ""), ("B9", "tests/unit/own_test.cpp", ""), ("B9", "tests/unit/dbg_test.cpp", ""),
    ("B5", "tests/runner/runner.cpp", ""), ("B9", "tests/runner/runner.cpp", ""),
    ("B10", "src/entry/load_library.cpp", ""), ("B10", "src/entry/load_library_ex.cpp", ""),
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
    ("B17", "src/entry/malloc.hpp", "include"), ("B17", "src/memory/new.hpp", "include"),
    *(("B17", f"src/entry/crt_{n.lstrip('_')}.cpp", "name") for n in B17_ADDED),
    ("B26", ".clang-tidy", "tidy"), ("B26", "src/entry/.clang-tidy", "tidy"), ("B26", "analysis.ruleset", "ruleset"),
    ("B26", "src/qiven3/.clang-tidy", "tidy"),
]
# Files and targets with no finding: the clean file, the allowances, the exemptions.
CLEAN = [
    "src/qiven3/clean.cpp", "tests/unit/edge_test.cpp", "src/reader/instances.cpp",
    "src/platform/win/process.cpp", "src/platform/win/windows.cpp", "src/qiven3/mapcheck.cpp",
    "third_party/sqlite/sqlite3.c", "src/store/sqlite.cpp", "tests/support/goldens/goldens.cpp",
    "qiven3_base", "qiven3_platform_win", "qiven3_sqlite", "src/diag/diag_windows.cpp", "qiven3_diag",
    "src/platform/platform.hpp",
    "src/hal/hal.hpp", "src/memory/.clang-tidy", "src/platform/.clang-tidy", "tests/driver/common.cpp",
    "src/memory/poison.hpp", "tests/faults/fault_assert.cpp", "src/diag/out.cpp",
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
    "src/entry/varargs.hpp": "#pragma once\n#include <cstdarg>\n"
        "inline int entry_config() { return sqlite3_config(1); }\ninline int entry_first(int n, ...) {\n"
        "    va_list args;\n    va_start(args, n);\n    int v = va_arg(args, int);\n    va_end(args);\n"
        "    return v;\n}\n",
    "src/store/sqlite.hpp": "#pragma once\ninline int store_db(void* db) { return sqlite3_db_config(db, 1); }\n",
    "src/entry/union.hpp": "#pragma once\nunion EntryBits { float f; unsigned u; };\n",
    "src/entry/thread.hpp": "#pragma once\n#include <thread>\n"
        "inline void entry_spawn() { std::thread t([] {}); t.detach(); }\n",
    "src/entry/joined.hpp": "#include <thread>\ninline void entry_join() { std::thread t([] {}); t.join(); }\n",
    "tests/support/plant/joined.hpp": "#pragma once\n#include <thread>\n"
        "inline void support_join() { std::thread t([] {}); if (t.joinable()) { t.join(); } }\n",
    "tests/support/plant/unjoined.hpp": "#pragma once\n#include <thread>\n"
        "inline void support_spawn() { { std::thread t([] {}); } t.join(); }\n",
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
    "src/entry/terminate.hpp": "#pragma once\n"
        "inline void entry_end(void* self) { TerminateProcess(self, 3); }\n",
    "tests/unit/scope_test.hpp": "#pragma once\n#include <cstdlib>\n"
        "inline int* scope_new() { int* p = new int(1); if (!p) { std::abort(); } return p; }\n",
    "src/memory/arena.hpp": "#pragma once\n#include <cstddef>\n"
        "inline void* arena_block(std::size_t n) { return ::operator new(n, std::align_val_t{64}); }\n"
        "inline void arena_drop(void* p) { ::operator delete(p, std::align_val_t{64}); }\n"
        "class Arena { std::span<std::byte> pages_; };\n",
    "src/memory/arena.cpp": "#include <new>\n", "src/memory/pool.cpp": "#include <new>\n",
    "src/memory/new.hpp": "#pragma once\n#include <new>\n",
    "src/memory/pool.hpp": "#pragma once\n#include <cstddef>\n#include <span>\n"
        "class Pool { std::span<std::byte> pages_; };\nclass Heap { std::span<std::byte> pages_; };\n",
    "src/memory/borrow.hpp": "#pragma once\n#include <span>\nclass Pool { std::span<int> held; };\n",
    "src/memory/construct.hpp": "#pragma once\n#include <memory>\n"
        "inline int* memory_place(void* p) { return new (p) int(1); }\n"
        "inline void memory_end(int* p) { std::destroy_at(p); }\n",
    "src/memory/fail.hpp": "#pragma once\n#include <atomic>\n#include <exception>\n"
        "std::atomic<const char*> fail_cause{nullptr};\n"
        "[[noreturn]] inline void fail_fast() noexcept { std::terminate(); }\n",
    "src/diag/handlers.hpp": "#pragma once\n#include <windows.h>\n#include <cstdlib>\n"
        "struct DiagState { int first = 0; };\nDiagState diag_state;\n"
        "inline void diag_end() { CreateThread(nullptr, 0, nullptr, nullptr, 0, nullptr); "
        "ExitProcess(3); TerminateProcess(GetCurrentProcess(), 3); std::abort(); }\n",
    "src/platform/win/clock.hpp": "#pragma once\n#include <chrono>\n"
        "inline auto win_now() { return std::chrono::system_clock::now(); }\n",
    "src/qiven3/subset_clean.hpp": "#pragma once\n"
        "// Names each subset rule's finding in comments and strings alone: typeid,\n"
        "// dynamic_cast, new, delete, malloc, free, reinterpret_cast, const_cast,\n"
        "// va_list, union, detach, std::thread, thread_local, a std::string_view\n"
        "// member, std::mutex, lock(), INFINITE, system_clock, assert, abort, exit,\n"
        "// std::terminate, TerminateProcess; and the forms each rule lets pass.\n"
        "#include <chrono>\n#include <cstddef>\n#include <mutex>\n#include <span>\n"
        "#include <string_view>\n#include <thread>\n#include <variant>\n"
        "namespace qiven3::clean {\n"
        "inline constexpr std::string_view names = \"typeid dynamic_cast new delete malloc free "
        "reinterpret_cast const_cast va_list union detach std::thread thread_local std::mutex lock() "
        "INFINITE system_clock assert abort exit TerminateProcess\";\n"
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
    ("B22", "src/entry/varargs.hpp", "call"), ("B22", "src/store/sqlite.hpp", "call"),
    ("B24", "src/entry/thread.hpp", "detach"), ("B24", "src/entry/thread.hpp", "thread"),
    ("B24", "src/entry/joined.hpp", "thread"), ("B24", "tests/support/plant/unjoined.hpp", "thread"),
    ("B25", "src/entry/globals.hpp", "thread_local"), ("B25", "src/entry/globals.hpp", "object"),
    ("B25", "src/entry/globals.hpp", "static"),
    ("B27", "src/entry/nolint.hpp", "nolint"), ("B27", "src/entry/nolint.hpp", "pragma"),
    ("B27", "src/entry/nolint.hpp", "suppress"),
    ("B28", "src/entry/command.hpp", ""), ("B28", "src/memory/borrow.hpp", ""),
    ("B29", "src/entry/mutex.hpp", ""), ("B29", "src/entry/mutex.hpp", "lock"),
    ("B29", "src/entry/mutex.hpp", "infinite"),
    ("B30", "src/entry/clock.hpp", ""), ("B31", "src/entry/abort.hpp", ""),
    ("B31", "src/entry/terminate.hpp", ""),
]
SUBSET_CLEAN = [
    "tests/unit/scope_test.hpp", "src/memory/arena.hpp", "src/memory/construct.hpp", "src/memory/fail.hpp",
    "src/platform/win/clock.hpp", "src/qiven3/subset_clean.hpp", "src/memory/arena.cpp", "src/memory/pool.cpp",
    "src/memory/pool.hpp", "tests/support/plant/joined.hpp", "src/diag/handlers.hpp",
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
    plant(Path(sys.argv[1]))
