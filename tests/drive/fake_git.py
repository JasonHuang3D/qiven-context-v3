"""A fake git for the loop's tests, run in git's place through its door
(tools/loop_git.py, QIVEN3_GIT_FAKE; tests/drive/own_git.py's
own_git(fake=True)): it answers the commands the loop's tools and the
cases' fixtures send, on a model of a repository kept in .git/fake.json,
so that a case whose property is the tool's, not git's, starts no git.

The model: the index, {path: blob id}, or the file GIT_INDEX_FILE names
when the environment names one (empty while it holds none); HEAD, a
commit id or none; and each commit's tree. Blob ids are git's (SHA-1 of
"blob <size>\\0" and the bytes, no filter); tree and commit ids are the
fake's own, from their content alone, a commit-tree's too; ignored files
are those the root's .gitignore and .git/info/exclude name, in the subset
of the pattern form the cases use: a name or glob, a leading / anchoring
it, a trailing / for directories. The commands: init [-q] [DIR]; add -A;
commit [-q] [--allow-empty] -m TEXT; write-tree; commit-tree TREE -p
PARENT -m TEXT; rev-parse [--verify] HEAD, HEAD^{tree} and
--absolute-git-dir; ls-files with -s, -z, -m, -o, --cached, --others and
--exclude-standard; hash-object --stdin-paths. Any other exits 129,
naming it, so that a tool's new command is never answered by guess.
A checkout with no model (the project's own) reads as one whose index
holds every file not ignored, at HEAD 0 x 40; a commit-tree there is not
kept."""

import fnmatch
import hashlib
import json
import os
import sys
from pathlib import Path

NO_HEAD = "0" * 40


def blob(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def top_of(start):
    for here in (start, *start.parents):
        if (here / ".git").exists():
            return here
    raise SystemExit(fail("not a git repository"))


def fail(text, status=128):
    sys.stderr.write(f"fatal: {text}\n")
    return status


def patterns(top):
    found = []
    for name in (".gitignore", ".git/info/exclude"):
        path = top / name
        if path.is_file():
            for line in path.read_text(encoding="utf-8").splitlines():
                line = line.strip()
                if line and not line.startswith("#"):
                    if line.startswith("!"):
                        raise SystemExit(fail(f"unsupported pattern {line!r}", 129))
                    found.append(line)
    return found


def ignored(rel, is_dir, rules):
    for rule in rules:
        directory_only, rule = rule.endswith("/"), rule.rstrip("/")
        if directory_only and not is_dir:
            continue
        anchored = rule.startswith("/") or "/" in rule
        if fnmatch.fnmatchcase(rel if anchored else rel.rsplit("/", 1)[-1], rule.lstrip("/")):
            return True
    return False


def worktree(top):
    """{path: bytes} of every file not ignored."""
    rules, out = patterns(top), {}
    for here, dirs, files in os.walk(top):
        prefix = Path(here).relative_to(top).as_posix()
        prefix = "" if prefix == "." else prefix + "/"
        dirs[:] = sorted(d for d in dirs if d != ".git" and not ignored(prefix + d, True, rules))
        for name in files:
            if not ignored(prefix + name, False, rules):
                out[prefix + name] = (Path(here) / name).read_bytes()
    return out


def load(top):
    path = top / ".git" / "fake.json"
    if path.is_file():
        return json.loads(path.read_text(encoding="utf-8"))
    return {"index": {p: blob(d) for p, d in worktree(top).items()}, "head": NO_HEAD, "model": False}


def save(top, model):
    (top / ".git" / "fake.json").write_text(json.dumps(model, sort_keys=True), encoding="utf-8")


def index_of(model):
    """The index the command reads: GIT_INDEX_FILE's, else the model's."""
    path = Path(os.environ["GIT_INDEX_FILE"]) if os.environ.get("GIT_INDEX_FILE") else None
    if path is None:
        return model["index"]
    return json.loads(path.read_text(encoding="utf-8")) if path.is_file() and path.stat().st_size else {}


def tree_of(index):
    return hashlib.sha1(b"tree " + json.dumps(index, sort_keys=True).encode("utf-8")).hexdigest()


def ls_files(top, model, flags):
    end = "\0" if "-z" in flags else "\n"
    index = index_of(model)
    if "-s" in flags:
        return "".join(f"100644 {index[p]} 0\t{p}{end}" for p in sorted(index))
    files = worktree(top) if {"-m", "-o", "--others"} & flags else {}
    listed = set()
    if "--cached" in flags or not {"-m", "-o", "--others"} & flags:
        listed |= set(index)
    if "-m" in flags:
        listed |= {p for p in index if p not in files or blob(files[p]) != index[p]}
    if {"-o", "--others"} & flags:
        if "--exclude-standard" not in flags:
            raise SystemExit(fail("ls-files --others without --exclude-standard", 129))
        listed |= set(files) - set(index)
    return "".join(p + end for p in sorted(listed))


def main(argv):
    here = Path.cwd()
    command, args = (argv[0], argv[1:]) if argv else ("", [])
    flags = set(args)
    if command == "init":
        top = Path(next((a for a in args if not a.startswith("-")), here)).resolve()
        (top / ".git").mkdir(parents=True, exist_ok=True)
        if not (top / ".git" / "fake.json").is_file():
            save(top, {"index": {}, "head": None, "model": True})
        return 0
    top = top_of(here)
    model = load(top)
    if command == "add" and args == ["-A"]:
        index = {p: blob(d) for p, d in worktree(top).items()}
        if os.environ.get("GIT_INDEX_FILE"):
            Path(os.environ["GIT_INDEX_FILE"]).write_text(json.dumps(index, sort_keys=True), encoding="utf-8")
        else:
            model["index"] = index
            save(top, model)
        return 0
    if command == "commit" and "-m" in args and flags <= {"-q", "--allow-empty", "-m", args[args.index("-m") + 1]}:
        made = json.dumps([model["index"], model["head"], args[args.index("-m") + 1], os.urandom(8).hex()])
        model["head"] = hashlib.sha1(made.encode("utf-8")).hexdigest()
        model.setdefault("trees", {})[model["head"]] = tree_of(model["index"])
        save(top, model)
        return 0
    if command == "write-tree" and not args:
        sys.stdout.write(tree_of(index_of(model)) + "\n")
        return 0
    if command == "commit-tree" and len(args) == 5 and args[1] == "-p" and args[3] == "-m":
        made = hashlib.sha1(b"commit " + json.dumps(args).encode("utf-8")).hexdigest()
        if model.get("model", True):
            model.setdefault("trees", {})[made] = args[0]
            save(top, model)
        sys.stdout.write(made + "\n")
        return 0
    if command == "rev-parse" and args == ["--absolute-git-dir"]:
        sys.stdout.write((top / ".git").as_posix() + "\n")
        return 0
    if command == "rev-parse" and args in (["HEAD"], ["--verify", "HEAD"], ["HEAD^{tree}"]):
        if not model["head"]:
            return fail("ambiguous argument 'HEAD': unknown revision")
        tree = model.get("trees", {}).get(model["head"]) or tree_of(model["index"])
        sys.stdout.write((tree if args == ["HEAD^{tree}"] else model["head"]) + "\n")
        return 0
    if command == "ls-files" and flags <= {"-s", "-z", "-m", "-o", "--cached", "--others", "--exclude-standard"}:
        sys.stdout.buffer.write(ls_files(top, model, flags).encode("utf-8"))
        return 0
    if command == "hash-object" and args == ["--stdin-paths"]:
        paths = sys.stdin.buffer.read().decode("utf-8").splitlines()
        sys.stdout.write("".join(blob((here / p).read_bytes()) + "\n" for p in paths if p))
        return 0
    return fail(f"the fake git answers no {' '.join(argv)!r}", 129)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
