"""The platform's files case of 938, defined once over an adapter
(contract/platform_cases.py names the binding's part): stage's files,
through the drive's handles (driver/platform_files.hpp).

Here: a failed open making nothing, under faults (938). P9, replace
held, is platform's own (foundation/system/platform/tests/files_test.cpp
and win_files_held_test.cpp), as are P19, publish (files_test.cpp and
win_files_stage_test.cpp), and P25, not UTF-8 (files_test.cpp,
fake_platform_test.cpp and win_platform_test.cpp there). A holder is
the binding's hold(drive, **times): how often each call refuses before it
succeeds, as a holder's handle released in time does, -1 held past any
deadline; none given, none held. On the fake it is its busy counts;
"target" and "temporary" hold replace's rename, "publish" publish's and
discard's; on the Windows drive real handles, "publish" not among them
(machine/windows_holds.py).
The Windows adapter's replace, stage and lock are its module win/files'.
"""

import itertools
import re
from pathlib import Path

from tests.drive.session import debug_library, has_faults

# replace's temporary file beside its target, and stage's directory.
TEMPORARY = re.compile(r"\.(?P<name>.+)\.[0-9a-f]{16}\.tmp")
STAGING = re.compile(r"\.[0-9a-f]{16}\.tmp")

# A Staged's open of "a/b/c.txt" under faults, by adapter: each platform
# call failed in turn, as (call, code, operation). The fake's pages() is a
# fault point, the drive's pages(budget) the request's first call; the
# Windows adapter's is none. Each of the open's own answers io.
OPEN_CALLS = {
    "fake": (("the drive's pages(budget)", "memory", "pages"), ("open_staged's entry", "io", "open"),
             ("the record's insert", "io", "open"), ("the file's open", "io", "open")),
    "windows": (("CreateDirectoryW of a", "io", "open"), ("CreateDirectoryW of a/b", "io", "open"),
                ("CreateFileW", "io", "open")),
}


def bag_of(directory):
    """The files under directory, by their relative paths."""
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in Path(directory).rglob("*") if p.is_file()}


def staging_left(directory):
    """The staging directories stage left in directory."""
    return sorted(p.name for p in Path(directory).iterdir() if STAGING.fullmatch(p.name))


class PlatformFiles:
    def nothing_kept(self, d):
        """On the fake: no file, staging directory or lock held open."""
        if self.ADAPTER == "fake":
            self.assertEqual(d.call("kept"), 0)

    def test_open_failed(self):
        """938: a Staged's open of a file two directories down, each of its
        platform calls failed in turn (OPEN_CALLS), each answering the one
        kind of the call failed, and nothing left in the staging directory,
        the parents made before the failure removed. memory answers on the
        fake alone, at k 1: the drive's own pages(budget) for the request's
        arena, a fault point of the fake's pages(), none of the Windows
        adapter's; each call of the open's own answers io. Then its
        file's own open failed, its parents made first, and one more file
        opened and published: the published directory holds that file
        alone. Built without QIVEN_FAULTS, not applicable."""
        self.needs("files")
        if not has_faults() or debug_library():
            self.skipTest("not applicable: a fault run needs QIVEN_FAULTS and the release library")
        bags = Path(self.files_root()) / "opened"
        bags.mkdir()
        deep = "a/b/c.txt"
        calls = OPEN_CALLS[self.ADAPTER]
        with self.drive() as d:
            staged = d.call("stage", path=str(bags))["id"]
            (inside,) = staging_left(bags)
            for k in itertools.count(1):
                reply = d.reply("staged_open", id=staged, path=deep, fault={"k": k, "of": "platform"})
                if not reply["fault"]["fired"]:
                    break
                with self.subTest(k=k, call=calls[k - 1][0] if k <= len(calls) else None):
                    self.assertLessEqual(k, len(calls), reply)
                    error = reply["error"]
                    got = (error["kind"], error["code"], error["operation"])
                    self.assertEqual(got, ("platform", *calls[k - 1][1:]), reply)
                    self.assertEqual(list((bags / inside).iterdir()), [])
            self.assertTrue(reply["ok"], reply)
            self.assertEqual(k - 1, len(calls))
            self.assertIsNone(d.call("discard", id=staged))
            # The last call failed is the file's own open, after its parents.
            staged = d.call("stage", path=str(bags))["id"]
            reply = d.reply("staged_open", id=staged, path=deep, fault={"k": k - 1, "of": "platform"})
            self.assertEqual((reply["error"]["kind"], reply["error"]["code"]), ("platform", "io"), reply)
            sink = d.call("staged_open", id=staged, path="kept.txt")["id"]
            d.call("sink_write", id=sink, data=b"kept\n".hex())
            self.assertIsNone(d.call("publish", id=staged, name="bag"))
            self.nothing_kept(d)
        self.assertEqual(sorted(p.relative_to(bags / "bag").as_posix() for p in (bags / "bag").rglob("*")),
                         ["kept.txt"])
        self.assertEqual(bag_of(bags / "bag"), {"kept.txt": b"kept\n"})
