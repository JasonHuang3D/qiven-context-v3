"""Install a Python package from its source tree into an interpreter's environment.

    <python> -I -m qiven_devkit.install <source> <target-python>

<source> holds pyproject.toml, whose [project] table gives the distribution's
name and version, and src/, whose every file (outside __pycache__) is
installed under the target's purelib, the directory the target interpreter
itself names. Beside them goes an installed distribution's .dist-info:
METADATA, INSTALLER and RECORD, as the PyPA's "Recording installed projects"
specification says. Nothing is built, fetched or launched: no build backend,
no network, no console script.

A file is written only where its content differs. A file the previous
install's RECORD lists and this install no longer holds is removed, with the
bytecode Python cached for it; a file RECORD does not list is never touched,
nor is any path outside purelib.

The module uses the standard library only, so that the very first install,
when no devkit is installed anywhere, can run this file by its path under the
target interpreter with -I.

Exit 0 installed, 1 refused or failed (the reason on stderr), 2 usage.
"""

import base64
import csv
import hashlib
import io
import os
import re
import subprocess
import sys
import tomllib
from pathlib import Path

INSTALLER = "qiven-devkit"
HANDLED = {"name", "version", "description", "requires-python"}
QUERY = "import sysconfig; print(sysconfig.get_path('purelib'))"


class Refused(Exception):
    """An install refused, with its reason."""


def purelib(python):
    """The purelib directory the interpreter `python` names, asked of it."""
    try:
        done = subprocess.run([str(python), "-I", "-X", "utf8", "-c", QUERY], capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise Refused(f"cannot ask {python} for its purelib: {error}") from error
    text = done.stdout.decode("utf-8", "replace").strip()
    if done.returncode != 0 or not text:
        raise Refused(f"{python} named no purelib (exit {done.returncode}): "
                      f"{done.stderr.decode('utf-8', 'replace').strip()}")
    where = Path(text)
    if not where.is_absolute() or not where.is_dir():
        raise Refused(f"{python} named purelib {text}, no directory")
    return where


def normalized(name):
    """A distribution name as a .dist-info directory spells it."""
    return re.sub(r"[-_.]+", "_", name).lower()


def digest(data):
    """RECORD's hash of data."""
    return "sha256=" + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b"=").decode("ascii")


def project(source):
    """The [project] table of source's pyproject.toml, refused where it asks
    what this installer does not do (a dependency, a launcher, a field
    computed by a backend)."""
    try:
        with open(Path(source, "pyproject.toml"), "rb") as f:
            table = tomllib.load(f).get("project")
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise Refused(f"cannot read {source}/pyproject.toml: {error}") from error
    if not isinstance(table, dict) or not isinstance(table.get("name"), str) \
            or not isinstance(table.get("version"), str):
        raise Refused(f"{source}/pyproject.toml: [project] needs a name and a version")
    unhandled = sorted(set(table) - HANDLED)
    if unhandled:
        raise Refused(f"{source}/pyproject.toml: [project] {', '.join(unhandled)} not handled by this installer")
    return table


def entries(source, table):
    """{path under purelib, posix: bytes} of the install, RECORD apart."""
    src = Path(source, "src")
    if not src.is_dir():
        raise Refused(f"{source} holds no src/")
    held = {p.relative_to(src).as_posix(): p.read_bytes() for p in sorted(src.rglob("*"))
            if p.is_file() and "__pycache__" not in p.relative_to(src).parts}
    if not held:
        raise Refused(f"{src} holds no file")
    info = f"{normalized(table['name'])}-{table['version']}.dist-info"
    meta = ["Metadata-Version: 2.4", f"Name: {table['name']}", f"Version: {table['version']}"]
    if "description" in table:
        meta.append(f"Summary: {table['description']}")
    if "requires-python" in table:
        meta.append(f"Requires-Python: {table['requires-python']}")
    held[f"{info}/METADATA"] = ("\n".join(meta) + "\n").encode("utf-8")
    held[f"{info}/INSTALLER"] = (INSTALLER + "\n").encode("ascii")
    return info, held


def record_text(info, held):
    """RECORD's text for the install's files, RECORD itself last, unhashed."""
    out = io.StringIO()
    writer = csv.writer(out, lineterminator="\n")
    for path in sorted(held):
        writer.writerow([path, digest(held[path]), len(held[path])])
    writer.writerow([f"{info}/RECORD", "", ""])
    return out.getvalue()


def listed(lib, name):
    """The paths the RECORDs of name's installs under lib list."""
    paths = set()
    for record in lib.glob(f"{normalized(name)}-*.dist-info/RECORD"):
        with open(record, encoding="utf-8", newline="") as f:
            paths.update(row[0] for row in csv.reader(f) if row)
    return paths


def inside(lib, path):
    """The file under lib that a RECORD path names, None where it lies outside."""
    target = (lib / path).resolve()
    return target if target.is_relative_to(lib.resolve()) and target != lib.resolve() else None


def write(target, data):
    """target's content made data, through a file beside it; whether it was written."""
    if target.is_file() and target.read_bytes() == data:
        return False
    target.parent.mkdir(parents=True, exist_ok=True)
    scratch = target.with_name(target.name + ".qiven-new")
    scratch.write_bytes(data)
    os.replace(scratch, target)
    return True


def remove(lib, path):
    """Remove the file a RECORD path names under lib, and the bytecode Python
    cached for it; empty directories it leaves, up to lib, go too. The count
    of files removed."""
    target = inside(lib, path)
    if target is None or not target.is_file():
        return 0
    target.unlink()
    count = 1
    if target.suffix == ".py":
        for cached in (target.parent / "__pycache__").glob(f"{target.stem}.*.pyc"):
            cached.unlink()
            count += 1
    top = lib.resolve()
    for folder in (target.parent / "__pycache__", *target.parents):
        if folder == top or not folder.is_relative_to(top):
            break
        if folder.is_dir() and not any(folder.iterdir()):
            folder.rmdir()
    return count


def install(source, lib):
    """Install source's package under the purelib directory lib; (files
    held, written, removed)."""
    lib = Path(lib)
    table = project(source)
    info, held = entries(source, table)
    before = listed(lib, table["name"])
    written = sum(write(lib / path, data) for path, data in held.items())
    written += write(lib / info / "RECORD", record_text(info, held).encode("utf-8"))
    gone = sorted(before - set(held) - {f"{info}/RECORD"})
    removed = sum(remove(lib, path) for path in gone)
    return len(held) + 1, written, removed


def main(argv):
    if len(argv) != 3:
        print("usage: python -I -m qiven_devkit.install <source> <target-python>", file=sys.stderr)
        return 2
    source, target = Path(argv[1]), argv[2]
    try:
        lib = purelib(target)
        held, written, removed = install(source, lib)
    except (Refused, OSError) as error:
        print(f"install: refused: {error}", file=sys.stderr)
        return 1
    print(f"install: {source} into {lib}: {held} files, {written} written, {removed} removed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
