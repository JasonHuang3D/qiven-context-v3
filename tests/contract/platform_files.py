"""The platform's cases P9, P19 and P25, each defined once over an adapter
(contract/platform_cases.py names the binding's part): replace's and
stage's files, through the drive's handles (driver/platform_files.hpp).

Here: P9 (replace held), P19 (publish) and P25 (not UTF-8). A holder is
the binding's hold(drive, **times): how often each call refuses before it
succeeds, as a holder's handle released in time does, -1 held past any
deadline; none given, none held. On the fake it is its busy counts;
"target" and "temporary" hold replace's rename, "publish" publish's and
discard's; on the Windows drive real handles (machine/windows_holds.py).
The Windows adapter's replace, stage and lock are its module win/files',
and both bindings set STAGE true; a binding of an adapter without stage
sets it false, checks that stage answers unsupported, and skips the case,
or P25's part after replace.
"""

import os
import re
from pathlib import Path

# replace's temporary file beside its target, and stage's directory.
TEMPORARY = re.compile(r"\.(?P<name>.+)\.[0-9a-f]{16}\.tmp")
STAGING = re.compile(r"\.[0-9a-f]{16}\.tmp")

# P19's files, by their relative paths.
BAG = {"bagit.txt": b"BagIt-Version: 1.0\n", "data/headers.jsonl": b'{"id":"a"}\n'}

# P25's bytes: an invalid byte, then a lone surrogate's three bytes.
NOT_UTF8 = (b"\xff", b"\xed\xa0\x80")


def bag_of(directory):
    """The files under directory, by their relative paths."""
    return {p.relative_to(directory).as_posix(): p.read_bytes() for p in Path(directory).rglob("*") if p.is_file()}


def staging_left(directory):
    """The staging directories stage left in directory."""
    return sorted(p.name for p in Path(directory).iterdir() if STAGING.fullmatch(p.name))


def temporaries(directory):
    """The temporary files replace left in directory."""
    return sorted(p.name for p in Path(directory).iterdir() if TEMPORARY.fullmatch(p.name))


class PlatformFiles:
    STAGE = True

    def stage_unsupported(self, d):
        """On an adapter without stage yet: stage answers unsupported, and
        the case is skipped."""
        error = self.failure(d.call, "stage", path=str(self.files_root()))
        self.assert_platform_error(error, "unsupported")
        self.skipTest("until package 5: stage: win/files")

    def nothing_kept(self, d):
        """On the fake: no file, staging directory or lock held open."""
        if self.ADAPTER == "fake":
            self.assertEqual(d.call("kept"), 0)

    def test_P9_replace_held(self):
        """A reader holds the target, released before the deadline, then
        held past it; a handle on the temporary file, released before the
        deadline, then held past it; a deadline of 0, one try."""
        self.needs("files")
        root = Path(self.files_root())
        target = root / "target.txt"
        target.write_bytes(b"old")
        with self.drive() as d:
            for held, data in (("target", b"new 1"), ("temporary", b"new 2")):
                with self.subTest(held=held, released=True):
                    target.write_bytes(b"old")
                    self.hold(d, **{held: 3})
                    self.assertIsNone(d.call("replace", path=str(target), data=data.hex()))
                    self.assertEqual(target.read_bytes(), data)
                    self.assertEqual(temporaries(root), [])
                with self.subTest(held=held, released=False):
                    target.write_bytes(b"old")
                    self.hold(d, **{held: -1})
                    error = self.failure(d.call, "replace", path=str(target), data=data.hex(), deadline=300)
                    self.hold(d)
                    self.assert_platform_error(error, "busy")
                    self.assertEqual(error.operation, "replace")
                    self.assertEqual(target.read_bytes(), b"old")
                    left = temporaries(root)
                    if held == "target":
                        self.assertEqual(left, [])
                        self.assertEqual(error.detail, "rename: error 5 (ERROR_ACCESS_DENIED)")
                    else:
                        # Its deletion refused too: the detail names it.
                        self.assertEqual(len(left), 1, left)
                        self.assertEqual(TEMPORARY.fullmatch(left[0])["name"], target.name)
                        self.assertEqual(error.detail, f"rename: error 32 (ERROR_SHARING_VIOLATION); left {left[0]}, "
                                                       "delete: error 32 (ERROR_SHARING_VIOLATION)")
                        (root / left[0]).unlink()
            # A deadline of 0 gets one try.
            self.hold(d, target=1)
            self.assert_platform_error(
                self.failure(d.call, "replace", path=str(target), data=b"once".hex(), deadline=0), "busy")
            self.assertEqual(target.read_bytes(), b"old")
            self.hold(d, target=1)
            self.assertIsNone(d.call("replace", path=str(target), data=b"twice".hex()))
            self.assertEqual(target.read_bytes(), b"twice")
            self.assertEqual(temporaries(root), [])
            self.nothing_kept(d)

    def staged_bag(self, d, directory):
        """BAG staged in directory through stage: its Staged's id, and the
        id of its last file, left open for publish to close."""
        staged = d.call("stage", path=str(directory))["id"]
        sinks = []
        for path, data in BAG.items():
            sink = d.call("staged_open", id=staged, path=path)["id"]
            d.call("sink_write", id=sink, data=data.hex())
            sinks.append(sink)
        self.assertIsNone(d.call("sink_close", id=sinks[0]))
        return staged, sinks[-1]

    def test_P19_publish(self):
        """BAG written through stage, then published; again with a file
        inside held, released before the deadline, then held past it; then
        onto a present name; open of ../x, publish of a/b."""
        self.needs("files")
        bags = Path(self.files_root()) / "bags"
        bags.mkdir()
        with self.drive() as d:
            if not self.STAGE:
                self.stage_unsupported(d)
            for name, times in (("one", 0), ("two", 3)):
                with self.subTest(name=name, held=times):
                    staged, open_sink = self.staged_bag(d, bags)
                    self.hold(d, publish=times)
                    self.assertIsNone(d.call("publish", id=staged, name=name))
                    self.assertEqual(bag_of(bags / name), BAG)
                    self.assertEqual(staging_left(bags), [])
                    # publish closed it: a second close is none.
                    self.assertIsNone(d.call("sink_close", id=open_sink))

            # Held past the deadline: busy, nothing under the name; the
            # discard publish made first stopped, naming what is left, which
            # a discard after the release removes.
            staged, open_sink = self.staged_bag(d, bags)
            self.hold(d, publish=-1)
            error = self.failure(d.call, "publish", id=staged, name="three", deadline=300)
            self.assert_platform_error(error, "busy")
            self.assertEqual(error.operation, "publish")
            self.assertFalse((bags / "three").exists())
            left = staging_left(bags)
            self.assertEqual(len(left), 1, left)
            # The detail names the staging directory left and, by its path
            # inside it, the first file the held file stopped.
            self.assertEqual(error.detail, f"rename: error 5 (ERROR_ACCESS_DENIED); left {left[0]}/bagit.txt, "
                                           "delete: error 32 (ERROR_SHARING_VIOLATION)")
            self.assertTrue((bags / left[0] / "bagit.txt").is_file())
            self.hold(d)
            self.assertIsNone(d.call("discard", id=staged))
            self.assertEqual(staging_left(bags), [])
            self.assertIsNone(d.call("sink_close", id=open_sink))

            # Onto a present name: exists, the present one's bytes kept,
            # the staging directory discarded first.
            (bags / "one" / "bagit.txt").write_bytes(b"present")
            staged, open_sink = self.staged_bag(d, bags)
            self.assert_platform_error(self.failure(d.call, "publish", id=staged, name="one"), "exists")
            self.assertEqual(bag_of(bags / "one"), {**BAG, "bagit.txt": b"present"})
            self.assertEqual(staging_left(bags), [])
            self.assertIsNone(d.call("discard", id=staged))
            self.assertIsNone(d.call("sink_close", id=open_sink))

            # A path that leaves the directory, and a name of two parts.
            staged = d.call("stage", path=str(bags))["id"]
            self.assert_platform_error(self.failure(d.call, "staged_open", id=staged, path="../x"), "path")
            self.assert_platform_error(self.failure(d.call, "publish", id=staged, name="a/b"), "path")
            self.assertIsNone(d.call("discard", id=staged))
            self.assertEqual(sorted(p.name for p in bags.iterdir()), ["one", "two"])
            self.assertFalse((bags.parent / "x").exists())
            self.nothing_kept(d)

    def test_P25_not_utf8(self):
        """A path holding 0xff, then a lone surrogate's three bytes, to
        resolve, replace and a Staged's open: path each time, nothing
        written."""
        self.needs("files")
        root = Path(self.files_root())
        inside = root / "staged"
        inside.mkdir()
        before = sorted(p.name for p in root.iterdir())
        prefix = os.fsencode(str(root)) + os.sep.encode()
        with self.drive() as d:
            for bad in NOT_UTF8:
                with self.subTest(bytes=bad.hex()):
                    path = (prefix + b"a" + bad + b".txt").hex()
                    self.assert_platform_error(self.failure(d.call, "resolve", path_hex=path), "path")
                    self.assert_platform_error(self.failure(d.call, "replace", path_hex=path, data="00"), "path")
            self.assertEqual(sorted(p.name for p in root.iterdir()), before)
            if not self.STAGE:
                self.stage_unsupported(d)
            for bad in NOT_UTF8:
                with self.subTest(bytes=bad.hex(), member="stage"):
                    staged = d.call("stage", path=str(inside))["id"]
                    error = self.failure(d.call, "staged_open", id=staged, path_hex=(b"a" + bad).hex())
                    self.assert_platform_error(error, "path")
                    self.assertIsNone(d.call("discard", id=staged))
            self.nothing_kept(d)
        self.assertEqual(sorted(p.name for p in root.iterdir()), before)
        self.assertEqual(list(inside.iterdir()), [])
