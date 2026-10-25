"""K19: what the build's hardening leaves in each Linux binary of the build's
binaries.txt, read by tests/structure/hardening.py from its bytes, beside
K11's imports, in the same jobs:
- PIE: ET_DYN with DF_1_PIE;
- relro and now: PT_GNU_RELRO, with DF_BIND_NOW or DF_1_NOW;
- the build id: an NT_GNU_BUILD_ID note;
- the stack protector: the import of __stack_chk_fail, by every binary,
  each holding a protected frame;
- CET: the GNU property note's X86_FEATURE_1_AND with IBT and SHSTK,
  where it survives: the presets by gcc. A residual by clang under a
  sanitizer (linux-clang, linux-tsan, linux-fuzz, linux-msan): clang's
  sanitizer runtimes, libclang_rt.asan, asan_static, asan_cxx, tsan,
  tsan_cxx, msan and msan_cxx, and the prefix's MemorySanitizer libc++.a,
  carry no property, so the linker drops it; there it is not checked;
- fortify: glibc's __*_chk imports, by at least one binary where the build
  optimizes and no sanitizer runs (linux-plain): fortify acts only where a
  size is known at compile time, as in SQLite's copies; none required
  elsewhere.
Stack-clash probes leave nothing in the headers, notes or symbols: they
are instructions, emitted only in a frame past the probe interval, a page,
or of a dynamic size, which the 4,096-byte frame bound leaves few of, and
the reader reads no instructions: a residual. On macOS (Mach-O) and on
Windows nothing is read. The controls: each check catches its absence
planted in a copy of a built binary's bytes, each catch logged; the
reader's own on an image made here.
"""

import struct
import sys
import unittest

from tests.drive.session import build_option
from tests.structure import hardening
from tests.structure.imports import Unreadable
from tests.structure.test_binaries import listed

LINUX = sys.platform.startswith("linux")
CLANG = "Clang"
OPTIMIZED = ("Release", "RelWithDebInfo", "MinSizeRel")
PRODUCT = "qiven3"
DT_DEBUG = 21  # a harmless tag in place of DT_BIND_NOW
NT_PLANTED = 0x7F  # a note type of no meaning


def preset():
    """What the build's preset holds: (cet expected, fortify expected)."""
    sanitize = build_option("QIVEN3_SANITIZE") or ""
    compiler = build_option("QIVEN3_CXX_COMPILER_ID") or ""
    optimized = (build_option("CMAKE_BUILD_TYPE") or "") in OPTIMIZED
    return not (sanitize and compiler == CLANG), optimized and not sanitize


def put(data, fmt, at, *values):
    copy = bytearray(data)
    struct.pack_into(fmt, copy, at, *values)
    return bytes(copy)


def no_pie(data):
    for at, tag, value in hardening.entries(data):
        if tag == hardening.DT_FLAGS_1:
            data = put(data, "<Q", at + 8, value & ~hardening.DF_1_PIE)
    return data


def no_relro(data):
    for at, kind, _, _, _ in hardening.segments(data):
        if kind == hardening.PT_GNU_RELRO:
            data = put(data, "<I", at, 0)  # PT_NULL
    return data


def no_now(data):
    for at, tag, value in hardening.entries(data):
        if tag == hardening.DT_FLAGS:
            data = put(data, "<Q", at + 8, value & ~hardening.DF_BIND_NOW)
        elif tag == hardening.DT_FLAGS_1:
            data = put(data, "<Q", at + 8, value & ~hardening.DF_1_NOW)
        elif tag == hardening.DT_BIND_NOW:
            data = put(data, "<Q", at, DT_DEBUG)
    return data


def no_build_id(data):
    for at, name, note, _, _ in hardening.notes(data):
        if name == hardening.GNU and note == hardening.NT_GNU_BUILD_ID:
            data = put(data, "<I", at + 8, NT_PLANTED)
    return data


def no_feature(bit):
    def plant(data):
        for _, name, note, desc_at, descriptor in hardening.notes(data):
            if name == hardening.GNU and note == hardening.NT_GNU_PROPERTY_TYPE_0:
                for value_at, prop, value in hardening.properties(descriptor):
                    if prop == hardening.X86_FEATURE_1_AND:
                        bits, = struct.unpack_from("<I", value)
                        data = put(data, "<I", desc_at + value_at, bits & ~bit)
        return data
    return plant


def renamed(data, names):
    """A copy with each name's last letter made X, wherever it is a whole
    string."""
    for name in names:
        old = b"\0" + name.encode() + b"\0"
        data = data.replace(old, old[:-2] + b"X\0")
    return data


def no_stack_protector(data):
    return renamed(data, [hardening.STACK_CHK_FAIL])


# Each plant and the name missing() gives its absence, CET's last.
PLANTS = (("pie", no_pie), ("relro", no_relro), ("now", no_now), ("build id", no_build_id),
          ("stack protector", no_stack_protector), ("ibt", no_feature(hardening.IBT)),
          ("shstk", no_feature(hardening.SHSTK)))


@unittest.skipUnless(LINUX, "ELF's hardening is Linux's: Mach-O is not read, Windows' binaries are PE")
class Hardening(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_K19_binaries(self):
        cet, fortify = preset()
        lacks, fortified = {}, []
        for binary in listed():
            read = hardening.read_file(binary)
            lacks[binary.stem] = hardening.missing(read, cet)
            if any(hardening.checked(n) for n in read.undefined):
                fortified.append(binary.stem)
        self.assertIn(PRODUCT, lacks)
        self.assertEqual({k: v for k, v in lacks.items() if v}, {})
        if fortify:
            self.assertNotEqual(fortified, [], "no binary imports a checking function under fortify")

    def test_K19_control(self):
        """Each check's absence, planted in a copy of a built binary's
        bytes, is caught; under fortify, a copy with its checking
        functions renamed imports none."""
        cet, fortify = preset()
        binaries = listed()
        binary = next(b for b in binaries if b.stem == PRODUCT)
        data = binary.read_bytes()
        self.assertEqual(hardening.missing(hardening.read(data), cet), [], binary)
        for name, plant in PLANTS if cet else PLANTS[:-2]:
            copy = plant(data)
            self.assertNotEqual(copy, data, f"{name}: nothing planted in {binary.name}")
            self.assertEqual(hardening.missing(hardening.read(copy), cet), [name], binary)
            print(f"K19 control: {name} planted absent in a copy of {binary.name}: caught")
        if fortify:
            for each in binaries:
                read = hardening.read_file(each)
                names = [n for n in read.undefined if hardening.checked(n)]
                if names:
                    break
            else:
                self.fail("no binary imports a checking function under fortify")
            copy = hardening.read(renamed(each.read_bytes(), names))
            self.assertEqual([n for n in copy.undefined if hardening.checked(n)], [], each)
            print(f"K19 control: fortify's {len(names)} imports renamed in a copy of {each.name}: caught")


def elf_image():
    """An ELF64 image holding each mark: ET_DYN; PT_DYNAMIC with DT_FLAGS
    BIND_NOW and DT_FLAGS_1 NOW and PIE; a build id note, 4-aligned; a GNU
    property note, 8-aligned, X86_FEATURE_1_AND IBT and SHSTK after another
    property; PT_GNU_RELRO; SHT_DYNSYM with two undefined symbols,
    __stack_chk_fail and __memcpy_chk, and one defined."""
    dyn_at, phnum = 64 + 4 * 56, 4
    dynamic = struct.pack("<6Q", hardening.DT_FLAGS, hardening.DF_BIND_NOW, hardening.DT_FLAGS_1,
                          hardening.DF_1_NOW | hardening.DF_1_PIE, 0, 0)
    id_at = dyn_at + len(dynamic)
    build_id = struct.pack("<III", 4, 8, hardening.NT_GNU_BUILD_ID) + hardening.GNU + bytes(range(8))
    property_at = id_at + len(build_id)
    props = struct.pack("<IIII", 0xC0008002, 4, 1, 0) + struct.pack(
        "<IIII", hardening.X86_FEATURE_1_AND, 4, hardening.IBT | hardening.SHSTK, 0)
    prop = struct.pack("<III", 4, len(props), hardening.NT_GNU_PROPERTY_TYPE_0) + hardening.GNU + props
    str_at = property_at + len(prop)
    strings = b"\0__stack_chk_fail\0__memcpy_chk\0main\0"
    sym_at = str_at + len(strings) + (-(str_at + len(strings)) % 8)
    symbols = bytes(24) + struct.pack(hardening.SYM, 1, 0x12, 0, 0, 0, 0) + struct.pack(
        hardening.SYM, 18, 0x12, 0, 0, 0, 0) + struct.pack(hardening.SYM, 31, 0x12, 0, 14, 0x1000, 8)
    sh_at = sym_at + len(symbols)
    head = bytearray(b"\x7fELF\x02\x01\x01" + bytes(9))
    head += struct.pack("<HHIQQQIHHHHHH", hardening.ET_DYN, 0x3E, 1, 0, 64, sh_at, 0, 64, 56, phnum, 64, 3, 1)
    for kind, offset, size, align in ((hardening.PT_DYNAMIC, dyn_at, len(dynamic), 8),
                                      (hardening.PT_NOTE, id_at, len(build_id), 4),
                                      (hardening.PT_NOTE, property_at, len(prop), 8),
                                      (hardening.PT_GNU_RELRO, dyn_at, len(dynamic), 1)):
        head += struct.pack(hardening.PHDR, kind, 4, offset, offset, offset, size, size, align)
    body = dynamic + build_id + prop + strings + bytes(sym_at - str_at - len(strings)) + symbols
    sections = bytes(64) + struct.pack(hardening.SHDR, 0, 3, 0, 0, str_at, len(strings), 0, 0, 1, 0) + struct.pack(
        hardening.SHDR, 0, hardening.SHT_DYNSYM, 0, 0, sym_at, len(symbols), 1, 1, 8, 24)
    return bytes(head) + body + sections


class Reader(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_hardening_elf(self):
        image = elf_image()
        read = hardening.read(image)
        self.assertEqual(read, hardening.Hardening(True, True, True, bytes(range(8)).hex(), 0x3,
                                                   ("__stack_chk_fail", "__memcpy_chk")))
        self.assertEqual(hardening.missing(read), [])
        self.assertEqual([n for n in read.undefined if hardening.checked(n)], ["__memcpy_chk"])

    def test_hardening_plants(self):
        """Each plant on the image made here, caught alone; without CET's
        check its bits' absence is not named."""
        image = elf_image()
        for name, plant in PLANTS:
            with self.subTest(plant=name):
                read = hardening.read(plant(image))
                self.assertEqual(hardening.missing(read), [name])
                self.assertEqual(hardening.missing(read, cet=False), [] if name in ("ibt", "shstk") else [name])
        executable = put(image, "<H", 0x10, 2)  # ET_EXEC
        self.assertEqual(hardening.missing(hardening.read(executable)), ["pie"])
        fortify = hardening.read(renamed(image, ["__memcpy_chk"]))
        self.assertEqual([n for n in fortify.undefined if hardening.checked(n)], [])

    def test_hardening_unreadable(self):
        for data in (b"", b"\x7fELF\x01\x01" + bytes(60), b"\x7fELF\x02\x02" + bytes(60), b"MZ" + bytes(62),
                     elf_image()[:200]):
            with self.assertRaises(Unreadable, msg=data[:8]):
                hardening.read(data)


if __name__ == "__main__":
    unittest.main()
