"""K11's reader: the libraries a binary imports, read from its bytes by the
standard library alone, no tool; and the lists they must be on.

- PE (Windows): the import directory and the delay-load directory, the
  optional header's data directories 1 and 13, each name read through the
  section holding its address.

Its ELF and glibc parts are retired (tools/README.md's Retired section).
foundation/devkit/structure/tests/test_binaries.py holds every binary of a build to its
system's list or its preset's residual; tools/watch.py names, on a failed
load, each import found nowhere. Run as a script it prints a binary's
imports, one a line.
"""

import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

# The system's libraries a binary may import, case aside.
WINDOWS_SYSTEM = frozenset(("kernel32.dll", "ntdll.dll", "advapi32.dll", "shell32.dll", "ole32.dll",
                            "bcrypt.dll", "dbghelp.dll"))
# Windows' API sets, Windows' own: contract names the loader maps to the
# system's libraries, by their whole form, a bare name and no path.
API_SET = re.compile(r"(?:api|ext)-ms-win(?:-[a-z0-9]+)+-l[0-9]+-[0-9]+-[0-9]+\.dll", re.ASCII)
# Each preset's residual beyond the list, by its sanitizers.
ASAN_DLL = "clang_rt.asan_dynamic-x86_64.dll"
FUZZ_DLLS = frozenset(("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "ucrtbase.dll", ASAN_DLL))


class Unreadable(Exception):
    """The bytes are no PE image this reader reads."""


@dataclass(frozen=True)
class Imports:
    names: tuple  # each imported library, in the file's order


def cstring(data, offset):
    """The NUL-ended string at offset, as text."""
    if not 0 <= offset < len(data):
        raise Unreadable(f"a name's offset {offset:#x} is outside the file")
    end = data.find(b"\0", offset)
    if end < 0:
        raise Unreadable(f"the name at {offset:#x} has no end")
    return data[offset:end].decode("utf-8", "replace")


def unpack(fmt, data, offset):
    if offset < 0 or offset + struct.calcsize(fmt) > len(data):
        raise Unreadable(f"a field at {offset:#x} is outside the file")
    return struct.unpack_from(fmt, data, offset)


def pe(data):
    """A PE image's imports: directories 1 and 13."""
    (lfanew,) = unpack("<I", data, 0x3C)
    if data[lfanew:lfanew + 4] != b"PE\0\0":
        raise Unreadable("no PE signature")
    coff = lfanew + 4
    _, sections, _, _, _, optional_size, _ = unpack("<HHIIIHH", data, coff)
    opt = coff + 20
    (magic,) = unpack("<H", data, opt)
    if magic == 0x20B:
        (image_base,) = unpack("<Q", data, opt + 24)
        count_at, dirs = opt + 108, opt + 112
    elif magic == 0x10B:
        (image_base,) = unpack("<I", data, opt + 28)
        count_at, dirs = opt + 92, opt + 96
    else:
        raise Unreadable(f"an optional header of magic {magic:#x}")
    (count,) = unpack("<I", data, count_at)
    table = []
    for i in range(sections):
        _, vsize, va, raw_size, raw = unpack("<8sIIII", data, opt + optional_size + 40 * i)
        table.append((va, max(vsize, raw_size), raw))

    def at(rva):
        for va, size, raw in table:
            if va <= rva < va + size:
                return raw + rva - va
        raise Unreadable(f"the address {rva:#x} is in no section")

    def directory(index):
        return unpack("<II", data, dirs + 8 * index) if index < count else (0, 0)

    names = []
    rva, size = directory(1)
    if rva and size:
        entry = at(rva)
        while True:
            fields = unpack("<IIIII", data, entry)
            if not any(fields):
                break
            names.append(cstring(data, at(fields[3])))
            entry += 20
    rva, size = directory(13)
    if rva and size:
        entry = at(rva)
        while True:
            fields = unpack("<8I", data, entry)
            if not any(fields):
                break
            attributes, name = fields[0], fields[1]
            # Attribute bit 0 clear: the old form, addresses not relative.
            names.append(cstring(data, at(name if attributes & 1 else name - image_base)))
            entry += 32
    return Imports(tuple(names))


def read(data):
    """The imports of an image's bytes, a PE."""
    try:
        if data[:2] == b"MZ":
            return pe(data)
    except (IndexError, struct.error) as e:
        raise Unreadable(str(e)) from e
    raise Unreadable("no PE")


def read_file(path):
    return read(Path(path).read_bytes())


def outside(imports, sanitize):
    """Each import on neither the system's list nor the preset's residual,
    given the build's QIVEN_SANITIZE; an API set, Windows' own, passes in
    every preset."""
    allowed = set(WINDOWS_SYSTEM)
    if "fuzzer" in sanitize:
        allowed |= FUZZ_DLLS
    elif "address" in sanitize:
        allowed.add(ASAN_DLL)
    return [n for n in imports.names if n.lower() not in allowed and not API_SET.fullmatch(n.lower())]


def main(argv):
    if len(argv) != 1:
        print("usage: imports.py BINARY", file=sys.stderr)
        return 2
    try:
        found = read_file(argv[0])
    except (OSError, Unreadable) as e:
        print(f"imports.py: {argv[0]}: {e}", file=sys.stderr)
        return 1
    for name in found.names:
        print(name)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
