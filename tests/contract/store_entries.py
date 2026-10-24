"""The entries the store cases append, and the check that a refusal wrote
nothing: helpers of the case modules (store_cases.py, store_more.py,
slow/store_peak.py), no case here.

Every entry is a fixture of entries.json, as the drive's stage book stores
it, or one made like a fixture through the drive's fixture_made, its fields
changed and its id and content digest computed there: no entry's bytes are
written here but a content, as canonical JSON (canonical() below, which the
store's strict parser checks again at each append).
"""

import hashlib
import json
from pathlib import Path

SCOPE = "project:qiven"
PERSON = "person"
LANES = ("implementer", "reviewer", "learner")
BODY_HEAD = b'{"content":'
BODY_TAIL = b',"id":"'


def canonical(value):
    """value's canonical JSON text: sorted keys, no spaces, UTF-8 raw. The
    contents here are plain text, which this form writes as the format's."""
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def header(entry):
    """An entry's header fields, read from its line."""
    return json.loads(entry[0])


def id_of(entry):
    return header(entry)["id"]


def seq_of(entry):
    return header(entry)["seq"]


def content_text(entry):
    """An entry's content, as its body record holds it, text."""
    record = entry[1]
    return record[len(BODY_HEAD):record.rindex(BODY_TAIL)].decode("utf-8")


def at(minute):
    """A time minute minutes after 2026-10-17T08:00, as the header has it."""
    return f"2026-10-17T{8 + minute // 60:02d}:{minute % 60:02d}:00.000Z"


def writer_of(lane, like_writer):
    """The writer of a lane, the like's session kept: its role the lane's."""
    return lane + like_writer[like_writer.index("/"):]


class Maker:
    """Entries made through one drive: fixtures by name, and entries made
    like them."""

    def __init__(self, drive):
        self.drive = drive
        self.fixtures = {}

    def fixture(self, name):
        if name not in self.fixtures:
            self.fixtures[name] = self.drive.fixture_entry(name)
        return self.fixtures[name]

    def content(self, name):
        """A fixture's content, as a value."""
        return json.loads(content_text(self.fixture(name)))

    def made(self, like, content=None, absent=(), key_of=None, **fields):
        """An entry like the fixture: fields set, absent removed, content a
        value (written canonical) or text, the fixture's where None."""
        if content is not None and not isinstance(content, str):
            content = canonical(content)
        return self.drive.fixture_made(like, fields, absent, content, key_of)

    def on(self, like, scope, lane, seq, parent, minute, content=None, **fields):
        """An entry like the fixture on (scope, lane) at seq after parent (an
        entry or None), its writer's role the lane's, at minute."""
        writer = writer_of(lane, header(self.fixture(like))["writer"])
        absent = ("parent",) if parent is None else ()
        if parent is not None:
            fields["parent"] = id_of(parent)
        return self.made(like, content, absent, scope=scope, lane=lane, seq=seq, writer=writer, at=at(minute),
                         **fields)

    def claim(self, scope, lane, seq, parent, minute, text=None, mistake=None):
        """A's claim on (scope, lane), its claim's text, or its mistake's,
        given: one content for each text."""
        content = self.content("A")
        if text is not None:
            content["claim"] = text
        if mistake is not None:
            content["admit"]["mistake"] = mistake
        return self.on("A", scope, lane, seq, parent, minute, content)

    def chain(self, scope, lane, count, after=None, minute=0):
        """count claims on (scope, lane), the first after `after` (an entry
        or None), each a minute after the last, each its own text."""
        out = []
        parent, seq = after, 0 if after is None else seq_of(after)
        for i in range(count):
            parent = self.claim(scope, lane, seq + i + 1, parent, minute + i,
                                text=f"Entry {seq + i + 1} of {lane} in {scope}.")
            out.append(parent)
        return out

    def forget(self, scope, lane, seq, parent, target, minute):
        """E's forget on (scope, lane) at seq after parent, of target."""
        writer = writer_of(lane, header(self.fixture("E"))["writer"])
        return self.made("E", absent=("content",), scope=scope, lane=lane, seq=seq, parent=id_of(parent),
                         writer=writer, target=id_of(target), at=at(minute))

    def held(self, *names):
        """The fixtures by name, in the order given."""
        return [self.fixture(name) for name in names]


def file_state(path):
    """A file's size and SHA-256, or None where there is none."""
    p = Path(path)
    if not p.exists():
        return None
    data = p.read_bytes()
    return len(data), hashlib.sha256(data).hexdigest()


def scanned(store, **range_):
    """A scan's header lines, read to its end, the scan then ended."""
    scan = store.scan(**range_)
    lines = list(scan)
    scan.close()
    return lines


def snapshot(store):
    """What a refusal must leave as it was (F74): the heads, the scan, the
    size and count; on a store with a file, the database's SHA-256 and the
    -wal's size and SHA-256 (-shm, an index the engine rewrites at open,
    left out)."""
    out = {"files": None}
    if store.path is not None:
        out["files"] = snapshot_files(store)
    out["heads"] = store.heads()
    out["scan"] = scanned(store)
    out["volume"] = store.size()["volume"]
    return out


def guard_of(store):
    """A forget's guard: every head the store holds, (scope, lane, id)."""
    return [(r["scope"], r["lane"], r["id"]) for r in store.heads()["rows"]]


def snapshot_files(store):
    """The database's size and SHA-256, and the -wal's, None for none."""
    return file_state(store.path), file_state(store.path + "-wal")


def same_files(before, after):
    """None where the files are as before, else what differs: a -wal
    absent before may be absent or empty after."""
    (db, wal), (db2, wal2) = before, after
    if db != db2:
        return f"the database: {db} then {db2}"
    if wal is not None and wal != wal2:
        return f"the -wal: {wal} then {wal2}"
    if wal is None and wal2 not in (None, (0, hashlib.sha256(b"").hexdigest())):
        return f"the -wal made: {wal2}"
    return None


def same_snapshot(before, after):
    """None where nothing was written between the snapshots, else what
    differs."""
    for key in ("heads", "scan", "volume"):
        if before[key] != after[key]:
            return f"{key}: {before[key]} then {after[key]}"
    if before["files"] is not None:
        return same_files(before["files"], after["files"])
    return None


# The entry format's parser table, each row's bytes given as a header line,
# with the code the strict parser refuses it with.
PARSER_ROWS = (
    (b"\xef\xbb\xbf{}", "bom"),
    (b'{"a":1,"a":2}', "duplicate_key"),
    (b'{"a":NaN}', "constant"),
    (b'{"a":Infinity}', "constant"),
    (b'{"a":1e400}', "float"),
    (b'{"a":1.0}', "float"),
    (b'{"a":' + b"9" * 400 + b"}", "int_range"),
    (b'{"a":9007199254740992}', "int_range"),
    (b'{"a":"\\ud800"}', "surrogate"),
    (b'{"a":null}', "null"),
    (b'{"A":1}', "key_syntax"),
    (b'{"a": 1}', "not_canonical"),
    (b'{"b":1,"a":2}', "not_canonical"),
    (b'{"a":"\\u00e9"}', "not_canonical"),
    (b'{"a":"\\u8bb0"}', "not_canonical"),
    (b'{"a":-0}', "not_canonical"),
    (b'{"a":"\xff"}', "utf8"),
    (b'{"a":"\xed\xa0\x80"}', "utf8"),
    (b'{"a":' * 16 + b"{}" + b"}" * 16, "depth"),
    (b"[" * 100000 + b"]" * 100000, "depth"),
    (b'{"a":"\xef\xbf\xbe"}', "nonchar"),
    (b'{"a":"\xf4\x8f\xbf\xbf"}', "nonchar"),
    (b'{"a":"\\ufdd0"}', "nonchar"),
    (b'{"\\ud800":1}', "surrogate"),
    (b'{"\\ufdd0":1}', "nonchar"),
)

# The validator's table: A's header with one change each, its id kept.
HEADER_ROWS = (
    ("owner", {"writer": "owner/s1", "lane": "owner"}),
    ("reserved", {"writer": "loop/s1", "lane": "loop"}),
    ("reserved", {"writer": "import/s1", "lane": "import"}),
    ("empty", {"parent": "", "seq": 2}),
    ("seq", {"seq": 2}),
    ("syntax", {"at": "2026-10-17T09:30:00Z"}),
    ("syntax", {"at": "2026-02-30T09:30:00.000Z"}),
    ("lane", {"lane": "reviewer"}),
    ("reflect_kind", {"writer": "reflect/s39", "lane": "reflect", "kind": "direction"}),
    ("target", {"kind": "check"}),
    ("id_mismatch", {"basis": "observed"}),
    ("unknown_key", {"note": "n"}),
    ("missing_key", {"writer": None}),
    ("missing_key", {"id": None}),
)


def chain_fields(entry):
    """The fields of an entry's header that fixture_made sets."""
    keys = ("scope", "lane", "seq", "parent", "kind", "writer", "basis", "at", "target")
    return {k: v for k, v in header(entry).items() if k in keys}


def changed_header(entry, changes):
    """The entry's header line with each change made, a None removing its
    key; its id kept."""
    fields = header(entry)
    for key, value in changes.items():
        if value is None:
            del fields[key]
        else:
            fields[key] = value
    return canonical(fields).encode("utf-8")


def body_rows(a, b):
    """The validator's table's body rows on A: (code, record)."""
    record = a[1]
    key = json.loads(record)["key"]
    return (
        ("body_mismatch", record.replace(b'"band":"high"', b'"band":"low"')),
        ("body_mismatch", record.replace(id_of(a).encode(), id_of(b).encode())),
        ("syntax", record.replace(key.encode(), key.upper().encode())),
        ("unknown_key", record[:-1] + b',"note":"n"}'),
        ("missing_key", record.replace(b',"key":"' + key.encode() + b'"', b"")),
    )


# F138's clearing fixtures, each with the pattern it trips.
CLEARING = (
    ("PWD" + "=/path/to/repo.git", "pair-password"),
    ("git clone https://x-access-token:" + "${GH_TOKEN}@github.com/o/r", "url-password"),
    ("postgres://<user>:" + "<password>@<host>/<db>", "url-password"),
    ("AKIA" + "IOSFODNN7EXAMPLE", "aws-akia"),
)


def clearing(m, digest, pattern, seq, parent, minute):
    """D's direction on the reviewer's lane, clearing pattern for the
    content of that digest."""
    content = m.content("D")
    content["clears"] = {"content": digest, "pattern": pattern}
    return m.on("D", SCOPE, "reviewer", seq, parent, minute, content)


def oversized(m):
    """A's claim, its mistake 70,000 bytes: a content past 65,536 bytes
    canonical, refused by its field's bound, which the content check reads
    before the content's (too_long at admit.mistake). A module at every
    field's bound (MX) stays within 65,536 bytes."""
    content = m.content("A")
    content["admit"]["mistake"] = "x" * 70000
    assert len(canonical(content).encode("utf-8")) > 65536
    return m.on("A", SCOPE, "implementer", 1, None, 0, content)


def exported(store, **range_):
    """An export's snapshot figures and its lines read whole, the export
    then closed and ended."""
    x = store.export(**range_)
    heads, lines = x.heads(), list(x)
    x.close()
    x.drop()
    return heads, lines


def lane_heads(lines):
    """The head rows of an import's lines: each lane's last."""
    out = {}
    for line in lines:
        h = header((line["line"], None))
        out[h["scope"], h["lane"]] = {"scope": h["scope"], "lane": h["lane"], "id": h["id"], "seq": h["seq"]}
    return [out[k] for k in sorted(out)]


def as_line(entry, forgotten=False, body=True):
    """An entry as an import's line: its body held, or not."""
    return {"line": entry[0], "body": entry[1] if body and not forgotten else None, "forgotten": forgotten}


def fillers(m, scope, lane, count, size=1024):
    """count claims on (scope, lane), each body record about size bytes."""
    probe = m.claim(scope, lane, 1, None, 0, text="x")
    pad = max(1, min(990, size - len(probe[1]) + 1))
    out, parent = [], None
    for i in range(count):
        parent = m.claim(scope, lane, i + 1, parent, i // 60, text=f"Entry {i + 1}: " + "f" * pad)
        out.append(parent)
    return out
