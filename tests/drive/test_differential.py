"""The differential test: v3's matcher and strict parser against Python's
own, through qiven3-child probe (tests/child/probe.cpp), on strings
generated from a fixed seed, each at most 10,000 bytes, since the oracle's
time grows with the square of a string's length.

- The twelve patterns of the secret check against re.search over the
  string, on strings mixing the patterns' fragments, their halves and ten
  non-ASCII characters, and on long ones built of the same parts.
- The syntaxes, printed ^...$, against re.fullmatch with what lies between.
- 4,000 fresh SHA-256 and HMAC-SHA-256 digests, which no pattern hits.
- The strict parser's code against the six steps written on json.loads
  with its four hooks (strict() below), on mutated JSON texts.
- The plants, each of which must disagree, or the comparison would not see
  a difference: the pattern ^[^a]{2}$, which the matcher reads over bytes
  and the oracle over code points; a wrong oracle for sk-key, its count
  one short; a wrong strict(), its integers' bound one past.

Each test prints its counts and seconds."""

import hashlib
import hmac
import json
import random
import re
import time
import unittest

from tests.drive import binaries
from tests.structure.test_content import PATTERNS

CHILD = "qiven3-child"
LONGEST = 10_000
SEED = 61
# The strings' counts, fitted to the drive label's limit.
PATTERN_STRINGS = 200_000
LONG_STRINGS = 200
SYNTAX_STRINGS = 200_000
DIGESTS = 4_000
PARSER_STRINGS = 200_000

# The syntaxes the text writes, whole-value. A path and a qualified name
# have rules, not syntaxes: refs checks them itself, outside the matcher.
SYNTAXES = (
    r"^(person|life|project:[a-z0-9][a-z0-9-]{0,47})$",  # the header's scope
    r"^[a-z][a-z0-9-]{0,31}$",  # its lane, and a writer's role
    r"^(loop|import|[a-z][a-z0-9-]{0,31}/s[1-9][0-9]{0,8})$",  # its writer
    r"^sha256:[0-9a-f]{64}$",  # an entry id
    r"^hmac-sha256:[0-9a-f]{64}$",  # a content digest
    r"^[1-9][0-9]{0,8}$",  # a writer's session number
    r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$",  # a JSON key
    r"^[A-Z][A-Z0-9]*(-[A-Z0-9]+)*$",  # a clause id
    r"^[A-Za-z_][A-Za-z0-9_]*((\.|::|/)[A-Za-z_][A-Za-z0-9_]*)*$",  # a test id
    r"^[A-Za-z0-9][A-Za-z0-9._/-]{0,199}$",  # a registry checkout's ref, a branch
    r"^[a-z0-9][a-z0-9-]*$",  # a slug
    r"^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$",  # a reference's repository
    r"^[0-9a-f]{40}$",  # its revision
    r"^[A-Za-z0-9._:-]{1,128}$",  # a transcript's message
    r"^[a-z0-9][a-z0-9-]{0,31}$",  # its client
    r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$",  # its session
    r"^L[0-9]+(-L?[0-9]+)?$",  # an anchor that reads as a line
)
PLANT = "^[^a]{2}$"

NONASCII = ("\u00e9", "\u0085", "\u2028", "\U0001F600", "\u4e2d", "\u8bb0", "\u5fc6", "\uff1a", "\u3000", "\u00a0")
# The secret check's table's fixtures, hits and misses, each built by
# expression, so that this file's own text hits none of the twelve.
FIXTURES = (
    "-----BEGIN " + "RSA PRIVATE KEY-----", "-----BEGIN " + "PGP PRIVATE KEY BLOCK-----", "-----BEGIN " + "PUBLIC KEY-----",
    "ghp_" + "A" * 36, "ghp_" + "A" * 35, "xghp_" + "A" * 36,
    "github_pat_" + "A" * 22 + "_" + "b" * 59, "github_pat_" + "A" * 81,
    "sk-" + "0123456789abcdef" * 2, "sk-proj-" + "B" * 40, "sk-" + "a" * 31, "task-" + "a" * 40,
    "id=AKIA" + "2" * 16, "AKIA" + "a" * 16, "XAKIA" + "A" * 16,
    "xoxb-" + "1" * 12 + "-" + "a" * 24, "xoxc-" + "1" * 10 + "-" + "1" * 10 + "-" + "a" * 64,
    "xoxo-hugs-and-kisses", "xoxb-" + "a" * 20,
    "xoxd-" + "a" * 40, "d=xoxd-" + "a1%2B" * 8, "xoxd-" + "a" * 31, "xxoxd-" + "a" * 40,
    "postgres://app:" + "p" * 8 + "@db/x", "redis://:" + "p" * 8 + "@db",
    "https://example.com:8080/a@b", "ssh://git@host/x", "http://user:@host",
    "Server=db;Password=" + "p" * 8 + ";", "host=db pwd=" + "p" * 8, 'Server=db;Password="' + "p" * 8 + '";',
    "Data Source=db;Pwd='" + "p" * 8 + "'", "DB_PASSWORD=" + "p" * 8, "PGPASSWORD=" + "p" * 8,
    "password: see the vault", "keyword=" + "k" * 8, 'Password="";', "mypwd=" + "p" * 8,
    "Authorization: Bearer " + "a" * 20, '"authorization": "Basic ' + "dXNlcjpw" * 2 + '"',
    "Proxy-Authorization: Token " + "t" * 8, "Authorization: Bearer <token>", "Authorization: Bearer $GH_TOKEN",
    "Authorization: Bearer " + "a" * 7, "Authorization: Digest username=" + "u" * 8, "XAuthorization: Bearer " + "a" * 20,
    "mysql --password " + "p" * 8, "--token=" + "t" * 20, "--api-key '" + "k" * 16 + "'", "--secret " + "s" * 8,
    "--password-stdin", "--token $GH_TOKEN", "--api-key=<key>", "--secret {secret}", "--password ''",
    "x--token " + "t" * 8, "--tokens " + "t" * 8,
    "SecretId=AKID" + "a1B2" * 8, "AKID" + "Z" * 32, "AKID" + "a" * 31, "XAKID" + "a" * 32, "akid" + "a" * 32,
)
EXTRA = ("Authorization", "authorization:", "Bearer ", "--token", "--pass" + "word=", "AKID", ":", "@", "=", " ", "\t",
         "'", "\"", "$", "-", "a" * 8, "\n", "\r", "/", "?", "#", "_")
SYNTAX_PARTS = ("person", "life", "project:", "loop", "import", "/s", "sha256:", "hmac-sha256:", "a", "z", "A", "Z",
                "0", "1", "9", "-", "_", "__", ".", "..", "::", "/", ":", " ", "\n", "x" * 31, "x" * 32, "y" * 47,
                "9" * 8, "9" * 9, "0123456789abcdef" * 4, "0123456789abcde" * 4, "f" * 63, "F" * 64, "p" * 199,
                "L", "#", "0123456789abcdef" * 2 + "01234567", "r" * 99, "m" * 127)


def fragments():
    """Each fixture, its halves, and the extra parts."""
    out = []
    for s in FIXTURES:
        out += [s, s[: len(s) // 2], s[len(s) // 2:]]
    return out + list(EXTRA)


def mixed(rng, parts, low, high):
    """low to high parts, seven in ten from parts, the rest non-ASCII."""
    return "".join(rng.choice(parts) if rng.random() < 0.7 else rng.choice(NONASCII)
                   for _ in range(rng.randint(low, high)))


def long_string(rng, parts):
    """Parts until the next would pass LONGEST bytes."""
    out, size = [], 0
    while True:
        part = rng.choice(parts) if rng.random() < 0.7 else rng.choice(NONASCII)
        if size + len(part.encode("utf-8")) > LONGEST:
            return "".join(out)
        out.append(part)
        size += len(part.encode("utf-8"))


def probe(test, facility, head, strings):
    """The probe's answer lines for strings, after head where there is one."""
    lines = ([head] if head is not None else []) + [s.hex() for s in strings]
    for s in strings:
        test.assertLessEqual(len(s), LONGEST)
    ran = binaries.run(CHILD, ["probe", facility], ("\n".join(lines) + "\n").encode("ascii"), timeout=600)
    test.assertEqual(ran.status, 0, ran)
    return ran.out.decode("ascii").split()


def matched(test, patterns, strings):
    """For each string, a hit or not of each pattern, by the probe."""
    head = " ".join(p.encode("ascii").hex() for p in patterns)
    out = probe(test, "match", head, [s.encode("utf-8") for s in strings])
    test.assertEqual(out[0], "ok", out[:4])
    test.assertEqual(len(out), 1 + len(strings))
    for row in out[1:]:
        test.assertEqual(len(row), len(patterns))
    return [[c == "1" for c in row] for row in out[1:]]


def report(what, count, disagreements, began):
    print(f"differential: {what}: {count} strings, {disagreements} disagreements, "
          f"{time.perf_counter() - began:.1f} s")


class Matcher(unittest.TestCase):
    ctest_reads = ("qiven3-child",)
    ctest_labels = ("fake",)

    def compare(self, patterns, oracles, strings):
        """The disagreements between the probe and the oracles, and the
        first few, by pattern and string."""
        got = matched(self, patterns, strings)
        count, first = 0, []
        for s, row in zip(strings, got):
            for name, oracle, hit in zip(patterns, oracles, row):
                if bool(oracle(s)) != hit:
                    count += 1
                    if len(first) < 5:
                        first.append((name, s[:80], hit))
        return count, first

    def test_patterns(self):
        began = time.perf_counter()
        rng = random.Random(SEED)
        parts = fragments()
        strings = list(FIXTURES) + [mixed(rng, parts, 1, 6) for _ in range(PATTERN_STRINGS)]
        strings += [long_string(rng, parts) for _ in range(LONG_STRINGS)]
        # The oracle's slowest reads: url-password with no @, and with one.
        half = (LONGEST - 6) // 2
        strings += ["a://" + "x" * half + ":" + "p" * half, "a://" + "x" * half + ":" + "p" * (half - 1) + "@"]
        compiled = [re.compile(p) for p in PATTERNS.values()]
        count, first = self.compare(list(PATTERNS.values()), [c.search for c in compiled], strings)
        report("the twelve patterns", len(strings), count, began)
        self.assertEqual(count, 0, first)

    def test_syntaxes(self):
        began = time.perf_counter()
        rng = random.Random(SEED + 1)
        strings = [""] + [mixed(rng, SYNTAX_PARTS, 1, 4) for _ in range(SYNTAX_STRINGS)]
        strings += [long_string(rng, SYNTAX_PARTS) for _ in range(LONG_STRINGS)]
        compiled = [re.compile(p[1:-1]) for p in SYNTAXES]
        count, first = self.compare(list(SYNTAXES), [c.fullmatch for c in compiled], strings)
        report("the syntaxes", len(strings), count, began)
        self.assertEqual(count, 0, first)

    def test_digests_hit_none(self):
        began = time.perf_counter()
        rng = random.Random(SEED + 2)
        strings = []
        for _ in range(DIGESTS // 2):
            key = rng.randbytes(32)
            data = rng.randbytes(rng.randint(0, 64))
            strings.append("sha256:" + hashlib.sha256(data).hexdigest())
            strings.append("hmac-sha256:" + hmac.new(key, data, hashlib.sha256).hexdigest())
        compiled = [re.compile(p) for p in PATTERNS.values()]
        oracle_hits = sum(1 for s in strings for c in compiled if c.search(s))
        probe_hits = sum(sum(row) for row in matched(self, list(PATTERNS.values()), strings))
        report("the digests", len(strings), oracle_hits + probe_hits, began)
        self.assertEqual((oracle_hits, probe_hits), (0, 0))

    def test_plants_disagree(self):
        began = time.perf_counter()
        rng = random.Random(SEED)
        parts = fragments()
        strings = [mixed(rng, parts, 1, 6) for _ in range(PATTERN_STRINGS)]
        plant = re.compile(PLANT[1:-1])
        count, _ = self.compare([PLANT], [plant.fullmatch], strings)
        report(f"the planted {PLANT}", len(strings), count, began)
        self.assertGreater(count, 0)
        # A planted wrong oracle, sk-key's count one short, disagrees.
        wrong = re.compile(PATTERNS["sk-key"].replace("{32,}", "{31,}"))
        count, _ = self.compare([PATTERNS["sk-key"]], [wrong.search], strings)
        report("the planted sk-key oracle, {31,}", len(strings), count, began)
        self.assertGreater(count, 0)


class Refused(Exception):
    """strict()'s code."""


MAX_INTEGER = 2**53 - 1
KEY = re.compile(r"[a-z][a-z0-9]*(_[a-z0-9]+)*")


def nonchar(c):
    return 0xFDD0 <= ord(c) <= 0xFDEF or (ord(c) & 0xFFFE) == 0xFFFE


def strict(data, top=MAX_INTEGER):
    """The strict parser's six steps on json.loads with its four hooks: the
    first fault's code, or accepted; top, the integers' bound, is changed
    by the planted wrong oracle alone."""
    if data.startswith(b"\xef\xbb\xbf"):
        return "bom"
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return "utf8"
    depth, inside, escaped = 0, False, False
    for c in text:
        if inside:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                inside = False
        elif c == '"':
            inside = True
        elif c in "[{":
            depth += 1
            if depth > 16:
                return "depth"
        elif c in "]}":
            depth -= 1

    def pairs(members):
        held = {}
        for k, v in members:
            if k in held:
                raise Refused("duplicate_key")
            held[k] = v
        return held

    def constant(_):
        raise Refused("constant")

    def floating(_):
        raise Refused("float")

    def integer(s):
        if len(s.lstrip("-")) > 16 or abs(int(s)) > top:
            raise Refused("int_range")
        return int(s)

    def walk(x):
        if x is None:
            raise Refused("null")
        if isinstance(x, str):
            for c in x:
                if 0xD800 <= ord(c) <= 0xDFFF:
                    raise Refused("surrogate")
                if nonchar(c):
                    raise Refused("nonchar")
        elif isinstance(x, list):
            for y in x:
                walk(y)
        elif isinstance(x, dict):
            for k, y in x.items():
                walk(k)
                if not KEY.fullmatch(k):
                    raise Refused("key_syntax")
                walk(y)

    try:
        value = json.loads(text, object_pairs_hook=pairs, parse_constant=constant, parse_float=floating,
                           parse_int=integer)
        walk(value)
    except Refused as refused:
        return str(refused)
    except json.JSONDecodeError:
        return "syntax"
    canonical = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False)
    return "accepted" if canonical.encode("utf-8") == data else "not_canonical"


# The texts' parts: scalars and keys at each code's edge, written as JSON
# text, escapes as such; separators, JSON's and others; bytes a mutation
# puts in.
SCALARS = ("0", "1", "-1", "-0", "12", "9007199254740991", "-9007199254740991", "9007199254740992",
           "9999999999999999", "-9999999999999999", "1000000000000000", "-1000000000000000", "10000000000000000",
           "99999999999999999", "1.5", "1E5", "1e400", "01", "+1", "1.", "-", "NaN", "-NaN", "Infinity",
           "-Infinity", "true", "false", "null", "tru", '""', '"a"', '"\\u00e9"', '"\\ud800"', '"\\udd1e"',
           '"\\ud834\\udd1e"', '"\\ud834\\u0041"', '"\\udd1e\\ud834"', '"\\ufdd0"', '"\\uffff"', '"\\u001f"',
           '"\\u001F"', '"\\u007f"', '"\\/"', '"\\x41"', '"\\u12"', '"\\n"', '"\\t"', '"\u00e9"', '"\u2028"',
           '"\u0085"', '"\ufdef"', '"\ufdf0"', '"\U0001fffe"', '"\x01"', '"\x7f"', '"\ufeff"', '"[{"', '"\\"]"')
KEYS = ('"a"', '"b"', '"aa"', '"a_b"', '"a1"', '"A"', '"1a"', '"a__b"', '"a_"', '""', '"\\u0061"', '"\\ud800"',
        '"\\ufdd0"', '"\u00e9"', '"a\\n"', "'a'", "a")
COMMAS = (",",) * 12 + ("", ",,", " ,", ", ", "\n,", "\t,", "\r,", "\x0b,", "\u00a0,")
COLONS = (":",) * 12 + ("", " :", ": ", "::", "\u3000:")
BYTES = (b"\xff", b"\xc3", b"\xed\xa0\x80", b"\x00", b'"', b"\\", b"]", b"}", b"[", b"{", b",", b":", b" ", b"\n")


def text_of(rng, depth):
    """A JSON-like text, depth levels at most."""
    roll = rng.random()
    if depth == 0 or roll < 0.45:
        return rng.choice(SCALARS)
    count = rng.randint(0, 3)
    if roll < 0.7:
        return "[" + rng.choice(COMMAS).join(text_of(rng, depth - 1) for _ in range(count)) + "]"
    keys = [rng.choice(KEYS) if rng.random() < 0.6 else rng.choice(KEYS[:2]) for _ in range(count)]
    members = (key + rng.choice(COLONS) + text_of(rng, depth - 1) for key in keys)
    return "{" + rng.choice(COMMAS).join(members) + "}"


def parser_input(rng):
    """A text, often nested deep, then often mutated at the byte level."""
    data = text_of(rng, rng.randint(0, 4))
    if rng.random() < 0.1:
        levels = rng.randint(12, 18)
        opener = rng.choice(("[", '{"a":'))
        data = opener * levels + data + ("]" if opener == "[" else "}") * levels
    if rng.random() < 0.03:
        data = rng.choice(("]", "}", "]]]")) + data
    raw = data.encode("utf-8", "surrogatepass")
    for _ in range(rng.choice((0, 0, 0, 1, 1, 2))):
        at = rng.randint(0, len(raw))
        roll = rng.random()
        if roll < 0.5:
            raw = raw[:at] + rng.choice(BYTES) + raw[at:]
        elif roll < 0.8:
            raw = raw[:at] + raw[at + 1:]
        else:
            raw = raw[:at]
    if rng.random() < 0.02:
        raw = b"\xef\xbb\xbf" + raw
    if rng.random() < 0.03:
        raw += rng.choice((b"\n", b" ", b"\r\n"))
    return raw[:LONGEST]


def long_input(rng):
    """An array of texts up to LONGEST bytes."""
    items, size = [], 2
    while True:
        item = text_of(rng, 3).encode("utf-8")
        if size + len(item) + 1 > LONGEST:
            return b"[" + b",".join(items) + b"]"
        items.append(item)
        size += len(item) + 1


class Parser(unittest.TestCase):
    ctest_reads = ("qiven3-child",)
    ctest_labels = ("fake",)

    def test_codes(self):
        began = time.perf_counter()
        rng = random.Random(SEED + 3)
        strings = [parser_input(rng) for _ in range(PARSER_STRINGS)]
        strings += [long_input(rng) for _ in range(LONG_STRINGS)]
        strings += [b"[" * 17, b"[" * 16 + b"]" * 16, b"]" * 3 + b"[" * 17, b"[" * 17 + b"\xff", b"\xef\xbb\xbf\xff"]
        got = probe(self, "parse", None, strings)
        self.assertEqual(len(got), len(strings))
        codes, count, first = {}, 0, []
        for data, code in zip(strings, got):
            want = strict(data)
            codes[want] = codes.get(want, 0) + 1
            if code != want:
                count += 1
                if len(first) < 5:
                    first.append((data[:80], code, want))
        report("the strict parser", len(strings), count, began)
        print("differential: the oracle's codes:", ", ".join(f"{k} {v}" for k, v in sorted(codes.items())))
        self.assertEqual(count, 0, first)
        # A planted wrong oracle, its bound one past 2^53 - 1, disagrees.
        planted = sum(1 for data, code in zip(strings, got) if code != strict(data, MAX_INTEGER + 1))
        print(f"differential: the planted oracle, its bound 2^53: {planted} disagreements")
        self.assertGreater(planted, 0)


if __name__ == "__main__":
    unittest.main()
