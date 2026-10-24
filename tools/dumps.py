"""The dumps' bound: a dumps' directory, the build's dumps/, keeps at most
DUMPS_KEPT dumps, the oldest removed first.

    bound(directory, pid=None) -> lines

The watcher (tools/watch.py) runs it after each case ends, whether the
case wrote a dump or none, so that every writer into the directory is
bounded: the diagnostic entry's minidump of a crash, the watcher's hang dump,
and those of the cases a test runs under the debugger.
- A dump is a file of the directory named *.dmp (a minidump) or *.core (a
  core file); nothing else there is read or removed.
- The dumps are ordered by their last write, then by name. The newest
  DUMPS_KEPT stay, and so does every dump the case of process pid wrote,
  by its name (<binary>-<pid>.dmp, <binary>-<pid>-hang.dmp or .core): the
  dump the case's report or the hang's lines name is never removed.
- Each dump removed is named in one line, "dumps: removed <name>, past the
  newest <DUMPS_KEPT>"; one that cannot be removed, "dumps: cannot remove
  <name>: <reason>". One another process removed first is its line, not
  this one's. No directory, no line.

Run as a script, it bounds the directory it is given and prints the lines:

    python -I tools/dumps.py <directory>
"""

import re
import sys
from pathlib import Path

DUMPS_KEPT = 8  # the dumps a dumps' directory keeps at most, the newest
SUFFIXES = (".dmp", ".core")


def own(name, pid):
    """Whether the dump's name is that of a dump process pid wrote."""
    return pid is not None and re.search(rf"-{pid}(?:-hang)?\.(?:dmp|core)$", name) is not None


def dumps(directory):
    """The directory's dumps, oldest first: [(mtime_ns, name)]."""
    found = []
    try:
        entries = list(Path(directory).iterdir())
    except OSError:
        return found
    for path in entries:
        if path.suffix not in SUFFIXES:
            continue
        try:
            if path.is_file():
                found.append((path.stat().st_mtime_ns, path.name))
        except OSError:
            continue  # removed meanwhile
    return sorted(found)


def bound(directory, pid=None, kept=DUMPS_KEPT):
    """Removes the dumps past the newest `kept`, oldest first, but none of
    process pid's: the lines naming each."""
    found = dumps(directory)
    lines = []
    for _, name in found[:max(len(found) - kept, 0)]:
        if own(name, pid):
            continue
        try:
            (Path(directory) / name).unlink()
        except FileNotFoundError:
            continue
        except OSError as e:
            lines.append(f"dumps: cannot remove {name}: {e.strerror or e}")
            continue
        lines.append(f"dumps: removed {name}, past the newest {kept}")
    return lines


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python -I tools/dumps.py <directory>")
    print("\n".join(bound(sys.argv[1])) or "dumps: none removed")
