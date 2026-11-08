"""The modules' check's command line; foundation/devkit/structure/modules.py holds the check,
foundation/devkit/python/table.py the table's reader.

    py -3.15 -I tools/loop_modules.py SOURCE [BUILD]
    py -3.15 -I tools/loop_modules.py --help
"""

import sys
from pathlib import Path

# Under -I the checkout is off sys.path; appended, so that the standard library still wins.
sys.path.append(str(Path(__file__).resolve().parents[1]))
from foundation.devkit.python import git
from foundation.devkit.python import table
from foundation.devkit.structure import modules


def main(argv):
    if argv in (["--help"], ["-h"]):
        print("\n\n".join(text.strip() for text in (__doc__, modules.__doc__, table.__doc__)))
        return 0
    if len(argv) not in (1, 2):
        print("usage: loop_modules.py SOURCE [BUILD]", file=sys.stderr)
        return 2
    try:
        errors, left, trees = modules.run(*argv)
    except (OSError, ValueError, KeyError, git.GitFailed) as error:
        print(f"modules: the check could not run: {error}", file=sys.stderr)
        return 2
    for line in errors:
        print(f"modules: {line}")
    print(f"vendored: {len(left)} includes in {', '.join(trees.vendored)} left unresolved")
    print(f"modules: {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
