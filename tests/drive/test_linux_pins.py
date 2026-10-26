"""The Linux toolchain's check, tools/linux_pins.py, on a fake prefix and
fake dpkg and apt answers each case makes in a scratch directory of its
own: a pin file of every kind over them holds; then each rule's plant is
caught, named in its mismatch, with exit 2: a digest changed (a download, a
command, the libc++ archive, a deb's index), a version changed (dpkg's,
--version's, _LIBCPP_VERSION's), a package of no compiler's source held, a
compiler's package not held or pinned by no deb, a pinned file missing (a
download, a command, libc++, a deb, dpkg itself), a removed package
installed; one whose configuration files alone are left is absent. An
entry of the prefix, or of its downloads, that no pin names is noted, the
check still passing. A malformed removed line refuses. The pin file
itself parses, its removed packages pinned by no deb line, and its binutils
packages hold over dpkg and apt answering as pinned, a version off and a
digest off each caught.

On each system the check's commands are answered in its own process from
the scratch's files; inside Linux once more by the check's own process,
over a fake dpkg-query, apt-cache and tool on PATH, the tool reached through
a link into its install's directory."""

import contextlib
import hashlib
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

from tools import linux_pins

ROOT = Path(__file__).resolve().parent.parent.parent
PIN_FILE = ROOT / "tools" / "linux-pins.txt"
URL = "https://example.invalid/releases/tool-1.0.tar.gz"
TOOL = "#!/bin/sh\necho 'tool version {version}'\n"
CONFIG = "#ifndef CONFIG\n#  define _LIBCPP_VERSION {number}\n#endif\n"
# binutils' packages whose tools the builds and cases run, or which they load.
BINUTILS = ("binutils", "binutils-common", "binutils-x86-64-linux-gnu", "libbinutils", "libctf0", "libsframe1")
DPKG = 'shift 3\ncat "$FAKE/dpkg.txt"\n'
APT = 'line=$(grep "^$2 " "$FAKE/apt.txt") || exit 100\necho "SHA256: ${line#* }"\n'
GCC = "13.3.0-6ubuntu2~24.04.1"  # a pinned compiler's package, of a held source


def sha(data):
    return hashlib.sha256(data).hexdigest()


class World:
    """A scratch prefix with a download, a command, a tool on PATH and
    libc++, and dpkg's and apt's answers as files: dpkg.txt in dpkg-query's
    form, apt.txt NAME=VERSION and the index's digest a line."""

    def __init__(self, case):
        self.top = Path(tempfile.mkdtemp(prefix="qiven3-linux-pins-")).resolve()
        case.addCleanup(shutil.rmtree, self.top, ignore_errors=True)
        self.prefix = self.top / "prefix"
        self.download = self.write(self.prefix / "downloads" / URL.rsplit("/", 1)[-1], b"the release archive\n")
        self.tool = self.write(self.prefix / "tool-1.0" / "bin" / "tool", TOOL.format(version="1.0").encode())
        self.make = self.write(self.top / "path" / "make", TOOL.format(version="4.3").encode())
        self.libcxx = self.prefix / "libcxx-msan-18.1.3"
        self.archive = self.write(self.libcxx / "lib" / "libc++.a", b"the archive\n")
        self.config = self.write(self.libcxx / "include" / "c++" / "v1" / "__config",
                                 CONFIG.format(number=180100).encode())
        self.linked = False
        try:
            (self.prefix / "bin").mkdir()
            os.symlink(self.tool, self.prefix / "bin" / "tool")
            self.linked = True
        except OSError:  # no right to link: the command named in its own directory
            shutil.rmtree(self.prefix / "bin", ignore_errors=True)
        self.dpkg = {"make": "4.3-4.1build2 make-dfsg ii ", "libc6": "2.39-0ubuntu8.9 glibc ii ",
                     "gcc-14-base": "14.2.0-4ubuntu2 gcc-14 ii ", "gcc-13": f"{GCC} gcc-13 hi "}
        self.apt = {"make=4.3-4.1build2": sha(b"make deb"), "libc6=2.39-0ubuntu8.9": sha(b"libc6 deb"),
                    f"gcc-13={GCC}": sha(b"gcc-13 deb")}
        self.dpkg_on_path = True
        self.save()

    @staticmethod
    def write(path, data):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        path.chmod(0o755)
        return path

    def save(self):
        (self.top / "dpkg.txt").write_text("".join(f"{n} {v}\n" for n, v in self.dpkg.items()), encoding="utf-8")
        (self.top / "apt.txt").write_text("".join(f"{k} {v}\n" for k, v in self.apt.items()), encoding="utf-8")

    def pins(self, extra=""):
        tool = "prefix/bin/tool" if self.linked else "prefix/tool-1.0/bin/tool"
        return "\n".join((
            "# a pin of each kind",
            f"download tool 1.0 {sha(self.download.read_bytes())} {URL}",
            f"command tool 1.0 {sha(self.tool.read_bytes())} {tool}",
            f"command make 4.3 {sha(self.make.read_bytes())} path/make",
            f"libcxx libcxx-msan 18.1.3 {sha(self.archive.read_bytes())} prefix/libcxx-msan-18.1.3",
            f"deb make 4.3-4.1build2 {self.apt['make=4.3-4.1build2']} ubuntu:noble/main",
            f"deb libc6 2.39-0ubuntu8.9 {self.apt['libc6=2.39-0ubuntu8.9']} ubuntu:noble-updates/main",
            f"deb gcc-13 {GCC} {self.apt[f'gcc-13={GCC}']} ubuntu:noble-updates/main",
            f"debugger gdb 15.1 {sha(b'gdb')} path/gdb",
            "removed gcc-14  # the old compiler", "removed libstdc++-14-dev", extra)) + "\n"

    def which(self, name):
        if name in ("dpkg-query", "apt-cache"):
            return f"/usr/bin/{name}" if self.dpkg_on_path else None
        found = self.top / "path" / name
        return str(found) if found.is_file() else None

    def run(self, argv):
        """The check's commands answered from the scratch's files."""
        if argv[0] == "dpkg-query":
            return 0, (self.top / "dpkg.txt").read_text(encoding="utf-8")
        if argv[0] == "apt-cache":
            for line in (self.top / "apt.txt").read_text(encoding="utf-8").splitlines():
                if line.split()[0] == argv[2]:
                    return 0, f"Package: {argv[2]}\nSHA256: {line.split()[1]}\n"
            return 100, f"E: No packages found for {argv[2]}\n"
        said = re.search(r"echo '([^']*)'", Path(argv[0]).read_text(encoding="utf-8"))
        return 0, said.group(1) + "\n"

    def check(self, pins=None):
        """The check in this process: its status and lines."""
        (self.top / "pins.txt").write_text(self.pins() if pins is None else pins, encoding="utf-8")
        out = io.StringIO()
        with mock.patch.object(linux_pins, "run", self.run), \
                mock.patch.object(linux_pins, "shutil", types.SimpleNamespace(which=self.which)), \
                contextlib.redirect_stdout(out):
            status = linux_pins.main(["--prefix", str(self.prefix), "--pins", str(self.top / "pins.txt")])
        return status, out.getvalue().splitlines()


class Check(unittest.TestCase):
    """Each rule of the check, a plant caught."""
    ctest_labels = ("fake",)

    ctest_reads = ("tools/linux-pins.txt",)

    def caught(self, plant, pattern):
        """The plant caught: the pins taken from the world as it stood
        before it, then the plant, exit 2 and its mismatch."""
        world = World(self)
        pins = world.pins()
        plant(world)
        world.save()
        status, lines = world.check(pins)
        self.assertEqual(status, 2, lines)
        self.assertTrue(any(re.search(pattern, line) for line in lines), (pattern, lines))
        return lines

    def test_holds(self):
        world = World(self)
        status, lines = world.check()
        self.assertEqual(status, 0, lines)
        self.assertEqual(lines[-1], "linux-pins: 8 pins, 2 removed, 0 mismatches")
        self.assertEqual([x for x in lines if "note" in x],
                         ["linux-pins: note: debugger gdb: absent from PATH; its debugger cases skip by name"])

    def test_digest_changed(self):
        for name, plant, pattern in (
                ("download", lambda w: w.download.write_bytes(b"another archive\n"),
                 r"mismatch: download tool: tool-1\.0\.tar\.gz's SHA-256 is [0-9a-f]{64}; the pin"),
                ("command", lambda w: w.tool.write_bytes(w.tool.read_bytes() + b"# grown\n"),
                 r"mismatch: command tool: tool's SHA-256 is "),
                ("libc++", lambda w: w.archive.write_bytes(b"another archive\n"),
                 r"mismatch: libcxx libcxx-msan: libc\+\+\.a's SHA-256 is "),
                ("index", lambda w: w.apt.update({"make=4.3-4.1build2": sha(b"another deb")}),
                 r"mismatch: deb make 4\.3-4\.1build2: the index's SHA-256 is " + sha(b"another deb"))):
            with self.subTest(name):
                self.caught(plant, pattern)

    def test_version_changed(self):
        for name, plant, pattern in (
                ("dpkg", lambda w: w.dpkg.update({"libc6": "2.39-0ubuntu8.10 glibc ii "}),
                 r"mismatch: deb libc6: installed 2\.39-0ubuntu8\.10; the pin is 2\.39-0ubuntu8\.9"),
                ("--version", lambda w: w.make.write_bytes(TOOL.format(version="4.3.1").encode()),
                 r"mismatch: command make: --version gives 'tool version 4\.3\.1'; the pin is 4\.3$"),
                ("_LIBCPP_VERSION", lambda w: w.config.write_bytes(CONFIG.format(number=180200).encode()),
                 r"mismatch: libcxx libcxx-msan: _LIBCPP_VERSION is 180200; the pin 180100"),
                ("_LIBCPP_VERSION's patch level", lambda w: w.config.write_bytes(CONFIG.format(number=180103).encode()),
                 r"mismatch: libcxx libcxx-msan: _LIBCPP_VERSION is 180103; the pin 180100")):
            with self.subTest(name):
                self.caught(plant, pattern)

    def test_version_alone(self):
        """--version off, the file's digest pinned as it is now: the
        version's mismatch alone."""
        world = World(self)
        world.make.write_bytes(TOOL.format(version="4.3.1").encode())
        status, lines = world.check()
        self.assertEqual(status, 2, lines)
        self.assertEqual([x for x in lines if "mismatch:" in x],
                         ["linux-pins: mismatch: command make: --version gives 'tool version 4.3.1'; the pin is 4.3"])

    def test_held(self):
        """The compilers' hold: a package of no compiler's source held, a
        pinned compiler's package not held, and an installed package of a
        compiler's source pinned by no deb, each caught."""
        self.caught(lambda w: w.dpkg.update({"gcc-14-base": "14.2.0-4ubuntu2 gcc-14 hi "}),
                    r"mismatch: deb gcc-14-base: held, but no package of gcc-13 or llvm-toolchain-18: only the "
                    r"compilers are held, the system's updates stay on \(apt-mark unhold gcc-14-base\)$")
        self.caught(lambda w: w.dpkg.update({"make": "4.3-4.1build2 make-dfsg hi "}), r"mismatch: deb make: held")
        self.caught(lambda w: w.dpkg.update({"gcc-13": f"{GCC} gcc-13 ii "}),
                    r"mismatch: deb gcc-13: a package of gcc-13, not held \(apt-mark hold gcc-13\)$")
        lines = self.caught(lambda w: w.dpkg.update({"llvm-18-dev": "1:18.1.3-1ubuntu1 llvm-toolchain-18 ii "}),
                            r"mismatch: deb llvm-18-dev: a package of llvm-toolchain-18, "
                            r"installed 1:18\.1\.3-1ubuntu1, pinned as no deb$")
        self.assertIn("linux-pins: mismatch: deb llvm-18-dev: a package of llvm-toolchain-18, not held "
                      "(apt-mark hold llvm-18-dev)", lines)

    def test_missing(self):
        for name, plant, pattern in (
                ("download", lambda w: w.download.unlink(),
                 r"mismatch: download tool: tool-1\.0\.tar\.gz is missing from the prefix's downloads"),
                ("command", lambda w: w.tool.unlink(), r"mismatch: command tool: missing \(prefix/"),
                ("on PATH", lambda w: w.make.unlink(), r"mismatch: command make: missing \(path/make\)"),
                ("libc++", lambda w: shutil.rmtree(w.libcxx),
                 r"mismatch: libcxx libcxx-msan: missing \(no lib/libc\+\+\.a or include/c\+\+/v1/__config"),
                ("deb", lambda w: w.dpkg.pop("libc6"), r"mismatch: deb libc6: not installed; the pin is 2\.39"),
                ("dpkg", lambda w: setattr(w, "dpkg_on_path", False),
                 r"mismatch: deb: dpkg-query or apt-cache is not on PATH")):
            with self.subTest(name):
                self.caught(plant, pattern)

    def test_removed_present(self):
        self.caught(lambda w: w.dpkg.update({"libstdc++-14-dev": "14.2.0-4ubuntu2~24.04.1 gcc-14 ii "}),
                    r"mismatch: removed libstdc\+\+-14-dev: installed 14\.2\.0-4ubuntu2~24\.04\.1, where the removal "
                    r"took it")
        world = World(self)  # removed, its configuration files kept: not installed
        world.dpkg.update({"gcc-14": "14.2.0-4ubuntu2 gcc-14 rc "})
        world.save()
        status, lines = world.check()
        self.assertEqual(status, 0, lines)

    def test_leftovers_noted(self):
        world = World(self)
        (world.prefix / "libcxx-msan-23.1.4" / "lib").mkdir(parents=True)
        world.write(world.prefix / "downloads" / "llvm-project-23.1.4.src.tar.xz", b"old\n")
        status, lines = world.check()
        self.assertEqual(status, 0, lines)
        self.assertIn("linux-pins: note: the prefix: libcxx-msan-23.1.4 is named by no pin", lines)
        self.assertIn("linux-pins: note: the downloads: llvm-project-23.1.4.src.tar.xz is named by no pin", lines)
        self.assertEqual(sum("named by no pin" in x for x in lines), 2, lines)

    def test_malformed(self):
        world = World(self)
        for extra, pattern in (("removed gcc-14 14.2.0", r"malformed: line 12: 3 fields, not 2$"),
                               ("removed libc6", r"malformed: line 12: libc6 both removed and pinned as a deb$"),
                               ("removed libstdc++-14-dev",
                                r"malformed: line 12: removed libstdc\+\+-14-dev pinned twice$")):
            with self.subTest(extra):
                status, lines = world.check(world.pins(extra))
                self.assertEqual(status, 2, lines)
                self.assertRegex(lines[0], pattern)


class PinFile(unittest.TestCase):
    """The pin file parses; its removed packages are gcc-14's compilers' and
    their development files', and its compilers the archive's, on PATH."""
    ctest_labels = ("fake",)

    ctest_reads = ("tools/linux-pins.txt",)

    def test_parses(self):
        pins, findings = linux_pins.parse(PIN_FILE.read_text(encoding="utf-8"))
        self.assertEqual(findings, [])
        removed = {p[2] for p in pins if p[1] == "removed"}
        self.assertEqual(removed, {"cpp-14", "cpp-14-x86-64-linux-gnu", "g++-14", "g++-14-x86-64-linux-gnu", "gcc-14",
                                   "gcc-14-x86-64-linux-gnu", "libgcc-14-dev", "libstdc++-14-dev"})
        self.assertEqual(len(pins) - len(removed), 77)
        commands = {p[2]: p[5] for p in pins if p[1] == "command"}
        for name in ("gcc-13", "g++-13", "clang-18", "clang++-18", "llvm-symbolizer-18"):
            self.assertEqual(commands.get(name), "path/" + name)
        debs = {p[2] for p in pins if p[1] == "deb"}
        self.assertTrue({"gcc-13", "g++-13", "clang-18", "libclang-rt-18-dev", "llvm-18"} <= debs, debs)

    def test_binutils(self):
        """The binutils packages the builds run, pinned by the archive
        index's digest at one version, hold over dpkg and apt answering as
        pinned; a version off and a digest off are each caught."""
        pins, _ = linux_pins.parse(PIN_FILE.read_text(encoding="utf-8"))
        debs = [p for p in pins if p[1] == "deb" and p[2] in BINUTILS]
        self.assertEqual(sorted(p[2] for p in debs), sorted(BINUTILS))
        version = debs[0][3]
        self.assertTrue(all(p[3] == version and p[5] == "ubuntu:noble-updates/main" for p in debs), debs)
        text = "".join(f"deb {p[2]} {p[3]} {p[4]} {p[5]}\n" for p in debs)
        for name, plant, pattern in (
                ("holds", None, None),
                ("version", lambda w: w.dpkg.update({"binutils": "2.42-4ubuntu2.99 binutils ii "}),
                 r"mismatch: deb binutils: installed 2\.42-4ubuntu2\.99; the pin is " + re.escape(version) + "$"),
                ("digest", lambda w: w.apt.update({f"libbinutils={version}": sha(b"another deb")}),
                 r"mismatch: deb libbinutils " + re.escape(version) + ": the index's SHA-256 is "
                 + sha(b"another deb") + "; the pin [0-9a-f]{64}$")):
            with self.subTest(name):
                world = World(self)
                world.dpkg = {p[2]: f"{version} binutils ii " for p in debs}
                world.apt = {f"{p[2]}={version}": p[4] for p in debs}
                if plant:
                    plant(world)
                world.save()
                status, lines = world.check(text)
                if plant is None:
                    self.assertEqual(status, 0, lines)
                    self.assertEqual(lines[-1], f"linux-pins: {len(BINUTILS)} pins, 0 removed, 0 mismatches")
                else:
                    self.assertEqual(status, 2, lines)
                    self.assertEqual(len([x for x in lines if "mismatch:" in x]), 1, lines)
                    self.assertTrue(any(re.search(pattern, x) for x in lines), (pattern, lines))


@unittest.skipIf(sys.platform == "win32", "not applicable on Windows: the fakes on PATH are POSIX shell scripts")
class Process(unittest.TestCase):
    """The check's own process over fakes on PATH."""
    ctest_labels = ("fake",)

    ctest_reads = ("tools/linux-pins.txt",)

    def test_process(self):
        world = World(self)
        self.assertTrue(world.linked)
        for name, body in (("dpkg-query", DPKG), ("apt-cache", APT)):
            world.write(world.top / "path" / name, ("#!/bin/sh\n" + body).encode())
        (world.top / "pins.txt").write_text(world.pins(), encoding="utf-8")
        system = world.top / "system"  # the fakes' own tools alone, so that no installed gdb is found
        system.mkdir()
        for name in ("cat", "grep"):
            os.symlink(shutil.which(name), system / name)
        env = {"PATH": f"{world.top / 'path'}:{system}", "FAKE": str(world.top)}
        argv = [sys.executable, "-I", str(ROOT / "tools" / "linux_pins.py"), "--prefix", str(world.prefix),
                "--pins", str(world.top / "pins.txt")]
        done = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=120, check=False)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertEqual(done.stdout.splitlines()[-1], "linux-pins: 8 pins, 2 removed, 0 mismatches")
        self.assertNotIn("named by no pin", done.stdout)
        world.dpkg.update({"gcc-14": "14.2.0-4ubuntu2 gcc-14 ii "})
        world.save()
        done = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=120, check=False)
        self.assertEqual(done.returncode, 2, done.stdout + done.stderr)
        self.assertIn("linux-pins: mismatch: removed gcc-14: installed 14.2.0-4ubuntu2, where the removal took it "
                      "(apt-get remove gcc-14)", done.stdout.splitlines())


if __name__ == "__main__":
    unittest.main()
