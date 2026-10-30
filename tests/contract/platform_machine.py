"""The platform's machine cases P17, P18, P21 and P22, each defined once
over an adapter (contract/platform_cases.py names the binding's part),
beside P26 (contract/platform_rest.py): this machine's facts and code
page, its residual rows, the program search and the guard's path forms,
each value as measured here.

Each needs machine, which the fake lacks: on the fake each is not
applicable. On the Windows adapter facts and synced are its module
win/facts', program its module win/process', and residual its own member,
each in package 5; until each lands a binding with FACTS, PROGRAM or
RESIDUAL false checks that the member answers unsupported and skips. P22's
guard runs through location's init as the binding runs it (contract/
platform_location.py); its synced forms read this machine's OneDrive
first, each skipping by its reason where it has none. skip_reasons() runs a
binding's machine cases and gives each one's skip reason: the bindings'
own checks pin them.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.drive.session import environment

CASES = ("test_P17_facts_here", "test_P18_residual_here", "test_P21_program", "test_P22_forms_here")

# P17: each fixed drive's facts here, and the code page.
FACTS_HERE = {"protection": ("unknown", "admin_only"), "file_history": ("off", None), "bitlocker": ("off", None)}
CODE_PAGE = "cp936"
# P18: each row's name and applies here, in the rows' order.
RESIDUAL_HERE = (("restore_points", "unknown"), ("file_history", "no"), ("pagefile", "yes"),
                 ("hibernation", "no"), ("crash_dumps", "yes"), ("encryption", "yes"))
# P22: the variables naming the synced folder, dropped for its last form.
ONEDRIVE = ("OneDrive", "OneDriveConsumer", "OneDriveCommercial")
NO_ONEDRIVE = "not applicable: no synced folder here: none of the OneDrive variables set"
DROPPED_NONE = "not applicable: with the OneDrive variables dropped, synced answers none for the folder here"


def logical_drives():
    """This machine's drive letters, each with its type (3: fixed)."""
    import ctypes

    kernel32 = ctypes.WinDLL("kernel32")
    kernel32.GetDriveTypeW.argtypes = (ctypes.c_wchar_p,)
    mask = kernel32.GetLogicalDrives()
    letters = [chr(ord("A") + i) for i in range(26) if mask >> i & 1]
    return {letter: kernel32.GetDriveTypeW(f"{letter}:\\") for letter in letters}


def skip_reasons(binding, names=CASES):
    """Each named case of the binding run alone: its skip reason, or None
    where it did not skip, with its failures and errors."""
    result = unittest.TestResult()
    unittest.TestSuite(binding(name) for name in names).run(result)
    reasons = {case.id().rsplit(".", 1)[1]: reason for case, reason in result.skipped}
    others = [text for _, text in result.failures + result.errors]
    return {name: reasons.get(name) for name in names}, others


class Waiting:
    FACTS = True
    PROGRAM = True
    RESIDUAL = True

    def waiting(self, ready, what, op, **args):
        """Until the member lands (ready false): op answers unsupported, and
        the case skips."""
        if ready:
            return
        with self.drive() as d:
            self.assert_platform_error(self.failure(d.call, op, **args), "unsupported")
        self.skipTest(f"until package 5: {what}")


class PlatformMachine(Waiting):
    def test_P17_facts_here(self):
        """code_page(); facts of a temporary directory on each fixed drive:
        protection unknown (admin_only), File History and BitLocker off."""
        self.needs("machine")
        with self.drive() as d:
            self.assertEqual(d.code_page(), CODE_PAGE)
        self.waiting(self.FACTS, "facts: win/facts", "facts", path=str(self.scratch))
        fixed = [letter for letter, kind in logical_drives().items() if kind == 3]
        self.assertIn("C", fixed)
        with self.drive() as d:
            for letter in fixed:
                with self.subTest(drive=letter):
                    with tempfile.TemporaryDirectory(prefix="qiven3-p17-", dir=f"{letter}:\\") as at:
                        facts = d.call("facts", path=at)
                    self.assertEqual(facts["volume"], f"{letter}:")
                    for key, (state, reason) in FACTS_HERE.items():
                        self.assertEqual(facts[key]["state"], state, facts)
                        if reason is not None:
                            self.assertEqual(facts[key]["reason"], reason, facts)

    def test_P18_residual_here(self):
        """residual of the store's directory and of C:\\: the rows as measured
        here, each once, each with what is kept, how long and what the owner
        can do."""
        self.needs("machine")
        store = self.files_root() / "store"
        store.mkdir()
        self.waiting(self.RESIDUAL, "residual", "residual", paths=[str(store)])
        with self.drive() as d:
            rows = d.call("residual", paths=[str(store), "C:\\"])
        self.assertEqual(tuple((row["name"], row["applies"]) for row in rows), RESIDUAL_HERE, rows)
        for row in rows:
            with self.subTest(row=row["name"]):
                self.assertTrue(all(row[key] for key in ("kept", "how_long", "owner_can")), row)

    def test_P21_program(self):
        """program("qiven3-planted") with a qiven3-planted.exe in the working
        directory, then with "." leading PATH: the absolute path on PATH
        twice; a name on no entry: start."""
        self.needs("machine")
        self.waiting(self.PROGRAM, "program: win/process", "program", name="qiven3-planted")
        here, on_path = self.scratch / "here", self.scratch / "on-path"
        for directory in (here, on_path):
            directory.mkdir()
            (directory / "qiven3-planted.exe").write_bytes(b"")
        rest = os.environ.get("PATH", "")
        for path in (f"{on_path};{rest}", f".;{on_path};{rest}"):
            with self.subTest(PATH=path.split(";", 1)[0]), self.drive(env=environment(PATH=path), cwd=here) as d:
                self.assertEqual(d.call("program", name="qiven3-planted"), str(on_path / "qiven3-planted.exe"))
        with self.drive() as d:
            self.assert_platform_error(self.failure(d.call, "program", name="qiven3-on-no-entry"), "start")


class PlatformForms(Waiting):
    """P22, apart: on the system's adapter it runs the binary."""

    def guarded(self, d, given, env=None):
        """init through the guard, accepted, with the case's configuration
        directory, under env as d's: its exit status and output."""
        return self.command(d, "init", given, "--accept", "--config", self.scratch / "config", env=env)[:2]

    def test_P22_forms_here(self):
        """The guard through a junction, a symlink (where the token may make
        one) and a subst drive into a checkout: checkout three times; a
        \\\\.\\ form of a checkout: checkout, the form resolved; the
        \\\\?\\ and \\\\localhost\\C$ forms of a synced folder: path twice;
        the synced folder with the OneDrive variables dropped: synced.
        Nothing recorded, no file made. The synced forms read the machine
        first: with no OneDrive variable set, or synced answering none for
        the folder once they are dropped, each skips by its reason."""
        self.needs("machine")
        self.waiting(self.FACTS, "synced: win/facts", "synced", path=str(self.scratch))
        given_cloud = next((os.environ[n] for n in ONEDRIVE if os.environ.get(n)), None)
        cloud = Path(os.path.realpath(given_cloud)) if given_cloud else None
        repo = self.scratch / "repo"
        (repo / ".git").mkdir(parents=True)
        repo = Path(os.path.realpath(repo))
        config = self.scratch / "config"
        record = config / "location.json"

        def refused(d, given, resolved, detail, env=None):
            code, out = self.guarded(d, given, env)
            self.assertEqual((code, out), (1, f"StoreError location: guard {resolved}; {detail}\n"))
            self.assertFalse(Path(resolved).exists())
            self.assertFalse(record.exists())

        import _winapi

        junction = self.scratch / "to-repo-junction"
        _winapi.CreateJunction(str(repo), str(junction))
        letter = next(x for x in "ZYXWVUTSRQ" if x not in logical_drives())
        subprocess.run(["subst", f"{letter}:", str(repo)], check=True, capture_output=True)
        self.addCleanup(subprocess.run, ["subst", f"{letter}:", "/D"], check=False, capture_output=True)
        with self.drive(config=str(config)) as d:
            for name, given in (("junction", junction / "s"), ("subst", f"{letter}:\\s")):
                with self.subTest(form=name):
                    refused(d, given, repo / "s", "checkout")
            with self.subTest(form="symlink"):
                link = self.scratch / "to-repo-link"
                try:
                    os.symlink(repo, link, target_is_directory=True)
                except OSError as e:
                    self.skipTest(f"not applicable: this token makes no symlink: {e}")
                refused(d, link / "s", repo / "s", "checkout")
        with self.drive(config=str(config)) as d:
            with self.subTest(form="\\\\.\\"):
                refused(d, f"\\\\.\\{repo}\\s", repo / "s", "checkout")
            with self.subTest(form="synced"):
                if cloud is None:
                    self.skipTest(NO_ONEDRIVE)
                unc = f"\\\\localhost\\{str(cloud)[0]}$\\{str(cloud)[3:]}\\s"
                for name, given in (("\\\\?\\", f"\\\\?\\{cloud}\\s"), ("\\\\localhost\\C$", unc)):
                    with self.subTest(form=name):
                        code, out = self.guarded(d, given)
                        self.assertEqual(code, 1, out)
                        self.assertTrue(out.startswith("PlatformError path: "), out)
                        self.assertFalse(record.exists())
        if cloud is None:
            return
        # By name in any case: a copy of os.environ on Windows holds each
        # name upper-cased, so environment(OneDrive=None) drops none.
        upper = {n.upper() for n in ONEDRIVE}
        dropped = {name: value for name, value in os.environ.items() if name.upper() not in upper}
        with self.drive(config=str(config), env=dropped) as d, self.subTest(form="variables dropped"):
            if d.call("synced", path=str(cloud)) is None:
                self.skipTest(DROPPED_NONE)
            if not (cloud / "s").exists():
                # A guard that failed to refuse would make it in the real folder.
                self.addCleanup(shutil.rmtree, cloud / "s", True)
            refused(d, cloud / "s", cloud / "s", "synced", dropped)

