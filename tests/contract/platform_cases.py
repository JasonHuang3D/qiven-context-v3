"""The platform's drive binding, which each of its cases is defined over.

A binding (contract/test_platform_fake.py, machine/test_platform_windows.py)
mixes the cases into a TestCase with its adapter: ADAPTER, the name it gives;
DRIVE, the drive's binary; TAGS, what it has of files, processes and
machine, and "own: fake" on the fake; drive_args(config, seed), the drive's arguments for it;
files_root(), a directory its files' operations may write under;
COUNT_HANDLES, whether each drive's handle count is compared; and, for
the files' cases, hold(drive, **times), its holders
(contract/platform_files.py), and, where P20 runs timed, held(drive,
kind), a holder's own times, taken and released. A case needing a tag the adapter lacks is
not applicable, a skip with the reason.

Here: the binding. P1, current's choice, is platform's own
(foundation/system/platform/tests/current_test.cpp), as is P2, config_dir
(win_platform_test.cpp and fake_platform_test.cpp there). P3 to P8 and P16 are location's
(contract/platform_location.py), P9, P19 and P25 the files'
(contract/platform_files.py), P10 to P15 and P20 the processes'
(contract/platform_process.py), P17, P18, P21 and P22 the machine's
(contract/platform_machine.py); the fake's own cases, "own: fake",
contract/platform_own.py.
"""

import shutil
import tempfile
from pathlib import Path

from tests.drive.session import FAKE, Drive, DriveError


class PlatformBinding:
    ADAPTER = None
    DRIVE = FAKE
    TAGS = frozenset()
    # Whether the drive's handles are counted before and after (the
    # machine cases).
    COUNT_HANDLES = False

    def setUp(self):
        super().setUp()
        self.scratch = Path(tempfile.mkdtemp(prefix="qiven3-p-"))
        self.addCleanup(shutil.rmtree, self.scratch, True)

    def needs(self, *tags):
        lacking = [tag for tag in tags if tag not in self.TAGS]
        if lacking:
            self.skipTest(f"not applicable: {', '.join(lacking)}: the {self.ADAPTER} adapter lacks it")

    def drive(self, config=None, seed=None, env=None, cwd=None):
        return Drive(*self.drive_args(config, seed), drive=self.DRIVE, env=env, cwd=cwd,
                     count_handles=self.COUNT_HANDLES)

    def drive_args(self, config=None, seed=None):
        raise NotImplementedError

    def files_root(self):
        raise NotImplementedError

    def failure(self, call, *args, **kwargs):
        """The DriveError the call raises; a failure if it raises none."""
        with self.assertRaises(DriveError) as caught:
            call(*args, **kwargs)
        return caught.exception

    def assert_platform_error(self, error, code):
        self.assertEqual((error.kind, error.code), ("platform", code), error)
