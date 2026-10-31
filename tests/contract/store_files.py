"""The store's files, as the cases that read or forge them see them:
helpers of the case modules (store_more.py), no case here.

The raw-byte scan of a store's directory (S11), and the forgeries of a
SQLite store's file made after its close (S16, S17): bytes changed in
place, a page's type byte, the header's application_id and user_version,
the journal mode's bytes, and a SQLite file of one page holding no
table. The header's offsets are SQLite's file format's: the page size at
16 (two bytes, big-endian), the write and read versions at 18 and 19 (1
a DELETE-mode file, 2 a WAL one), the page count at 28, user_version at
60 and application_id at 68 (four bytes each, big-endian).
"""

import struct
from pathlib import Path

PAGE_SIZE_AT = 16
VERSIONS_AT = 18
USER_VERSION_AT = 60
APPLICATION_ID_AT = 68
HEADER_BYTES = 100
# A b-tree page's first byte: an interior or leaf page, of an index or a
# table.
PAGE_TYPES = (2, 5, 10, 13)


def marker(name, text):
    """A marker's patterns: its UTF-8 and its UTF-16LE bytes, each named."""
    return [(f"{name} utf-8", text.encode("utf-8")), (f"{name} utf-16le", text.encode("utf-16-le"))]


def key(name, hex_text):
    """A key's patterns: its hex in lower and in upper case, each named."""
    return [(f"{name} hex", hex_text.lower().encode("ascii")), (f"{name} HEX", hex_text.upper().encode("ascii"))]


def scan(directory, patterns):
    """Every file under directory, (name, size), and each hit, (name,
    pattern's name, offset), in the order found."""
    files, hits = [], []
    for path in sorted(p for p in Path(directory).rglob("*") if p.is_file()):
        data = path.read_bytes()
        files.append((path.name, len(data)))
        for name, pattern in patterns:
            at = data.find(pattern)
            while at >= 0:
                hits.append((path.name, name, at))
                at = data.find(pattern, at + 1)
    return files, hits


def side_files(path):
    """The -wal and -shm a SQLite file has beside it, by their suffixes."""
    return [s for s in ("-wal", "-shm", "-journal") if Path(str(path) + s).exists()]


def changed_once(data, old, new):
    """data with its one occurrence of old replaced by new, as long."""
    assert len(old) == len(new), (old, new)
    at = data.find(old)
    if at < 0 or data.find(old, at + 1) >= 0:
        raise AssertionError(f"{old[:40]!r}: {data.count(old)} occurrences in the file, not one")
    return data[:at] + new + data[at + len(old):]


def digit_changed(text, at):
    """text (bytes) with its hex digit at `at` changed to another."""
    digit = text[at:at + 1]
    assert digit in b"0123456789abcdef", digit
    return text[:at] + (b"1" if digit == b"0" else b"0") + text[at + 1:]


def page_size(data):
    size = struct.unpack_from(">H", data, PAGE_SIZE_AT)[0]
    return 65536 if size == 1 else size


def type_byte_changed(data):
    """data with the type byte of its first b-tree page after the first
    (the schema's) set to 0, which is no page type; and that page's
    number."""
    size = page_size(data)
    for page in range(2, len(data) // size + 1):
        at = (page - 1) * size
        if data[at] in PAGE_TYPES:
            return data[:at] + b"\x00" + data[at + 1:], page
    raise AssertionError("no b-tree page after the first")


def patched(data, at, value):
    """data with the four bytes at `at` set to value, big-endian."""
    return data[:at] + struct.pack(">I", value) + data[at + 4:]


def delete_mode(data, application_id):
    """A SQLite file in DELETE mode, its application_id given: data's
    journal mode bytes set to 1 and 1, as a file made without WAL has
    them."""
    return patched(data[:VERSIONS_AT] + b"\x01\x01" + data[VERSIONS_AT + 2:], APPLICATION_ID_AT, application_id)


def table_less(size=4096):
    """A SQLite file of one page in DELETE mode holding no table, its
    application_id and user_version 0: the header, then the schema's
    table b-tree leaf with no cell."""
    header = bytearray(HEADER_BYTES)
    header[0:16] = b"SQLite format 3\x00"
    struct.pack_into(">H", header, PAGE_SIZE_AT, size)
    header[18:24] = bytes((1, 1, 0, 64, 32, 32))
    struct.pack_into(">I", header, 24, 1)       # the file change counter
    struct.pack_into(">I", header, 28, 1)       # the page count
    struct.pack_into(">I", header, 44, 4)       # the schema format
    struct.pack_into(">I", header, 56, 1)       # UTF-8
    struct.pack_into(">I", header, 92, 1)       # valid for the change counter
    struct.pack_into(">I", header, 96, 3053004)  # the version that wrote it
    page = bytearray(size - HEADER_BYTES)
    page[0] = 13
    struct.pack_into(">H", page, 5, size)       # the cell content's start: none
    return bytes(header) + bytes(page)
