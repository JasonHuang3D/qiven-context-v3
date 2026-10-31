"""K19's reader: what the build's hardening leaves in a Linux binary, read
from its ELF64 bytes by the standard library alone, no tool.

- PIE: the header's ET_DYN, with DT_FLAGS_1's DF_1_PIE;
- relro and now: a PT_GNU_RELRO segment, and DT_FLAGS' DF_BIND_NOW,
  DT_FLAGS_1's DF_1_NOW or DT_BIND_NOW;
- the build id: an NT_GNU_BUILD_ID note of a PT_NOTE segment;
- CET: the GNU property note's GNU_PROPERTY_X86_FEATURE_1_AND, its IBT
  and SHSTK bits;
- the stack protector and fortify: the undefined symbols of the dynamic
  symbol table, __stack_chk_fail and glibc's __*_chk functions, read
  through the section headers' SHT_DYNSYM and its string table.

Each field's place in the file is given too (segments, entries, notes),
which the case's controls edit in a copy. tests/structure/test_hardening.py
holds each binary of a build to them. Run as a script it prints what a
binary holds, one a line.
"""

import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from tests.structure.imports import Unreadable, cstring, unpack

ET_DYN = 3
PT_DYNAMIC, PT_NOTE, PT_GNU_RELRO = 2, 4, 0x6474E552
DT_NULL, DT_BIND_NOW, DT_FLAGS, DT_FLAGS_1 = 0, 24, 30, 0x6FFFFFFB
DF_BIND_NOW, DF_1_NOW, DF_1_PIE = 0x8, 0x1, 0x08000000
SHT_DYNSYM, SHN_UNDEF = 11, 0
NT_GNU_BUILD_ID, NT_GNU_PROPERTY_TYPE_0 = 3, 5
GNU = b"GNU\0"
X86_FEATURE_1_AND = 0xC0000002
IBT, SHSTK = 0x1, 0x2
STACK_CHK_FAIL = "__stack_chk_fail"
PHDR, DYN, SHDR, SYM = "<IIQQQQQQ", "<QQ", "<IIQQQQIIQQ", "<IBBHQQ"


@dataclass(frozen=True)
class Hardening:
    pie: bool  # ET_DYN with DF_1_PIE
    relro: bool  # a PT_GNU_RELRO segment
    now: bool  # DF_BIND_NOW, DF_1_NOW or DT_BIND_NOW
    build_id: str  # NT_GNU_BUILD_ID's bytes in hex, or ""
    x86_features: int | None  # X86_FEATURE_1_AND's bits, or None without the property
    undefined: tuple  # each undefined dynamic symbol's name, in the table's order


def segments(data):
    """Each program header: (its own offset, type, offset, size, alignment)."""
    if data[:4] != b"\x7fELF" or data[4:6] != b"\x02\x01":
        raise Unreadable("no 64-bit little-endian ELF image")
    phoff, = unpack("<Q", data, 0x20)
    phentsize, phnum = unpack("<HH", data, 0x36)
    found = []
    for i in range(phnum):
        at = phoff + phentsize * i
        kind, _, offset, _, _, size, _, align = unpack(PHDR, data, at)
        found.append((at, kind, offset, size, align))
    return found


def entries(data):
    """Each PT_DYNAMIC entry before DT_NULL: (its offset, tag, value)."""
    found = []
    for _, kind, offset, size, _ in segments(data):
        if kind == PT_DYNAMIC:
            for at in range(offset, offset + size - size % 16, 16):
                tag, value = unpack(DYN, data, at)
                if tag == DT_NULL:
                    break
                found.append((at, tag, value))
    return found


def notes(data):
    """Each note of each PT_NOTE segment: (its offset, name, type,
    descriptor's offset, descriptor), the descriptor and the next note at
    the segment's alignment, 4 or 8, from the note's start."""
    found = []
    for _, kind, offset, size, align in segments(data):
        if kind != PT_NOTE:
            continue
        step, at = 8 if align == 8 else 4, offset
        while at + 12 <= offset + size:
            namesz, descsz, note = unpack("<III", data, at)
            desc_at = at + (12 + namesz + step - 1) // step * step
            found.append((at, data[at + 12:at + 12 + namesz], note, desc_at, data[desc_at:desc_at + descsz]))
            at = desc_at + (descsz + step - 1) // step * step
    return found


def properties(descriptor):
    """Each property of a GNU property note's descriptor, 8-aligned (ELF64):
    (its data's offset in the descriptor, type, data)."""
    found, at = [], 0
    while at + 8 <= len(descriptor):
        kind, size = struct.unpack_from("<II", descriptor, at)
        found.append((at + 8, kind, descriptor[at + 8:at + 8 + size]))
        at += 8 + (size + 7) // 8 * 8
    return found


def undefined_symbols(data):
    """The undefined symbols of SHT_DYNSYM, by its sh_link's string table."""
    shoff, = unpack("<Q", data, 0x28)
    shentsize, shnum = unpack("<HH", data, 0x3A)
    sections = [unpack(SHDR, data, shoff + shentsize * i) for i in range(shnum)]
    for _, kind, _, _, offset, size, link, _, _, entsize in sections:
        if kind == SHT_DYNSYM:
            if not entsize or link >= len(sections):
                raise Unreadable("SHT_DYNSYM without its entry size or string table")
            strings = sections[link][4]
            names = []
            for i in range(1, size // entsize):
                name, _, _, shndx, _, _ = unpack(SYM, data, offset + entsize * i)
                if shndx == SHN_UNDEF:
                    names.append(cstring(data, strings + name))
            return tuple(names)
    return ()


def read(data):
    """What a 64-bit little-endian ELF image holds of the hardening."""
    try:
        kind, = unpack("<H", data, 0x10)
        relro = any(s[1] == PT_GNU_RELRO for s in segments(data))
        build_id, features = "", None
        for _, name, note, _, descriptor in notes(data):
            if name == GNU and note == NT_GNU_BUILD_ID:
                build_id = descriptor.hex()
            elif name == GNU and note == NT_GNU_PROPERTY_TYPE_0:
                for _, prop, value in properties(descriptor):
                    if prop == X86_FEATURE_1_AND and len(value) >= 4:
                        features = struct.unpack_from("<I", value)[0]
        tags = {tag: value for _, tag, value in entries(data)}
        flags, flags_1 = tags.get(DT_FLAGS, 0), tags.get(DT_FLAGS_1, 0)
        return Hardening(pie=kind == ET_DYN and bool(flags_1 & DF_1_PIE), relro=relro,
                         now=bool(flags & DF_BIND_NOW or flags_1 & DF_1_NOW or DT_BIND_NOW in tags),
                         build_id=build_id, x86_features=features, undefined=undefined_symbols(data))
    except (IndexError, struct.error) as e:
        raise Unreadable(str(e)) from e


def read_file(path):
    return read(Path(path).read_bytes())


def checked(name):
    """Whether a symbol is one of glibc's checking functions, fortify's."""
    return name.startswith("__") and name.endswith("_chk") and name != STACK_CHK_FAIL


def missing(hardening, cet=True):
    """What a binary lacks of the hardening every one carries, by name:
    pie, relro, now, build id, stack protector; with cet, ibt and shstk."""
    held = (("pie", hardening.pie), ("relro", hardening.relro), ("now", hardening.now),
            ("build id", bool(hardening.build_id)), ("stack protector", STACK_CHK_FAIL in hardening.undefined))
    lacks = [name for name, there in held if not there]
    if cet:
        bits = hardening.x86_features or 0
        lacks += [name for name, bit in (("ibt", IBT), ("shstk", SHSTK)) if not bits & bit]
    return lacks


def main(argv):
    if len(argv) != 1:
        print("usage: hardening.py BINARY", file=sys.stderr)
        return 2
    try:
        found = read_file(argv[0])
    except (OSError, Unreadable) as e:
        print(f"hardening.py: {argv[0]}: {e}", file=sys.stderr)
        return 1
    print(f"pie {found.pie}\nrelro {found.relro}\nnow {found.now}\nbuild id {found.build_id or 'none'}")
    print("x86 features none" if found.x86_features is None else f"x86 features {found.x86_features:#x}")
    for name in found.undefined:
        if name == STACK_CHK_FAIL or checked(name):
            print(f"import {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
