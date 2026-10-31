"""Check the Linux toolchain against the pin file, tools/linux-pins.txt.

Run inside the Linux system the loop builds on, by the pinned interpreter:

    python3.15 -I tools/linux_pins.py --prefix DIR [--pins FILE]

DIR is the prefix the release archives and the builds were installed under;
no path of a machine is in the pin file. Each pin is one line of five
fields, separated by white space, a '#' starting a comment:

    KIND NAME VERSION SHA256 SOURCE

- deb: an Ubuntu package. dpkg's installed version must be VERSION, and
  the archive's index (apt-cache show NAME=VERSION) must still give SHA256
  for its .deb; an index that no longer lists that version is noted, its
  digest not re-read. SOURCE is ubuntu:SUITE/COMPONENT.
- The compilers' hold: each installed package built from the source
  packages HELD (gcc-13 and llvm-toolchain-18) must be held (apt-mark
  hold), so that it moves by a deliberate re-pin alone, and must be pinned
  as a deb; no other package may be held, so that the system's updates
  stay on.
- download: a release file, kept as DIR/downloads/ and the URL's last
  part, its SHA-256 re-read. SOURCE is the URL.
- command: a tool. SOURCE is prefix/PATH, under DIR, or path/NAME, found
  on PATH. The resolved file's SHA-256 must be SHA256, and a line of
  `TOOL --version` must hold VERSION as a whole token.
- debugger: a development aid, its debugger cases skipping by name where
  it is absent. SOURCE is path/NAME. Absent from PATH, it is noted; present,
  it is checked as a command is.
- debugger-deb: a package installed with a debugger, checked as a deb is;
  not installed, it is noted.
- libcxx: libc++ with libc++abi within it, built under MemorySanitizer.
  SOURCE is prefix/PATH, its install's prefix under DIR: lib/libc++.a's
  SHA-256 must be SHA256, and include/c++/v1/__config's _LIBCPP_VERSION
  must be VERSION's major, then its minor in two digits and 00 (18.1.3 is
  180100: libc++ 18 leaves the patch level out, which the LLVM source's
  download pin holds).
- removed: a package an earlier toolchain's removal took, a line of two
  fields, KIND NAME: dpkg must not list it installed. A name both removed
  and pinned as a deb is malformed.
- The prefix: each entry of DIR's top level, and of DIR/downloads, that no
  pin names, by its path or by where a pinned path resolves, is noted, an
  earlier toolchain's leftover until it is removed.

Exits 0 when every pin holds; otherwise names each mismatch, one a line,
and exits 2, as it does when it cannot run (no pin file, a malformed line).
"""

import argparse
import contextlib
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

PINS = Path(__file__).resolve().parent / "linux-pins.txt"
KINDS = ("deb", "download", "command", "debugger", "debugger-deb", "libcxx", "removed")
FIELDS = {"removed": 2}  # a kind's fields where they are not 5
HELD = ("gcc-13", "llvm-toolchain-18")  # the source packages of the pinned compilers, held
HEX = re.compile(r"[0-9a-f]{64}")
SHOWN = 40  # mismatches printed at most, then a count of the rest
TIMEOUT = 60


def parse(text):
    """The pins, as (line number, kind, name, version, sha256, source), a
    removed line's last three empty, and each malformed line's finding."""
    pins, findings, seen = [], [], set()
    for number, line in enumerate(text.splitlines(), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        fields = line.split()
        want = FIELDS.get(fields[0], 5)
        if len(fields) != want:
            findings.append(f"line {number}: {len(fields)} fields, not {want}")
            continue
        kind, name, version, digest, source = fields if want == 5 else (*fields, "", "", "")
        if kind not in KINDS:
            findings.append(f"line {number}: kind {kind!r} is none of {', '.join(KINDS)}")
        elif want == 5 and not HEX.fullmatch(digest):
            findings.append(f"line {number}: {name}'s SHA-256 is not 64 lower-case hex digits")
        elif (kind, name) in seen:
            findings.append(f"line {number}: {kind} {name} pinned twice")
        else:
            seen.add((kind, name))
            pins.append((number, kind, name, version, digest, source))
    debs = {name for _, kind, name, *_ in pins if kind in ("deb", "debugger-deb")}
    findings += [f"line {number}: {name} both removed and pinned as a deb"
                 for number, kind, name, *_ in pins if kind == "removed" and name in debs]
    return pins, findings


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def run(argv):
    """The command's status and its output, standard error included."""
    try:
        done = subprocess.run(argv, capture_output=True, text=True, timeout=TIMEOUT, errors="replace")
    except (OSError, subprocess.TimeoutExpired) as e:
        return None, str(e)
    return done.returncode, done.stdout + done.stderr


def under(prefix, source):
    """The file a prefix/ source names, or None when it is not one."""
    if not source.startswith("prefix/") or ".." in source.split("/"):
        return None
    return prefix / source.removeprefix("prefix/")


def installed_debs(names=()):
    """(version, source package, held) of each name that is installed; of
    every installed package when no name is given. dpkg's status is
    "ii", or "hi" when held."""
    status, out = run(["dpkg-query", "-W", "-f", "${Package} ${Version} ${source:Package} ${db:Status-Abbrev}\n",
                       *names])
    found = {}
    for line in (out if status is not None else "").splitlines():
        parts = line.split()
        if len(parts) == 4 and len(parts[3]) >= 2 and parts[3][1] == "i":
            found[parts[0]] = (parts[1], parts[2], parts[3][0] == "h")
    return found


def index_digest(name, version):
    """The archive index's SHA-256 for NAME=VERSION's .deb, or None."""
    status, out = run(["apt-cache", "show", f"{name}={version}"])
    if status != 0:
        return None
    match = re.search(r"^SHA256: ([0-9a-f]{64})$", out, re.MULTILINE)
    return match.group(1) if match else None


def check_debs(pins, out, notes):
    debs = [p for p in pins if p[1] in ("deb", "debugger-deb")]
    removed = [p[2] for p in pins if p[1] == "removed"]
    if not debs and not removed:
        return
    if shutil.which("dpkg-query") is None or shutil.which("apt-cache") is None:
        out.append("deb: dpkg-query or apt-cache is not on PATH")
        return
    every = installed_debs()
    for name in removed:
        if name in every:
            out.append(f"removed {name}: installed {every[name][0]}, where the removal took it "
                       f"(apt-get remove {name})")
    have = installed_debs([p[2] for p in debs]) if debs else {}
    for _, kind, name, version, digest, source in debs:
        if not source.startswith("ubuntu:"):
            out.append(f"{kind} {name}: source {source!r} is not ubuntu:SUITE/COMPONENT")
        if name not in have:
            (notes if kind == "debugger-deb" else out).append(f"{kind} {name}: not installed; the pin is {version}")
            continue
        if have[name][0] != version:
            out.append(f"{kind} {name}: installed {have[name][0]}; the pin is {version}")
            continue
        listed = index_digest(name, version)
        if listed is None:
            notes.append(f"{kind} {name} {version}: the archive's index no longer lists it; digest not re-read")
        elif listed != digest:
            out.append(f"{kind} {name} {version}: the index's SHA-256 is {listed}; the pin {digest}")
    check_hold(debs, every, out)


def check_hold(debs, every, out):
    """Each installed package of a HELD source held and pinned as a deb; no
    other package held."""
    pinned = {name for _, kind, name, *_ in debs if kind == "deb"}
    compilers = {name for name, (_, source, _) in every.items() if source in HELD}
    for name in sorted(compilers):
        if not every[name][2]:
            out.append(f"deb {name}: a package of {every[name][1]}, not held (apt-mark hold {name})")
        if name not in pinned:
            out.append(f"deb {name}: a package of {every[name][1]}, installed {every[name][0]}, pinned as no deb")
    for name in sorted(n for n, (_, _, held) in every.items() if held and n not in compilers):
        out.append(f"deb {name}: held, but no package of {' or '.join(HELD)}: only the compilers are held, "
                   f"the system's updates stay on (apt-mark unhold {name})")


def check_download(prefix, name, digest, source, out):
    if not source.startswith("https://"):
        out.append(f"download {name}: source {source!r} is not an https URL")
        return
    path = prefix / "downloads" / source.rsplit("/", 1)[-1]
    if not path.is_file():
        out.append(f"download {name}: {path.name} is missing from the prefix's downloads")
        return
    got = sha256(path)
    if got != digest:
        out.append(f"download {name}: {path.name}'s SHA-256 is {got}; the pin {digest}")


def check_command(prefix, name, version, digest, source, out, kind="command"):
    if source.startswith("path/"):
        found = shutil.which(source.removeprefix("path/"))
        path = Path(found) if found else None
    else:
        path = under(prefix, source)
        if path is None:
            out.append(f"{kind} {name}: source {source!r} is neither prefix/PATH nor path/NAME")
            return
    if path is None or not path.is_file():
        out.append(f"{kind} {name}: missing ({source})")
        return
    real = path.resolve()
    got = sha256(real)
    if got != digest:
        out.append(f"{kind} {name}: {real.name}'s SHA-256 is {got}; the pin {digest}")
    status, text = run([str(path), "--version"])
    token = re.compile(r"(?<![\w.])" + re.escape(version) + r"(?![\w.])")
    if status != 0:
        out.append(f"{kind} {name}: --version failed ({status}): {text.strip()[:120]}")
    elif not any(token.search(line) for line in text.splitlines()):
        first = text.strip().splitlines()[0] if text.strip() else "no output"
        out.append(f"{kind} {name}: --version gives {first[:120]!r}; the pin is {version}")


def check_debugger(prefix, name, version, digest, source, out, notes):
    if not source.startswith("path/"):
        out.append(f"debugger {name}: source {source!r} is not path/NAME")
    elif shutil.which(source.removeprefix("path/")) is None:
        notes.append(f"debugger {name}: absent from PATH; its debugger cases skip by name")
    else:
        check_command(prefix, name, version, digest, source, out, "debugger")


def check_libcxx(prefix, name, version, digest, source, out):
    root = under(prefix, source)
    if root is None:
        out.append(f"libcxx {name}: source {source!r} is not prefix/PATH")
        return
    archive, config = root / "lib" / "libc++.a", root / "include" / "c++" / "v1" / "__config"
    if not archive.is_file() or not config.is_file():
        out.append(f"libcxx {name}: missing (no lib/libc++.a or include/c++/v1/__config under {source})")
        return
    got = sha256(archive)
    if got != digest:
        out.append(f"libcxx {name}: libc++.a's SHA-256 is {got}; the pin {digest}")
    parts = version.split(".")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        out.append(f"libcxx {name}: version {version!r} is not MAJOR.MINOR.PATCH")
        return
    want = f"{int(parts[0])}{int(parts[1]):02d}00"
    match = re.search(r"^#\s*define _LIBCPP_VERSION (\d+)$", config.read_text(errors="replace"), re.MULTILINE)
    if match is None or match.group(1) != want:
        out.append(f"libcxx {name}: _LIBCPP_VERSION is {match.group(1) if match else 'absent'}; the pin {want}")


def check_prefix(prefix, pins, notes):
    """A note of each entry of the prefix's top level, and of its downloads,
    that no pin names, by its path or where a pinned path resolves."""
    top = prefix.resolve()
    named = {"downloads"} if any(p[1] == "download" for p in pins) else set()
    for *_, source in pins:
        path = under(prefix, source)
        if path is None:
            continue
        named.add(source.removeprefix("prefix/").split("/", 1)[0])
        with contextlib.suppress(OSError, ValueError):
            named.add(path.resolve().relative_to(top).parts[0])
    fetched = {p[5].rsplit("/", 1)[-1] for p in pins if p[1] == "download"}
    for folder, kept, where in ((prefix, named, "the prefix"), (prefix / "downloads", fetched, "the downloads")):
        if folder.is_dir() and (folder != prefix / "downloads" or fetched):
            notes += [f"{where}: {entry.name} is named by no pin" for entry in sorted(folder.iterdir())
                      if entry.name not in kept]


def check(prefix, pins):
    """The mismatches of every pin, and the notes."""
    out, notes = [], []
    check_debs(pins, out, notes)
    check_prefix(prefix, pins, notes)
    for _, kind, name, version, digest, source in pins:
        if kind == "download":
            check_download(prefix, name, digest, source, out)
        elif kind == "command":
            check_command(prefix, name, version, digest, source, out)
        elif kind == "debugger":
            check_debugger(prefix, name, version, digest, source, out, notes)
        elif kind == "libcxx":
            check_libcxx(prefix, name, version, digest, source, out)
    return out, notes


def main(argv=None):
    parser = argparse.ArgumentParser(description="Check the Linux toolchain against its pin file.")
    parser.add_argument("--prefix", required=True, help="the tools' install prefix")
    parser.add_argument("--pins", default=str(PINS), help="the pin file (default: tools/linux-pins.txt)")
    args = parser.parse_args(argv)
    prefix = Path(args.prefix)
    try:
        text = Path(args.pins).read_text(encoding="utf-8")
    except OSError as e:
        print(f"linux-pins: cannot read the pin file: {e}")
        return 2
    if not prefix.is_dir():
        print(f"linux-pins: the prefix {prefix} is not a directory")
        return 2
    pins, malformed = parse(text)
    if malformed:
        for line in malformed[:SHOWN]:
            print(f"linux-pins: malformed: {line}")
        return 2
    mismatches, notes = check(prefix, pins)
    for line in notes[:SHOWN]:
        print(f"linux-pins: note: {line}")
    for line in mismatches[:SHOWN]:
        print(f"linux-pins: mismatch: {line}")
    if len(mismatches) > SHOWN:
        print(f"linux-pins: and {len(mismatches) - SHOWN} more mismatches")
    removed = sum(1 for p in pins if p[1] == "removed")
    print(f"linux-pins: {len(pins) - removed} pins, {removed} removed, "
          f"{len(mismatches)} mismatch{'' if len(mismatches) == 1 else 'es'}")
    return 2 if mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
