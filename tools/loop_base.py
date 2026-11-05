"""The old places' measure: each table file's rows' paths in the old
places at the base commit (tools/loop_table.py's old_rows), kept in
tools/loop_base.json beside the commit it names, its one naming, so that
the modules' check reads them with no git, on a clone too shallow to hold
the commit too (CI's).

- rows_at(root, commit): old_rows() of the table's files at commit in
  root's repository (foundation/devkit/python/git.py's files_at), two git
  processes.
- text(commit, rows): the file's text.

Run as python -I tools/loop_base.py [COMMIT]: the file written again from
the commit it names, or from COMMIT, then named there by its full hash;
exit 2 and one line, the file unwritten, when git could not read the commit.
"""

import json
import runpy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent  # the checkout this file is in
sys.path.append(str(ROOT))  # under -I the checkout is off sys.path; appended, so that the standard library still wins
from foundation.devkit.python import git

TABLE = runpy.run_path(str(HERE / "loop_table.py"))
DATA = ROOT / TABLE["BASE"]


def rows_at(root, commit):
    """old_rows() of the table's files at commit, read from root's .git."""
    return TABLE["old_rows"](git.files_at(root, commit, TABLE["is_table_file"]))


def text(commit, rows):
    """The file's text: the commit, then the rows."""
    return json.dumps({"commit": commit, "rows": rows}, indent=2) + "\n"


def main(argv):
    if argv in (["--help"], ["-h"]) or len(argv) > 1:
        print(__doc__.strip())
        return 0 if len(argv) == 1 else 2
    try:
        commit = git.full_hash(ROOT, argv[0] if argv else json.loads(DATA.read_text(encoding="utf-8"))["commit"])
        rows = rows_at(ROOT, commit)
    except git.GitFailed as error:
        print(f"loop_base: could not run: {error}", file=sys.stderr)
        return 2
    DATA.write_text(text(commit, rows), encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
