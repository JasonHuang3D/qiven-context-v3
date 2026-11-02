"""The old places' measure: each table file's rows' paths in the old
places at the base commit (tools/loop_table.py's old_rows), kept in
tools/loop_base.json beside the commit it names, its one naming, so that
the modules' check reads them with no git, on a clone too shallow to hold
the commit too (CI's).

- rows_at(root, commit): old_rows() of the table's files at commit in
  root's repository, read through tools/loop_git.py's run, two git
  processes; LookupError naming the commit when the repository lacks it.
- full(root, commit): the commit's full hash, whatever form names it;
  LookupError the same.
- shallow(root): whether root's repository is a shallow clone, the one
  where a commit missing does not fail the comparison.
- text(commit, rows): the file's text.

Run as python -I tools/loop_base.py [COMMIT]: the file written again from
the commit it names, or from COMMIT, then named there by its full hash.
"""

import json
import runpy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent  # the checkout this file is in
GIT = runpy.run_path(str(HERE / "loop_git.py"))
TABLE = runpy.run_path(str(HERE / "loop_table.py"))
DATA = ROOT / TABLE["BASE"]


def git_at(root, argv):
    """git, then the argv that make it read root's .git."""
    git = GIT["which"]()
    if git is None:
        raise FileNotFoundError("no git")
    return git, ["--git-dir", str(Path(root) / ".git"), *argv]


def found(root, commit, argv):
    """git's run over root's .git; LookupError naming the commit when it fails."""
    done = GIT["run"](*git_at(root, argv))
    if done.returncode:
        raise LookupError(f"the base {commit} is not in the repository of {root}: "
                          + done.stderr.decode("utf-8", "replace").strip())
    return done.stdout.decode("utf-8")


def full(root, commit):
    """The commit's full hash in root's repository."""
    return found(root, commit, ["rev-parse", "--verify", f"{commit}^{{commit}}"]).strip()


def shallow(root):
    """Whether root's repository is a shallow clone."""
    done = GIT["run"](*git_at(root, ["rev-parse", "--is-shallow-repository"]), check=True)
    return done.stdout.decode("utf-8").strip() == "true"


def rows_at(root, commit):
    """old_rows() of the table's files at commit, read from root's .git."""
    listed = found(root, commit, ["ls-tree", "-r", "-z", "--name-only", f"{commit}^{{commit}}"])
    paths = [p for p in listed.split("\0") if p and TABLE["is_table_file"](p)]
    done = GIT["run"](*git_at(root, ["cat-file", "--batch"]),
                      data="".join(f"{commit}:{p}\n" for p in paths).encode(), check=True)
    texts, rest = {}, done.stdout
    for path in paths:  # each "<id> blob <size>", its bytes, a newline
        head, _, rest = rest.partition(b"\n")
        size = int(head.split()[2])
        texts[path], rest = rest[:size].decode("utf-8"), rest[size + 1:]
    return TABLE["old_rows"](texts)


def text(commit, rows):
    """The file's text: the commit, then the rows."""
    return json.dumps({"commit": commit, "rows": rows}, indent=2) + "\n"


def main(argv):
    if argv in (["--help"], ["-h"]) or len(argv) > 1:
        print(__doc__.strip())
        return 0 if len(argv) == 1 else 2
    commit = full(ROOT, argv[0] if argv else json.loads(DATA.read_text(encoding="utf-8"))["commit"])
    DATA.write_text(text(commit, rows_at(ROOT, commit)), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
