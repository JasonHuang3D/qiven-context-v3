"""The libraries a binary imports, read from its bytes by the standard
library alone, no tool.

- PE (Windows): the import directory and the delay-load directory, the
  optional header's data directories 1 and 13, each name read through the
  section holding its address.
- Unreadable: the bytes are no PE image this reader reads.
- Imports: the names, each imported library in the file's order.
- read_file(path): the Imports of a file's bytes.

Its ELF and glibc parts are retired (tools/README.md's Retired section).
Its consumers: the watcher (foundation/devkit/test/harness/watch.py), which
names, on a failed load, each import found nowhere, and K11
(foundation/devkit/build/tests/test_binaries.py), which holds every
binary of a build to its system's list or its preset's residual.
"""

import struct
from dataclasses import dataclass
from pathlib import Path


class Unreadable(Exception):
    """The bytes are no PE image this reader reads."""


@dataclass(frozen=True)
class Imports:
    names: tuple  # each imported library, in the file's order


def _cstring(data, offset):
    """The NUL-ended string at offset, as text."""
    if not 0 <= offset < len(data):
        raise Unreadable(f"a name's offset {offset:#x} is outside the file")
    end = data.find(b"\0", offset)
    if end < 0:
        raise Unreadable(f"the name at {offset:#x} has no end")
    return data[offset:end].decode("utf-8", "replace")


def _unpack(fmt, data, offset):
    if offset < 0 or offset + struct.calcsize(fmt) > len(data):
        raise Unreadable(f"a field at {offset:#x} is outside the file")
    return struct.unpack_from(fmt, data, offset)


def _pe(data):
    """A PE image's imports: directories 1 and 13."""
    (lfanew,) = _unpack("<I", data, 0x3C)
    if data[lfanew:lfanew + 4] != b"PE\0\0":
        raise Unreadable("no PE signature")
    coff = lfanew + 4
    _, sections, _, _, _, optional_size, _ = _unpack("<HHIIIHH", data, coff)
    opt = coff + 20
    (magic,) = _unpack("<H", data, opt)
    if magic == 0x20B:
        (image_base,) = _unpack("<Q", data, opt + 24)
        count_at, dirs = opt + 108, opt + 112
    elif magic == 0x10B:
        (image_base,) = _unpack("<I", data, opt + 28)
        count_at, dirs = opt + 92, opt + 96
    else:
        raise Unreadable(f"an optional header of magic {magic:#x}")
    (count,) = _unpack("<I", data, count_at)
    table = []
    for i in range(sections):
        _, vsize, va, raw_size, raw = _unpack("<8sIIII", data, opt + optional_size + 40 * i)
        table.append((va, max(vsize, raw_size), raw))

    def at(rva):
        for va, size, raw in table:
            if va <= rva < va + size:
                return raw + rva - va
        raise Unreadable(f"the address {rva:#x} is in no section")

    def directory(index):
        return _unpack("<II", data, dirs + 8 * index) if index < count else (0, 0)

    names = []
    rva, size = directory(1)
    if rva and size:
        entry = at(rva)
        while True:
            fields = _unpack("<IIIII", data, entry)
            if not any(fields):
                break
            names.append(_cstring(data, at(fields[3])))
            entry += 20
    rva, size = directory(13)
    if rva and size:
        entry = at(rva)
        while True:
            fields = _unpack("<8I", data, entry)
            if not any(fields):
                break
            attributes, name = fields[0], fields[1]
            # Attribute bit 0 clear: the old form, addresses not relative.
            names.append(_cstring(data, at(name if attributes & 1 else name - image_base)))
            entry += 32
    return Imports(tuple(names))


def _read(data):
    """The imports of an image's bytes, a PE."""
    try:
        if data[:2] == b"MZ":
            return _pe(data)
    except (IndexError, struct.error) as e:
        raise Unreadable(str(e)) from e
    raise Unreadable("no PE")


def read_file(path):
    return _read(Path(path).read_bytes())
