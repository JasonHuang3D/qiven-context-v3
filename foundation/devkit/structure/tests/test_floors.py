"""The floors the build enforces: Windows 11 24H2, macOS 15.

- Windows: every compile command of the build's compile_commands.json
  sets NTDDI_VERSION 0x0A000010 (NTDDI_WIN11_GE) and _WIN32_WINNT and
  WINVER 0x0A00 (_WIN32_WINNT_WIN10), each once, and undefines none; a
  command off Windows sets none. Configure checks the build's own flags
  (foundation/devkit/build/build.cmake); this holds each target to them, since cl takes the
  last of two /D of one name without a word. No source of the tree
  defines or undefines one of them.
- Linux: Ubuntu 24.04's, K11's reading of each binary's glibc needs, is
  retired (tools/README.md's Retired section).
- macOS: the macos-clang preset's CMAKE_OSX_DEPLOYMENT_TARGET is 15.0.
  No job builds under it, here or in CI: CI's macos-clang job is gone.
Each over the tree."""

import json
import re
import unittest
from pathlib import Path

from foundation.devkit.python import system
from foundation.devkit.python import table
from foundation.devkit.python.build_tree import ROOT
from foundation.devkit.test.harness import binaries

TREES = table.trees(ROOT)
WINDOWS = system.WINDOWS
FLOOR = {"NTDDI_VERSION": "0x0A000010", "_WIN32_WINNT": "0x0A00", "WINVER": "0x0A00"}
NAMES = "|".join(FLOOR)
DEFINE = re.compile(rf'(?:^|\s)"?[/-]([DU])\s*"?({NAMES})(?:=([^\s"]*))?(?=["\s]|$)')
DIRECTIVE = re.compile(rf"^[ \t]*#[ \t]*(define|undef)[ \t]+({NAMES})\b", re.MULTILINE)
SUFFIXES = {*TREES.cxx_suffixes, ".rc"}  # .rc, a resource script: read here, and no C++ file
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


def tree_sources(root=ROOT):
    for top in TREES.every:  # the trees every check walks
        for path in sorted((root / top).rglob("*")):
            if path.suffix.lower() in SUFFIXES and path.is_file():
                yield path.relative_to(root).as_posix(), path.read_bytes().decode("utf-8", "replace")


def preset_findings(text):
    """The macOS preset's deployment target, if it is not the floor's."""
    presets = {p["name"]: p for p in json.loads(text)["configurePresets"]}
    value = presets.get(MACOS, {}).get("cacheVariables", {}).get("CMAKE_OSX_DEPLOYMENT_TARGET")
    value = value.get("value") if isinstance(value, dict) else value
    return [] if value == DEPLOYMENT else [f"{MACOS}: CMAKE_OSX_DEPLOYMENT_TARGET {value!r}, not {DEPLOYMENT}"]


class Floors(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_windows_commands(self):
        path = binaries.directory("QIVEN_BUILD") / "compile_commands.json"
        entries = json.loads(path.read_bytes().decode("utf-8"))
        self.assertEqual(database_findings(entries, WINDOWS), [])

    def test_sources(self):
        files = list(tree_sources())
        self.assertGreater(len(files), 10)
        self.assertEqual(source_findings(files), [])

    def test_macos_preset(self):
        text = (ROOT / "CMakePresets.json").read_bytes().decode("utf-8")
        self.assertEqual(preset_findings(text), [])


if __name__ == "__main__":
    unittest.main()
