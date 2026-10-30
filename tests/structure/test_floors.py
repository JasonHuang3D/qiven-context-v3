"""The floors the build enforces: Windows 11 24H2, Ubuntu 24.04, macOS 15.

- Windows: every compile command of the build's compile_commands.json
  sets NTDDI_VERSION 0x0A000010 (NTDDI_WIN11_GE) and _WIN32_WINNT and
  WINVER 0x0A00 (_WIN32_WINNT_WIN10), each once, and undefines none; a
  command off Windows sets none. Configure checks the build's own flags
  (CMakeLists.txt); this holds each target to them, since cl takes the
  last of two /D of one name without a word. No source of the tree
  defines or undefines one of them.
- Linux: K11 (test_binaries.py) reads each binary's glibc needs.
- macOS: the macos-clang preset's CMAKE_OSX_DEPLOYMENT_TARGET is 15.0.
  No job builds under it, here or in CI: CI's macos-clang job is gone.
Each over the tree, and over planted copies that break it."""

import json
import re
import sys
import unittest
from pathlib import Path

from tests.drive import binaries

ROOT = Path(__file__).resolve().parents[2]
WINDOWS = sys.platform == "win32"
FLOOR = {"NTDDI_VERSION": "0x0A000010", "_WIN32_WINNT": "0x0A00", "WINVER": "0x0A00"}
NAMES = "|".join(FLOOR)
DEFINE = re.compile(rf'(?:^|\s)"?[/-]([DU])\s*"?({NAMES})(?:=([^\s"]*))?(?=["\s]|$)')
DIRECTIVE = re.compile(rf"^[ \t]*#[ \t]*(define|undef)[ \t]+({NAMES})\b", re.MULTILINE)
SOURCES = ("src", "tests", "tools")
SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx", ".inl", ".rc"}
MACOS = "macos-clang"
DEPLOYMENT = "15.0"


def command_findings(command, windows):
    """Each way one compile command leaves the floor."""
    found, values = [], {}
    for kind, name, value in DEFINE.findall(command):
        if kind == "U":
            found.append(f"undefines {name}")
        else:
            values.setdefault(name, []).append(value)
    for name, floor in FLOOR.items():
        given = values.get(name, [])
        if windows and given != [floor]:
            found.append(f"{name} {' '.join(given) or 'unset'}, not {floor}")
        elif not windows and given:
            found.append(f"{name} set off Windows")
    return found


def database_findings(entries, windows):
    """Each compile command of a compile_commands.json off the floor, by file."""
    found = []
    for entry in entries:
        command = entry.get("command") or " ".join(entry.get("arguments", ()))
        found += [f"{Path(entry['file']).name}: {f}" for f in command_findings(command, windows)]
    if not entries:
        found.append("no compile command")
    return found


def source_findings(files):
    """Each define or undefine of a floor macro, by (name, text) of each file."""
    return [f"{name}: #{kind} {macro}" for name, text in files for kind, macro in DIRECTIVE.findall(text)]


def tree_sources():
    for top in SOURCES:
        for path in sorted((ROOT / top).rglob("*")):
            if path.suffix.lower() in SUFFIXES and path.is_file():
                yield path.relative_to(ROOT).as_posix(), path.read_bytes().decode("utf-8", "replace")


def preset_findings(text):
    """The macOS preset's deployment target, if it is not the floor's."""
    presets = {p["name"]: p for p in json.loads(text)["configurePresets"]}
    value = presets.get(MACOS, {}).get("cacheVariables", {}).get("CMAKE_OSX_DEPLOYMENT_TARGET")
    value = value.get("value") if isinstance(value, dict) else value
    return [] if value == DEPLOYMENT else [f"{MACOS}: CMAKE_OSX_DEPLOYMENT_TARGET {value!r}, not {DEPLOYMENT}"]


class Floors(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_windows_commands(self):
        path = binaries.directory("QIVEN3_BUILD") / "compile_commands.json"
        entries = json.loads(path.read_bytes().decode("utf-8"))
        self.assertEqual(database_findings(entries, WINDOWS), [])

    def test_windows_commands_planted(self):
        floor = " ".join(f"-D{n}={v}" for n, v in FLOOR.items())
        control = f"cl /nologo /TP -DQIVEN3_LOG_MIN=info {floor} /DWIN32 /W4 /WX /c a.cpp"
        self.assertEqual(command_findings(control, True), [])
        self.assertEqual(command_findings("cc -DQIVEN3_LOG_MIN=info -c a.cpp", False), [])
        plants = {
            "a target's higher value after the floor's": control.replace("/c", "-DNTDDI_VERSION=0x0A000011 /c"),
            "a target's lower value": control.replace("/c", "/D_WIN32_WINNT=0x0A00 /DWINVER=0x0603 /c"),
            "a higher value in place of the floor's": control.replace("0x0A000010", "0x0A000011"),
            "the macros missing": control.replace(floor, ""),
            "one macro missing": control.replace("-DWINVER=0x0A00", ""),
            "an undefine": control.replace("/c", "/UNTDDI_VERSION /c"),
            "a value cl reads apart": control.replace("/c", '/D "_WIN32_WINNT=0x0A01" /c'),
        }
        for name, command in plants.items():
            with self.subTest(plant=name):
                self.assertNotEqual(command_findings(command, True), [])
        with self.subTest(plant="a floor macro off Windows"):
            self.assertNotEqual(command_findings(f"cc {floor} -c a.cpp", False), [])
        with self.subTest(plant="no compile command"):
            self.assertNotEqual(database_findings([], True), [])
        entries = [{"file": "a.cpp", "command": control}, {"file": "b.cpp", "arguments": control.split()}]
        self.assertEqual(database_findings(entries, True), [])
        entries[1]["arguments"] = plants["the macros missing"].split()
        self.assertEqual(len(database_findings(entries, True)), 3)

    def test_sources(self):
        files = list(tree_sources())
        self.assertGreater(len(files), 10)
        self.assertEqual(source_findings(files), [])

    def test_sources_planted(self):
        clean = ("// NTDDI_VERSION is set by the build\n#include <windows.h>\n"
                 "#if NTDDI_VERSION >= NTDDI_WIN11_GE\n#endif\nauto s = \"#define WINVER\";\n")
        self.assertEqual(source_findings([("clean.cpp", clean)]), [])
        plants = {
            "a higher value": "#define NTDDI_VERSION 0x0A000011\n#include <windows.h>\n",
            "an undefine, then a value": "#undef _WIN32_WINNT\n#define _WIN32_WINNT 0x0A01\n",
            "spaced": "  #  define WINVER 0x0603\n",
        }
        for name, text in plants.items():
            with self.subTest(plant=name):
                self.assertNotEqual(source_findings([("plant.cpp", text)]), [])

    def test_macos_preset(self):
        text = (ROOT / "CMakePresets.json").read_bytes().decode("utf-8")
        self.assertEqual(preset_findings(text), [])
        target = f'"CMAKE_OSX_DEPLOYMENT_TARGET": "{DEPLOYMENT}"'
        self.assertEqual(text.count(target), 1)
        plants = {
            "another value": text.replace(target, '"CMAKE_OSX_DEPLOYMENT_TARGET": "14.0"'),
            "a newer value": text.replace(target, '"CMAKE_OSX_DEPLOYMENT_TARGET": "26.0"'),
            "none": text.replace(target, '"QIVEN3_FAULTS": "ON"'),
        }
        for name, planted in plants.items():
            with self.subTest(plant=name):
                self.assertNotEqual(preset_findings(planted), [])


if __name__ == "__main__":
    unittest.main()
