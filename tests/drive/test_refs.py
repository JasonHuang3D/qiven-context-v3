"""refs' checks against Python, through qiven3-child probe refs
(tests/child/probe.cpp).

- The whitespace table, 29 code points, against the pinned interpreter's
  str.isspace over every code point but the surrogates, so a new pin
  re-checks it; and a name and a path holding each code point between two
  letters, against the rules written below on str.isspace; in parts, a
  case each (WHITESPACE_SPLIT).
- check_path, check_name and check_subject against the same rules, on
  strings generated from a fixed seed out of the rules' own parts.
- The plants, each of which must disagree: a table without U+3000, and a
  line suffix without its #LN-LM form.

Each test prints its counts and seconds."""

import random
import re
import time
import unittest

from tests.drive import binaries

CHILD = "qiven3-child"
SEED = 68
STRINGS = 100_000

SUFFIX = re.compile(r"(:[0-9]+([:-][0-9]+)?|#L[0-9]+(-L?[0-9]+)?)$")
TYPES = ("entity", "activity", "repo", "item", "clause", "module", "entry", "scope")
REPOSITORY = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$")
# The whitespace sweep in parts, a case each (test_whitespace_part1 on), by
# their indices in code_points(), start and end: the 1,112,064 code points
# alone and between two letters passed 30 s whole on CI's slowest job.
WHITESPACE_SPLIT = ((0, 370_688), (370_688, 741_376), (741_376, 1_112_064))


def control(c):
    return ord(c) <= 0x1F or 0x7F <= ord(c) <= 0x9F


def path_ok(s, suffix=SUFFIX):
    return (1 <= len(s) <= 200 and "\\" not in s
            and not any(control(c) or c in "  " for c in s)
            and all(part not in ("", ".", "..") for part in s.split("/"))
            and not suffix.search(s))


def name_ok(s, suffix=SUFFIX):
    return 1 <= len(s) <= 200 and not any(c.isspace() or control(c) for c in s) and not suffix.search(s)


def subject_ok(s, suffix=SUFFIX):
    kind, colon, name = s.partition(":")
    if (len(s) > 160 or not colon or kind not in TYPES or not name
            or any(c.isspace() or control(c) for c in s)):
        return False
    if kind not in ("entry", "scope") and suffix.search(s):
        return False
    if kind != "repo":
        return True
    repository, slash, rest = name.partition("/")
    if "#" in repository or not REPOSITORY.fullmatch(repository):
        return False
    if not slash:
        return True
    path, mark, qualified = rest.partition("#")
    return path_ok(path, suffix) and (not mark or name_ok(qualified, suffix))


def answers(test, strings):
    """The probe's four digits for each string."""
    lines = [s.encode("utf-8").hex() for s in strings]
    ran = binaries.run(CHILD, ["probe", "refs"], ("\n".join(lines) + "\n").encode("ascii"), timeout=600)
    test.assertEqual(ran.status, 0, ran)
    out = ran.out.decode("ascii").split()
    test.assertEqual(len(out), len(strings))
    return out


def code_points():
    return [chr(p) for p in range(0x110000) if not 0xD800 <= p <= 0xDFFF]


PARTS = ("a", "Z", "0", "7", "12", ":", "-", "#", "#L", "L", "/", ".", "..", "_", "\\", " ", "\t", "\x07", "\x7f",
         "\u0085", " ", " ", "　", "记", "\U0001F600", "repo:", "scope:", "entry:", "entity:",
         "module:", "note:", "q", "src/x.py", "::")


def generated(rng, count):
    out = []
    for _ in range(count):
        n = rng.randrange(1, 12)
        s = "".join(rng.choice(PARTS) for _ in range(n))
        if rng.random() < 0.02:
            s = s * rng.randrange(10, 40)
        out.append(s)
    return out


class Refs(unittest.TestCase):
    ctest_labels = ("fake",)

    def whitespace(self, part):
        """The code points of WHITESPACE_SPLIT's part (from 1), alone and
        between two letters. The class holds a case for each part and no
        other, the parts hold every code point once, in order, their sizes
        within one of each other, and str.isspace holds 29 of them."""
        held = sorted(name for name in dir(type(self)) if name.startswith("test_whitespace"))
        self.assertEqual(held, sorted(f"test_whitespace_part{i}" for i in range(1, len(WHITESPACE_SPLIT) + 1)))
        began = time.perf_counter()
        everyone = code_points()
        self.assertEqual([i for start, end in WHITESPACE_SPLIT for i in range(start, end)], list(range(len(everyone))))
        sizes = [end - start for start, end in WHITESPACE_SPLIT]
        self.assertLessEqual(max(sizes) - min(sizes), 1, sizes)
        self.assertEqual(sum(c.isspace() for c in everyone), 29)
        start, end = WHITESPACE_SPLIT[part - 1]
        points = everyone[start:end]
        got = answers(self, points)
        wrong = [hex(ord(c)) for c, row in zip(points, got) if (row[0] == "1") != c.isspace()]
        print(f"refs: whitespace, part {part}: {len(points)} code points, {sum(r[0] == '1' for r in got)} in "
              f"the table, {len(wrong)} disagreements, {time.perf_counter() - began:.1f} s")
        self.assertEqual(wrong, [])
        # Each code point between two letters, as a name and as a path.
        between = ["a" + c + "b" for c in points]
        got = answers(self, between)
        wrong = [(hex(ord(s[1])), row) for s, row in zip(between, got)
                 if (row[1] == "1") != path_ok(s) or (row[2] == "1") != name_ok(s)]
        print(f"refs: between, part {part}: {len(between)} strings, {len(wrong)} disagreements, "
              f"{time.perf_counter() - began:.1f} s")
        self.assertEqual(wrong[:5], [])

    def test_generated(self):
        began = time.perf_counter()
        rng = random.Random(SEED)
        strings = generated(rng, STRINGS)
        got = answers(self, strings)
        wrong, accepted = [], [0, 0, 0]
        for s, row in zip(strings, got):
            verdicts = (path_ok(s), name_ok(s), subject_ok(s))
            for i, v in enumerate(verdicts):
                accepted[i] += v
            if tuple(c == "1" for c in row[1:]) != verdicts:
                wrong.append((s[:80], row, verdicts))
        print(f"refs: generated: {len(strings)} strings, accepted as path, name, subject {accepted}, "
              f"{len(wrong)} disagreements, {time.perf_counter() - began:.1f} s")
        self.assertEqual(wrong[:5], [])
        for count in accepted:
            self.assertGreater(count, STRINGS // 100)

    def test_plants_disagree(self):
        rng = random.Random(SEED)
        strings = generated(rng, 20_000) + ["　", "a　b", "x#L1-L2"]
        got = answers(self, strings)
        no_ideographic = sum((r[0] == "1") != (len(s) == 1 and s.isspace() and s != "　")
                             for s, r in zip(strings, got))
        short = re.compile(r"(:[0-9]+([:-][0-9]+)?|#L[0-9]+(-[0-9]+)?)$")
        no_lines = sum((r[2] == "1") != name_ok(s, short) for s, r in zip(strings, got))
        print(f"refs: plants: without U+3000 {no_ideographic}, without #LN-LM {no_lines} disagreements")
        self.assertGreater(no_ideographic, 0)
        self.assertGreater(no_lines, 0)


def whitespace_case(part):
    """WHITESPACE_SPLIT's part as a case of Refs', named and described."""
    def case(self):
        self.whitespace(part)
    case.__name__ = f"test_whitespace_part{part}"
    case.__doc__ = f"The code points of WHITESPACE_SPLIT's part {part}: as Refs.whitespace."
    return case


for _part in range(1, len(WHITESPACE_SPLIT) + 1):
    setattr(Refs, f"test_whitespace_part{_part}", whitespace_case(_part))


if __name__ == "__main__":
    unittest.main()
