"""K11: each binary of the build's binaries.txt imports only its system's
libraries, or its preset's residual, read by tests/structure/imports.py
from its bytes; on Linux it names its loader, holds no run path, and needs
no glibc symbol version past the floor's, GLIBC_2.39. The controls:
qiven3-fault-import, which imports the test tree's own library, is caught;
on Linux, so is a copy of a built binary with one version need renamed
GLIBC_9.99. The reader's own controls: images made here, each import, run
path and version need found."""

import struct
import sys
import tempfile
import unittest
from pathlib import Path

from tests.drive import binaries
from tests.drive.session import build_option
from tests.structure import imports

WINDOWS = sys.platform == "win32"
SYSTEM = "windows" if WINDOWS else "linux"
PLANTED = "qiven3-fault-import"
PLANTED_LIBRARY = "qiven3_fault_lib.dll" if WINDOWS else "libqiven3_fault_lib.so"
LOADER = "/lib64/ld-linux-x86-64.so.2"
PAST = "GLIBC_9.99"  # the control's need, past every floor


def listed():
    build = binaries.directory("QIVEN3_BUILD")
    text = (build / "binaries.txt").read_text(encoding="utf-8")
    return [Path(line).resolve() for line in text.splitlines() if line.strip()]


class Imports(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_K11_binaries(self):
        sanitize = build_option("QIVEN3_SANITIZE") or ""
        found, past = {}, {}
        for binary in listed():
            read = imports.read_file(binary)
            self.assertEqual(read.kind, "pe" if WINDOWS else "elf", binary)
            if not WINDOWS:
                self.assertEqual((read.interp, read.run_paths), (LOADER, ()), binary)
                self.assertTrue(imports.newest_glibc(read), binary)
                past[binary.stem] = imports.past_floor(read)
            self.assertIn("kernel32.dll" if WINDOWS else "libc.so.6", [n.lower() for n in read.names], binary)
            found[binary.stem] = imports.outside(read, SYSTEM, sanitize)
        self.assertIn(PLANTED, found)
        # The control: the planted binary's import of the test tree's library, caught.
        self.assertEqual(found.pop(PLANTED), [PLANTED_LIBRARY])
        self.assertEqual({k: v for k, v in found.items() if v}, {})
        self.assertEqual({k: v for k, v in past.items() if v}, {})

    @unittest.skipUnless(sys.platform.startswith("linux"), "glibc's symbol versions are Linux's")
    def test_K11_glibc_control(self):
        """A copy of a built binary, one glibc need renamed past the floor,
        is caught."""
        for binary in listed():
            data = binary.read_bytes()
            needs = [n for _, n in imports.read(data).versions
                     if n.startswith(imports.GLIBC_PREFIX) and len(n) >= len(PAST)]
            if needs:
                break
        else:
            self.fail("no built binary needs a glibc version to rename")
        old = b"\0" + needs[0].encode() + b"\0"
        new = b"\0" + PAST.encode().ljust(len(old) - 2, b"\0") + b"\0"
        self.assertIn(old, data, binary)
        with tempfile.TemporaryDirectory() as scratch:
            copy = Path(scratch) / binary.name
            copy.write_bytes(data.replace(old, new, 1))
            read = imports.read_file(copy)
        self.assertEqual(imports.past_floor(read), [PAST], binary)
        self.assertEqual(imports.past_floor(imports.read(data)), [], binary)

    def test_K11_lists(self):
        """The residuals by the build's sanitizers, case aside on Windows."""
        pe = imports.Imports("pe", ("KERNEL32.dll", "clang_rt.asan_dynamic-x86_64.dll", "VCRUNTIME140.dll",
                                    "api-ms-win-crt-heap-l1-1-0.dll"))
        self.assertEqual(imports.outside(pe, "windows", ""), list(pe.names[1:]))
        self.assertEqual(imports.outside(pe, "windows", "address"), list(pe.names[2:]))
        self.assertEqual(imports.outside(pe, "windows", "fuzzer,address"), [])
        elf = imports.Imports("elf", ("libc.so.6", "libc++.so.1", "libstdc++.so.6"))
        self.assertEqual(imports.outside(elf, "linux", "address,undefined"), ["libc++.so.1", "libstdc++.so.6"])
        self.assertEqual(imports.outside(elf, "linux", "memory"), ["libstdc++.so.6"])

    def test_K11_glibc_floor(self):
        """The floor's versions: 2.39 and below pass, past it or of no
        version's form caught, another library's versions not glibc's."""
        elf = imports.Imports("elf", ("libc.so.6",), versions=(
            ("libc.so.6", "GLIBC_2.2.5"), ("libc.so.6", "GLIBC_2.39"), ("libc.so.6", "GLIBC_2.40"),
            ("libm.so.6", "GLIBC_2.40"), ("libc.so.6", "GLIBC_3"), ("libc.so.6", "GLIBC_PRIVATE"),
            ("libstdc++.so.6", "GLIBCXX_3.4.33"), ("libgcc_s.so.1", "GCC_14.0")))
        self.assertEqual(imports.past_floor(elf), ["GLIBC_2.40", "GLIBC_3", "GLIBC_PRIVATE"])
        self.assertEqual(imports.newest_glibc(elf), "GLIBC_3")
        self.assertEqual(imports.newest_glibc(imports.Imports("elf", ())), "")


def pe_image(names, delayed):
    """A PE32+ image of one section at 0x1000, its import directory naming
    `names` and its delay-load directory `delayed`."""
    section_raw, va = 0x200, 0x1000
    body = bytearray(0x400)
    strings = 0x200  # within the section
    imports_at, delays_at = 0x0, 0x100

    def name(text):
        nonlocal strings
        at = strings
        body[at:at + len(text) + 1] = text.encode() + b"\0"
        strings += len(text) + 1
        return va + at

    for i, text in enumerate(names):
        struct.pack_into("<IIIII", body, imports_at + 20 * i, 0, 0, 0, name(text), 1)
    for i, text in enumerate(delayed):
        struct.pack_into("<8I", body, delays_at + 32 * i, 1, name(text), 0, 0, 0, 0, 0, 0)
    head = bytearray(section_raw)
    head[0:2] = b"MZ"
    struct.pack_into("<I", head, 0x3C, 0x40)
    head[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", head, 0x44, 0x8664, 1, 0, 0, 0, 240, 0x22)
    opt = 0x58
    struct.pack_into("<H", head, opt, 0x20B)
    struct.pack_into("<Q", head, opt + 24, 0x140000000)
    struct.pack_into("<I", head, opt + 108, 16)
    struct.pack_into("<II", head, opt + 112 + 8, va + imports_at, 20 * (len(names) + 1))
    struct.pack_into("<II", head, opt + 112 + 8 * 13, va + delays_at, 32 * (len(delayed) + 1))
    struct.pack_into("<8sIIII", head, opt + 240, b".idata", len(body), va, len(body), section_raw)
    return bytes(head + body)


def elf_image(needed, run_path=None, versions=()):
    """An ELF64 image: one PT_LOAD over the file, PT_INTERP, PT_DYNAMIC with
    DT_NEEDED for each of `needed`, DT_RUNPATH when given, and for each
    (library, names) of `versions` an Elf64_Verneed with a Vernaux a name,
    through DT_VERNEED and DT_VERNEEDNUM."""
    strings = b"\0" + LOADER.encode() + b"\0"
    offsets = {}

    def text(value):
        nonlocal strings
        if value not in offsets:
            offsets[value] = len(strings)
            strings += value.encode() + b"\0"
        return offsets[value]

    tags = [(imports.DT_NEEDED, text(n)) for n in needed]
    if run_path:
        tags.append((imports.DT_RUNPATH, text(run_path)))
    need = b""
    for i, (library, names) in enumerate(versions):
        following = 16 + 16 * len(names) if i + 1 < len(versions) else 0
        need += struct.pack("<HHIII", 1, len(names), text(library), 16, following)
        for j, name in enumerate(names):
            need += struct.pack("<IHHII", 0, 0, 2 + j, text(name), 16 if j + 1 < len(names) else 0)
    phoff, base = 64, 0x400000
    dyn_at = phoff + 3 * 56
    extra = 2 if versions else 0
    str_at = dyn_at + 16 * (len(tags) + 2 + extra)
    need_at = str_at + len(strings)
    if versions:
        tags += [(imports.DT_VERNEED, base + need_at), (imports.DT_VERNEEDNUM, len(versions))]
    tags += [(imports.DT_STRTAB, base + str_at), (imports.DT_NULL, 0)]
    size = need_at + len(need)
    head = bytearray(b"\x7fELF\x02\x01\x01" + bytes(9))
    head += struct.pack("<HHIQQQIHHHHHH", 3, 0x3E, 1, 0, phoff, 0, 0, 64, 56, 3, 0, 0, 0)
    head += struct.pack("<IIQQQQQQ", imports.PT_LOAD, 5, 0, base, base, size, size, 0x1000)
    interp = str_at + 1  # the loader's name, the strings' first
    head += struct.pack("<IIQQQQQQ", imports.PT_INTERP, 4, interp, base + interp, base + interp, len(LOADER) + 1, 0, 1)
    head += struct.pack("<IIQQQQQQ", imports.PT_DYNAMIC, 6, dyn_at, base + dyn_at, 0, 16 * len(tags), 0, 8)
    head += b"".join(struct.pack("<QQ", t, v) for t, v in tags)
    return bytes(head + strings + need)


class Reader(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_pe(self):
        read = imports.read(pe_image(["KERNEL32.dll", "qiven3_fault_lib.dll"], ["dbghelp.dll"]))
        self.assertEqual(read.names, ("KERNEL32.dll", "qiven3_fault_lib.dll", "dbghelp.dll"))
        self.assertEqual(imports.outside(read, "windows", ""), ["qiven3_fault_lib.dll"])

    def test_elf(self):
        read = imports.read(elf_image(["libc.so.6", "libqiven3_fault_lib.so"], "$ORIGIN"))
        self.assertEqual((read.names, read.interp, read.run_paths),
                         (("libc.so.6", "libqiven3_fault_lib.so"), LOADER, ("$ORIGIN",)))
        self.assertEqual(imports.read(elf_image(["libm.so.6"])).run_paths, ())

    def test_elf_versions(self):
        """Each Vernaux of each Verneed, in the file's order; the floor's
        check over them."""
        image = elf_image(["libc.so.6", "libm.so.6"], versions=(
            ("libc.so.6", ("GLIBC_2.2.5", "GLIBC_2.34")), ("libm.so.6", ("GLIBC_2.29",))))
        read = imports.read(image)
        self.assertEqual(read.versions, (("libc.so.6", "GLIBC_2.2.5"), ("libc.so.6", "GLIBC_2.34"),
                                         ("libm.so.6", "GLIBC_2.29")))
        self.assertEqual((imports.past_floor(read), imports.newest_glibc(read)), ([], "GLIBC_2.34"))
        planted = imports.read(image.replace(b"GLIBC_2.34", PAST.encode(), 1))
        self.assertEqual(imports.past_floor(planted), [PAST])
        self.assertEqual(imports.read(elf_image(["libc.so.6"])).versions, ())

    def test_unreadable(self):
        for data in (b"", b"MZ" + bytes(10), b"\x7fELF\x03\x01" + bytes(60), b"#!/bin/sh\n"):
            with self.assertRaises(imports.Unreadable, msg=data[:8]):
                imports.read(data)


if __name__ == "__main__":
    unittest.main()
