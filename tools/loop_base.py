"""The old places' measure: each table file's rows' paths in the old
places at the base commit (tools/loop_table.py's old_rows), kept in
tools/loop_base.json beside the commit it names, its one naming, so that
the modules' check reads them with no git, on a clone too shallow to hold
the commit too (CI's). tests/drive/test_loop_base.py refuses a file that
differs from the commit.

- rows_at(root, commit): old_rows() of the table's files at commit in
  root's repository, read through tools/loop_git.py's run, two git
  processes; LookupError naming the commit when the repository lacks it.
- text(commit, rows): the file's text.

Run as python -I tools/loop_base.py [COMMIT]: the file written again from
the commit it names, or from COMMIT, then named there.
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


def rows_at(root, commit):
    """old_rows() of the table's files at commit, read from root's .git."""
    git = GIT["which"]()
    if git is None:
        raise FileNotFoundError("no git")
    where = ["--git-dir", str(Path(root) / ".git")]
    listed = GIT["run"](git, [*where, "ls-tree", "-r", "-z", "--name-only", f"{commit}^{{commit}}"])
    if listed.returncode:
        raise LookupError(f"the base {commit} is not in the repository of {root}: "
                          + listed.stderr.decode("utf-8", "replace").strip())
    paths = [p for p in listed.stdout.decode("utf-8").split("\0") if p and TABLE["is_table_file"](p)]
    done = GIT["run"](git, [*where, "cat-file", "--batch"], data="".join(f"{commit}:{p}\n" for p in paths).encode(),
                      check=True)
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
    commit = argv[0] if argv else json.loads(DATA.read_text(encoding="utf-8"))["commit"]
    DATA.write_text(text(commit, rows_at(ROOT, commit)), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
