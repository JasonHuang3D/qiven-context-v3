"""The navigation aid: C++ symbols answered by clangd, on an agent's call.

    py -3.15 -I tools/nav.py COMMAND ... [--file FILE] [--limit N] [--no-index] [--root DIR]

It runs only when called: nothing puts its output into a session. Each call
starts clangd (QIVEN3_CLANGD, else LLVM's under ProgramFiles on Windows,
else PATH's) over the analysis preset's stripped database,
build/windows-analysis/tidy/compile_commands.json, the one without
/pathmap, and stops it before it answers.

Commands:
- index: builds clangd's background index under build/nav/, from a copy of
  that database, and records the commit and each C and C++ file's blob it
  was built at, and the version of the clangd that built it
  (build/nav/index.json). A heavy job, run only on request: every
  translation unit is parsed.
- def NAME, refs NAME, callers NAME: a plain or qualified name (publish,
  Staged::publish). With --file, the symbols named so that FILE uses or
  declares; without it, every symbol of the index named so. A name that
  matches several symbols lists them all. def gives each symbol's
  declaration and definition; refs its references, declarations left out;
  callers its references grouped by the function that holds them.
- type FILE:LINE NAME: the type of NAME on that line, from clangd's hover;
  the tool finds the column, at NAME's first use on the line.
- api FILE: a header's declarations, each without its body.

Where the index is built, def, refs and callers answer from it, the files
they open added; clangd may start re-indexing the files changed since,
which ends with the call. Without it, or with --no-index, they answer from
the translation unit of --file alone, which they then need.

The answer: one envelope, one line of JSON, at most 8,192 bytes, its fields:
- status: ok, missing (no symbol of that name), not_ready (clangd, the
  database or the index absent) or failed;
- code: OK, or the refusal's: NO_SYMBOL, CLANGD_ABSENT, DATABASE_ABSENT,
  NO_INDEX, NO_FILE, NOT_ON_LINE, CLANGD_FAILED, CLANGD_TIMEOUT,
  INDEX_TIMEOUT, INTERNAL;
- next_action: what to do next, a remedy for a refusal;
- operation_id: the call's id, op: and 8 hexadecimal digits;
- detail_ref: the whole answer, unbounded, under build/nav/answers/ (the
  newest 20 kept), or null;
- data: clangd's answers, and with them:
  - scope: what was asked, the index or the translation unit of --file;
  - unresolved: the lines where the name is called or used as a member
    and clangd returned nothing there (no symbol, or a reference none of
    the symbols holds), such as a call through a template parameter, each
    with its use and what clangd gave. So no answer reads "no callers" in
    silence; read them by hand. Comments and literals are not uses;
  - index: present or not; built, its commit, the time, the clangd that
    built it, the C and C++ files changed since (changed), and whether
    clangd had loaded it. An index whose record names another clangd than
    the one the call runs, or none, is stale: answered as absent, the two
    versions named under build;
  - every list as {count, shown, items}: at most --limit items (default
    20, at most 200), fewer where the envelope would pass its bound, the
    full count kept.
Lines and columns are 1-based, paths relative to the checkout.

Exit 0 for status ok, 1 for any other envelope, 2 for a bad argument (its
usage on standard error, no envelope).

The modules: tools/nav_lsp.py (clangd and the protocol) and
tools/nav_query.py (the commands over it), loaded by runpy.
"""

import argparse
import datetime
import json
import runpy
import secrets
import shutil
import subprocess
import sys
import time
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
LSP = runpy.run_path(str(TOOLS / "nav_lsp.py"))  # under -I this directory is off sys.path
QUERY = runpy.run_path(str(TOOLS / "nav_query.py"), init_globals={"LSP": LSP})
Refusal, Clangd = LSP["Refusal"], LSP["Clangd"]
DATABASE = "build/windows-analysis/tidy/compile_commands.json"
NAV = "build/nav"
RECORD = "index.json"
ANSWERS = "answers"
KEEP = 20  # the answers kept
BOUND = 8192  # an envelope's bytes at most
LIMIT, LIMIT_MAX = 20, 200
INDEX_WAIT = 3600  # seconds index waits for the background index at most
LOAD_WAIT = 120  # seconds a query waits for the index's loading at most
CODE = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".ipp")
NEXT = {"OK": "", "NO_INDEX": "give --file, or build the index: nav.py index (heavy)"}


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def git(root, *argv, data=None):
    done = subprocess.run(["git", *argv], cwd=str(root), input=data, capture_output=True, timeout=300)
    if done.returncode != 0:
        raise OSError(done.stderr.decode("utf-8", "replace").strip()[-200:])
    return done.stdout.decode("utf-8", "replace")


def snapshot(root):
    """(HEAD's commit, {path: blob} of each C and C++ file of the checkout,
    tracked or not ignored), or (None, None) where git cannot tell."""
    try:
        commit = git(root, "rev-parse", "HEAD").strip()
        listed = [p for p in git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard").split("\0")
                  if p.endswith(CODE) and (Path(root) / p).is_file()]
        listed = sorted(set(listed))
        blobs = git(root, "hash-object", "--stdin-paths", data="\n".join(listed).encode("utf-8")).split()
    except (OSError, subprocess.SubprocessError):
        return None, None
    return commit, dict(zip(listed, blobs))


def index_state(root, files, clangd):
    """The index's age: absent, or its commit, time, clangd and the files
    changed since by blob; absent where the record names another clangd
    than the call's version, clangd, or none."""
    nav = Path(root) / NAV
    try:
        record = json.loads((nav / RECORD).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"present": False, "build": "nav.py index (heavy)"}
    if not (nav / ".cache").is_dir():
        return {"present": False, "build": "the record is there but not the index: nav.py index"}
    built_by = record.get("clangd")
    if not built_by or built_by != clangd:
        return {"present": False, "build": f"stale: built by clangd {built_by or '(none named)'}, the call runs "
                                           f"clangd {clangd or '(no version)'}: nav.py index (heavy)"}
    old = record.get("files", {})
    changed = None if files is None else sorted(p for p in set(old) | set(files) if old.get(p) != files.get(p))
    return {"present": True, "commit": record.get("commit"), "built": record.get("built"), "clangd": built_by,
            "changed": changed if changed is not None else "unknown: git gave no listing"}


def listed(items):
    return {"count": len(items), "shown": len(items), "items": list(items)}


def lists_in(value):
    """Every {count, shown, items} of value, depth first."""
    if isinstance(value, dict):
        if set(value) == {"count", "shown", "items"}:
            yield value
        for v in value.values():
            yield from lists_in(v)
    elif isinstance(value, list):
        for v in value:
            yield from lists_in(v)


def text_of(envelope):
    return json.dumps(envelope, separators=(",", ":"), ensure_ascii=True)


def bounded(envelope, limit):
    """The envelope cut to its bound: each list to limit items, then the
    longest list halved until it fits; its counts kept."""
    for found in lists_in(envelope["data"]):
        del found["items"][limit:]
        found["shown"] = len(found["items"])
    text = text_of(envelope)
    while len(text) > BOUND:
        candidates = [f for f in lists_in(envelope["data"]) if f["items"]]
        if not candidates:
            envelope["data"] = {"cut": f"the answer passed {BOUND} bytes", "detail_ref": envelope["detail_ref"]}
            return text_of(envelope)
        longest = max(candidates, key=lambda f: len(text_of(f["items"])))
        del longest["items"][len(longest["items"]) // 2:]
        longest["shown"] = len(longest["items"])
        text = text_of(envelope)
    return text


def keep_answer(root, operation_id, envelope):
    """The whole answer under build/nav/answers/, the newest KEEP kept; its
    path, or None."""
    folder = Path(root) / NAV / ANSWERS
    try:
        folder.mkdir(parents=True, exist_ok=True)
        name = operation_id.replace(":", "-") + ".json"
        (folder / name).write_text(text_of(envelope) + "\n", encoding="utf-8")
        kept = sorted(folder.glob("op-*.json"), key=lambda p: p.stat().st_mtime, reverse=True)
        for old in kept[KEEP:]:
            old.unlink(missing_ok=True)
    except OSError:
        return None
    return f"{NAV}/{ANSWERS}/{name}"


def database_dir(root):
    path = Path(root) / DATABASE
    if not path.is_file():
        raise Refusal("not_ready", "DATABASE_ABSENT", f"no stripped database: {DATABASE}",
                      "build the analysis preset: py -3.15 -I tools/loop.py build windows windows-analysis")
    return path.parent


def smallest(entries):
    """The database's smallest file, the cheapest to open."""
    files = [Path(e["directory"]) / e["file"] for e in entries]
    return min((f for f in files if f.is_file()), key=lambda f: f.stat().st_size)


def build_index(args):
    """index: the background index over a copy of the database, waited for,
    and its record."""
    source = database_dir(args.root)
    nav = args.root / NAV
    nav.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source / "compile_commands.json", nav / "compile_commands.json")
    entries = json.loads((nav / "compile_commands.json").read_text(encoding="utf-8"))
    commit, files = snapshot(args.root)
    version = LSP["clangd_version"](args.clangd)
    start = time.monotonic()
    with Clangd(args.clangd, nav, args.root, background=True) as clangd:
        clangd.open(smallest(entries))  # a file opened loads the database
        if not clangd.wait_index(INDEX_WAIT, loaded_only=False):
            raise Refusal("failed", "INDEX_TIMEOUT", f"the background index did not end in {INDEX_WAIT} s",
                          "run nav.py index again; clangd resumes from the shards written")
    seconds = round(time.monotonic() - start, 1)
    record = {"commit": commit, "built": utc_now(), "clangd": version, "database": DATABASE, "seconds": seconds,
              "files": files or {}}
    (nav / RECORD).write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8")
    shards = sum(1 for _ in (nav / ".cache").rglob("*.idx")) if (nav / ".cache").is_dir() else 0
    return {"index": {"present": True, "commit": commit, "built": record["built"], "clangd": version,
                      "changed": listed([])},
            "translation_units": len(entries), "shards": shards, "seconds": seconds}


def run(args):
    """The command's data, or a Refusal; (status, code, next_action, data)."""
    args.clangd = LSP["find_clangd"]()
    if args.command == "index":
        return "ok", "OK", "query it: nav.py def|refs|callers NAME", build_index(args)
    if args.command in ("type", "api"):
        return QUERY["single"](args, Clangd, database_dir(args.root))
    commit, files = snapshot(args.root)
    clangd = None if args.no_index else LSP["clangd_version"](args.clangd)
    state = {"present": False} if args.no_index else index_state(args.root, files, clangd)
    if not state["present"] and args.file is None:
        stale = f"; the index is {state['build']}" if state.get("build", "").startswith("stale") else ""
        raise Refusal("not_ready", "NO_INDEX", f"no index, and no --file to answer from{stale}", NEXT["NO_INDEX"])
    background = state["present"]
    folder = args.root / NAV if background else database_dir(args.root)
    with Clangd(args.clangd, folder, args.root, background=background) as clangd:
        if background:
            state["loaded"] = None
            args.opener = smallest(json.loads((folder / "compile_commands.json").read_text(encoding="utf-8")))
        data = QUERY["named"](args, clangd, files, state, commit)
    if isinstance(state.get("changed"), list):
        state["changed"] = listed(state["changed"])
    data["index"] = state
    if not data["symbols"]["count"]:
        return "missing", "NO_SYMBOL", "check the name, give --file where it is used, or read unresolved", data
    return "ok", "OK", "", data


class Parser(argparse.ArgumentParser):
    def error(self, message):
        self.print_usage(sys.stderr)
        print(f"nav: {message} (see --help)", file=sys.stderr)
        sys.exit(2)


def parse(argv):
    parser = Parser(prog="nav.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--root", type=Path, default=ROOT, help="the checkout (default: this tool's)")
    common.add_argument("--limit", type=int, default=LIMIT, help=f"a list's items at most (1 to {LIMIT_MAX})")
    commands = parser.add_subparsers(dest="command", required=True, parser_class=Parser)
    commands.add_parser("index", parents=[common], help="build the background index (heavy)")
    for name, what in (("def", "its declarations and definitions"), ("refs", "its references"),
                       ("callers", "its references by the function holding them")):
        one = commands.add_parser(name, parents=[common], help=f"a plain or qualified name: {what}")
        one.add_argument("name", metavar="NAME")
        one.add_argument("--file", help="the symbols this file uses or declares; without the index, its unit")
        one.add_argument("--no-index", action="store_true", help="answer from --file's unit alone")
    one = commands.add_parser("type", parents=[common], help="the type of NAME on FILE's LINE")
    one.add_argument("at", metavar="FILE:LINE")
    one.add_argument("name", metavar="NAME")
    one = commands.add_parser("api", parents=[common], help="a header's declarations, without bodies")
    one.add_argument("file", metavar="FILE")
    args = parser.parse_args(argv)
    if not 1 <= args.limit <= LIMIT_MAX:
        parser.error(f"--limit is 1 to {LIMIT_MAX}")
    if hasattr(args, "name") and not QUERY["NAME"].fullmatch(args.name):
        parser.error(f"not a plain or qualified name: {args.name!r}")
    if args.command == "type":
        file, _, line = args.at.rpartition(":")
        if not file or not line.isdigit() or int(line) < 1:
            parser.error(f"not FILE:LINE: {args.at!r}")
        args.file, args.line = file, int(line)
    args.no_index = getattr(args, "no_index", False)
    args.file = getattr(args, "file", None)
    args.root = args.root.resolve()
    return args


def main(argv=None):
    args = parse(sys.argv[1:] if argv is None else argv)
    operation_id = "op:" + secrets.token_hex(4)
    try:
        status, code, next_action, data = run(args)
    except Refusal as e:
        status, code, next_action, data = e.status, e.code, e.next_action, {"message": str(e)}
    except Exception as e:  # an error of the tool's own is an envelope, never a traceback
        status, code, next_action = "failed", "INTERNAL", "report it with the call"
        data = {"message": f"{type(e).__name__}: {e}"[:400]}
    envelope = {"status": status, "code": code, "next_action": next_action, "operation_id": operation_id,
                "detail_ref": None, "data": data}
    envelope["detail_ref"] = keep_answer(args.root, operation_id, envelope)
    print(bounded(envelope, args.limit))
    return 0 if status == "ok" else 1


if __name__ == "__main__":
    sys.exit(main())
