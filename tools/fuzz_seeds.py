"""Seeds a fuzz target's corpus, at the build (tests/fuzz/CMakeLists.txt).

    fuzz_seeds.py <checkout> <target> <corpus>

The seeds: the target's kept inputs, tests/fuzz/<target>/regress/; and,
where the checkout has them, each file of the goldens (tests/goldens/v1/)
and of tests/fixtures/, and each line of tests/vectors/ids.json, each as its
bytes. Each seed is written as <corpus>/<sha1 of its bytes> unless that
file is there already: libFuzzer grows the corpus, and nothing here removes
or rewrites what it holds. Prints one line, and exits 0, or 2 when it
could not run.
"""

import hashlib
import sys
from pathlib import Path

TARGETS = ("digest",)


def files(root):
    """Each file under root, by its path, or none when root is absent."""
    if not root.is_dir():
        return []
    return sorted(p for p in root.rglob("*") if p.is_file())


def seeds(checkout, target):
    """Each seed's bytes, in a stable order."""
    tests = checkout / "tests"
    found = [p.read_bytes() for p in files(tests / "fuzz" / target / "regress")]
    for root in (tests / "goldens" / "v1", tests / "fixtures"):
        found += [p.read_bytes() for p in files(root)]
    ids = tests / "vectors" / "ids.json"
    if ids.is_file():
        found += [line for line in ids.read_bytes().split(b"\n") if line.strip()]
    return found


def main(argv):
    if len(argv) != 4 or argv[2] not in TARGETS:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    checkout, target, corpus = Path(argv[1]), argv[2], Path(argv[3])
    if not (checkout / "tests" / "fuzz" / target).is_dir():
        print(f"fuzz_seeds: {checkout} holds no tests/fuzz/{target}/", file=sys.stderr)
        return 2
    corpus.mkdir(parents=True, exist_ok=True)
    all_seeds = seeds(checkout, target)
    new = 0
    for data in all_seeds:
        path = corpus / hashlib.sha1(data).hexdigest()
        if not path.exists():
            path.write_bytes(data)
            new += 1
    held = sum(1 for p in corpus.iterdir() if p.is_file())
    print(f"fuzz_seeds: {target}: {len(all_seeds)} seeds, {new} new; the corpus holds {held}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
