"""K11's reader: the libraries a binary imports, read from its bytes by the
standard library alone, no tool; and the lists they must be on.

- PE (Windows): the import directory and the delay-load directory, the
  optional header's data directories 1 and 13, each name read through the
  section holding its address.
- ELF (Linux): the DT_NEEDED entries of PT_DYNAMIC, their names in
  DT_STRTAB, with PT_INTERP and any DT_RPATH or DT_RUNPATH.

tests/structure/test_binaries.py holds every binary of a build to its
system's list or its preset's residual; tools/watch.py names, on a failed
load, each import found nowhere. Run as a script it prints a binary's
imports, one a line.
"""

import struct
import sys
from dataclasses import dataclass
from pathlib import Path

# The system's libraries a binary may import, case aside on Windows.
WINDOWS_SYSTEM = frozenset(("kernel32.dll", "ntdll.dll", "advapi32.dll", "shell32.dll", "ole32.dll",
                            "bcrypt.dll", "dbghelp.dll"))
LINUX_SYSTEM = frozenset(("libc.so.6", "libm.so.6", "ld-linux-x86-64.so.2", "libpthread.so.0", "libdl.so.2",
                          "librt.so.1"))
# Each preset's residual beyond the list, by its sanitizers.
ASAN_DLL = "clang_rt.asan_dynamic-x86_64.dll"
FUZZ_DLLS = frozenset(("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "ucrtbase.dll", ASAN_DLL))
FUZZ_PREFIX = "api-ms-win-crt-"  # the universal C runtime's API sets, under /MD
MSAN_LIBS = frozenset(("libc++.so.1", "libc++abi.so.1"))
# clang's sanitizer runtimes link glibc's resolver, which the text's list lacks.
SANITIZER_LIBS = frozenset(("libresolv.so.2",))

PT_LOAD, PT_DYNAMIC, PT_INTERP = 1, 2, 3
DT_NULL, DT_NEEDED, DT_STRTAB, DT_RPATH, DT_RUNPATH = 0, 1, 5, 15, 29


class Unreadable(Exception):
    """The bytes are no PE or ELF image this reader reads."""


@dataclass(frozen=True)
class Imports:
    kind: str  # "pe" or "elf"
    names: tuple  # each imported library, in the file's order
    interp: str = ""  # ELF: PT_INTERP's loader
    run_paths: tuple = ()  # ELF: each DT_RPATH or DT_RUNPATH


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
    return Imports("pe", tuple(names))


def elf(data):
    """An ELF image's DT_NEEDED, PT_INTERP and run paths."""
    if data[4] not in (1, 2) or data[5] not in (1, 2):
        raise Unreadable("an ELF class or byte order this reader does not know")
    wide, end = data[4] == 2, "<" if data[5] == 1 else ">"
    word = "Q" if wide else "I"
    if wide:
        phoff, = unpack(end + "Q", data, 0x20)
        phentsize, phnum = unpack(end + "HH", data, 0x36)
        header = end + "IIQQQQQQ"  # type, flags, offset, vaddr, paddr, filesz, memsz, align
    else:
        phoff, = unpack(end + "I", data, 0x1C)
        phentsize, phnum = unpack(end + "HH", data, 0x2A)
        header = end + "IIIIIIII"  # type, offset, vaddr, paddr, filesz, memsz, flags, align
    loads, dynamic, interp = [], None, ""
    for i in range(phnum):
        fields = unpack(header, data, phoff + phentsize * i)
        if wide:
            kind, _, offset, vaddr, _, filesz, _, _ = fields
        else:
            kind, offset, vaddr, _, filesz, _, _, _ = fields
        if kind == PT_LOAD:
            loads.append((vaddr, filesz, offset))
        elif kind == PT_DYNAMIC:
            dynamic = (offset, filesz)
        elif kind == PT_INTERP:
            interp = cstring(data, offset)
    if dynamic is None:
        return Imports("elf", (), interp)
    entries, strtab = [], None
    step = struct.calcsize(end + word + word)
    for i in range(dynamic[1] // step):
        tag, value = unpack(end + word + word, data, dynamic[0] + step * i)
        if tag == DT_NULL:
            break
        if tag == DT_STRTAB:
            strtab = value
        entries.append((tag, value))
    if strtab is None:
        raise Unreadable("PT_DYNAMIC holds no DT_STRTAB")
    for vaddr, filesz, offset in loads:
        if vaddr <= strtab < vaddr + filesz:
            strings = offset + strtab - vaddr
            break
    else:
        raise Unreadable(f"DT_STRTAB's address {strtab:#x} is in no PT_LOAD")
    names = tuple(cstring(data, strings + v) for t, v in entries if t == DT_NEEDED)
    paths = tuple(cstring(data, strings + v) for t, v in entries if t in (DT_RPATH, DT_RUNPATH))
    return Imports("elf", names, interp, paths)


def read(data):
    """The imports of an image's bytes, PE or ELF."""
    try:
        if data[:2] == b"MZ":
            return pe(data)
        if data[:4] == b"\x7fELF":
            return elf(data)
    except (IndexError, struct.error) as e:
        raise Unreadable(str(e)) from e
    raise Unreadable("neither PE nor ELF")


def read_file(path):
    return read(Path(path).read_bytes())


def outside(imports, system, sanitize):
    """Each import on neither the system's list nor the preset's residual,
    given the build's QIVEN3_SANITIZE."""
    if system == "windows":
        allowed = set(WINDOWS_SYSTEM)
        if "fuzzer" in sanitize:
            allowed |= FUZZ_DLLS
        elif "address" in sanitize:
            allowed.add(ASAN_DLL)
        return [n for n in imports.names
                if n.lower() not in allowed and not ("fuzzer" in sanitize and n.lower().startswith(FUZZ_PREFIX))]
    allowed = set(LINUX_SYSTEM) | (MSAN_LIBS if sanitize == "memory" else set())
    if sanitize:
        allowed |= SANITIZER_LIBS
    return [n for n in imports.names if n not in allowed]


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
    if found.interp:
        print(f"interp {found.interp}")
    for path in found.run_paths:
        print(f"run path {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
