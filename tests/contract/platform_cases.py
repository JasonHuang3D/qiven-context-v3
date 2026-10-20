"""The platform's cases P1 to P9, each defined once over an adapter.

A binding (contract/test_platform_fake.py, machine/test_platform_windows.py)
mixes these into a TestCase with its adapter: ADAPTER, the name it gives;
TAGS, what it has of files, processes and machine; drive_args(config,
seed), the drive's arguments for it; and default_config(), where its
configuration location lies with no override. A case needing a tag the
adapter lacks is not applicable, a skip with the reason.

Here: P1 and P2. P3 to P9 come with the platform's files.
"""

import shutil
import sys
import tempfile
from pathlib import Path

from tests.drive.session import Drive, DriveError, environment

SYSTEMS = {"win32": "windows", "linux": "linux", "darwin": "macos"}


class PlatformBinding:
    ADAPTER = None
    TAGS = frozenset()

    def setUp(self):
        super().setUp()
        self.scratch = Path(tempfile.mkdtemp(prefix="qiven3-p-"))
        self.addCleanup(shutil.rmtree, self.scratch, True)

    def needs(self, *tags):
        lacking = [tag for tag in tags if tag not in self.TAGS]
        if lacking:
            self.skipTest(f"not applicable: {', '.join(lacking)}: the {self.ADAPTER} adapter lacks it")

    def drive(self, config=None, seed=None, env=None):
        return Drive(*self.drive_args(config, seed), env=env)

    def drive_args(self, config=None, seed=None):
        raise NotImplementedError

    def default_config(self):
        raise NotImplementedError

    def failure(self, call, *args, **kwargs):
        """The DriveError the call raises; a failure if it raises none."""
        with self.assertRaises(DriveError) as caught:
            call(*args, **kwargs)
        return caught.exception

    def assert_platform_error(self, error, code):
        self.assertEqual((error.kind, error.code), ("platform", code), error)


class PlatformCases:
    def test_P1_unsupported(self):
        """adapter_for("linux"), and the call current() makes: unsupported,
        naming the system."""
        with self.drive() as d:
            error = self.failure(d.call, "adapter_for", system="linux")
            self.assert_platform_error(error, "unsupported")
            self.assertIn("linux", error.detail)
            system = SYSTEMS.get(sys.platform, "unknown")
            if system == "windows":
                self.assertEqual(d.call("current"), "windows")
                self.assertEqual(d.call("adapter_for", system="windows"), "windows")
            else:
                error = self.failure(d.call, "current")
                self.assert_platform_error(error, "unsupported")
                self.assertIn(system, error.detail)
            self.assertEqual(d.call("name"), self.ADAPTER)

    def test_P2_configuration(self):
        """The override; the default location under LOCALAPPDATA moved, unset
        and relative; config_dir(create=true) missing, present, a file in
        its place, the override's parent missing."""
        override = str(self.scratch / "over" / "qiven3")
        (self.scratch / "over").mkdir()
        with self.drive(config=override) as d:
            self.assertEqual(d.config_dir(), override)
            self.assertFalse(Path(override).exists())

        expected = self.default_config()
        self.assertTrue(Path(expected).is_absolute(), expected)
        self.assertEqual(Path(expected).name, "qiven3")
        elsewhere = str(self.scratch / "elsewhere")
        for value in (elsewhere, None, "relative"):
            with self.subTest(LOCALAPPDATA=value):
                with self.drive(env=environment(LOCALAPPDATA=value)) as d:
                    self.assertEqual(d.config_dir(), expected)
        self.assertFalse(Path(elsewhere).exists())

        with self.drive(config=override) as d:
            self.assertEqual(d.config_dir(create=True), override)
            self.assertTrue(Path(override).is_dir())
            kept = Path(override) / "kept"
            kept.write_bytes(b"kept")
            self.assertEqual(d.config_dir(create=True), override)
            self.assertEqual(kept.read_bytes(), b"kept")
            shutil.rmtree(override)
            Path(override).write_bytes(b"a file")
            self.assert_platform_error(self.failure(d.config_dir, create=True), "exists")
            self.assertEqual(Path(override).read_bytes(), b"a file")

        orphan = self.scratch / "missing" / "qiven3"
        with self.drive(config=str(orphan)) as d:
            self.assert_platform_error(self.failure(d.config_dir, create=True), "io")
        self.assertFalse(orphan.parent.exists())
