"""The dumps' bound: a dumps' directory, the build's dumps/, keeps at most
DUMPS_KEPT dumps, the oldest removed first.

    bound(directory, pid=None) -> lines
    hang_dump(pid, binary, directory) -> the hang dump's path, or None

The watcher (watch.py, beside it) runs it after each case ends, whether the
case wrote a dump or none, so that every writer into the directory is
bounded: the diagnostic entry's minidump of a crash, the watcher's hang dump,
and those of the cases a test runs under the debugger.
- A dump is a file of the directory named *.dmp (a minidump); nothing else
  there is read or removed.
- The dumps are ordered by their last write, then by name. The newest
  DUMPS_KEPT stay, and so does every dump the case of process pid wrote,
  by its name (<binary>-<pid>.dmp, <binary>-<pid>-hang.dmp): the
  dump the case's report or the hang's lines name is never removed. The
  hang dump's name is formed here, by hang_dump, which the watcher passes
  to the debugger, so that it and this reading share the one form.
- Each dump removed is named in one line, "dumps: removed <name>, past the
  newest <DUMPS_KEPT>"; one that cannot be removed, "dumps: cannot remove
  <name>: <reason>". One another process removed first is its line, not
  this one's. No directory, no line.
"""

import re
from pathlib import Path

DUMPS_KEPT = 8  # the dumps a dumps' directory keeps at most, the newest
SUFFIX = ".dmp"
HANG = "-hang"  # a hang dump's mark, before SUFFIX


def hang_dump(pid, binary, directory):
    """The watcher's hang dump of process pid, <binary>-<pid>-hang.dmp in
    directory, a Path; None when directory is not given."""
    return Path(directory) / f"{Path(binary).stem}-{pid}{HANG}{SUFFIX}" if directory else None


def own(name, pid):
    """Whether the dump's name is that of a dump process pid wrote."""
    return pid is not None and re.search(rf"-{pid}(?:{HANG})?{re.escape(SUFFIX)}$", name) is not None


def dumps(directory):
    """The directory's dumps, oldest first: [(mtime_ns, name)]."""
    found = []
    try:
        entries = list(Path(directory).iterdir())
    except OSError:
        return found
    for path in entries:
        if path.suffix != SUFFIX:
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
