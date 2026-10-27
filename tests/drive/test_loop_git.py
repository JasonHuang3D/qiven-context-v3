"""The loop's tools run git under their own environment, never the host's
config (tools/loop_run.py's git_env, 1016): the snapshot of the working
tree and the clone's sync (tools/loop_linux.py), and the selection's blob
ids (tools/loop_select.py), on this system's git, in a scratch
repository holding the checkout's .gitattributes. Under each hostile host
config the case makes in a scratch directory, git pointed at it by the
home directories and the system config's variable: signing by a program
that fails, hooks that fail, core.autocrlf true (system and global),
core.safecrlf true over a CRLF file, an fsmonitor that fails, the file
protocol refused, a global excludes file naming an untracked file, a
global attributes file and its filter that fails, and an inherited
GIT_DIR, GIT_WORK_TREE or GIT_INDEX_FILE pointing elsewhere: each tool
succeeds, with the same blob ids, the same snapshot (its tree and parent;
HEAD itself for a clean checkout) and the same clone as under a clean
config. And git starts no background work under that environment: the
settings against it are in its command scope and in force."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.drive.own_git import own_git
from tests.drive.test_loop_ci import ROOT, load_loop

LOOP = load_loop()
BLOBS = LOOP.SELECT["blobs"]
LINUX = LOOP.LINUX["inside"].__globals__
# The scratch repository's committed files, beside the checkout's
# .gitattributes, and the working tree's change: a text file now with CRLF
# (stored with LF under eol=lf), a -text file's CRLF kept, a new file.
COMMITTED = {"a.txt": b"one\n", "tests/fixtures/entries.json": b"[]\r\n", "run.bat": b"@echo off\r\n"}
CHANGED = {"a.txt": b"two\r\n", "tests/fixtures/entries.json": b"[1]\r\n", "new.txt": b"new\n"}
HOOKS = ("post-checkout", "reference-transaction", "pre-commit", "post-commit", "post-index-change")
SETTINGS = ("signing", "hooks", "autocrlf", "safecrlf", "fsmonitor", "file protocol", "excludes", "filter",
            "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE")
FAILED = "failed: "  # a tool's exception, as computed
# No background work (git_env): what git would otherwise start after a
# command and leave writing in the repository, and its value in force.
QUIET = {"maintenance.auto": "false", "maintenance.autoDetach": "false", "gc.auto": "0", "gc.autoDetach": "false"}


def hostile(top, name):
    """The environment of a host whose git config, made under top by the
    case's clean git, turns the setting name against git: the inherited
    environment less every GIT_ variable, its home directories under top,
    git's system config a file there; then the setting."""
    home, fails = top / "home", (top / "no-such-program").as_posix()
    (home / ".config" / "git").mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(HOME=str(home), USERPROFILE=str(home), XDG_CONFIG_HOME=str(home / ".config"),
               GIT_CONFIG_SYSTEM=str(top / "system"))
    config, system = {}, {}
    if name == "signing":
        config = {"commit.gpgsign": "true", "tag.gpgsign": "true", "gpg.program": fails}
    elif name == "hooks":
        (top / "hooks").mkdir()
        for hook in HOOKS:
            (top / "hooks" / hook).write_bytes(b"#!/bin/sh\nexit 1\n")
            (top / "hooks" / hook).chmod(0o755)
        config = {"core.hooksPath": (top / "hooks").as_posix()}
    elif name == "autocrlf":
        config = system = {"core.autocrlf": "true"}
    elif name == "safecrlf":
        config = {"core.autocrlf": "true", "core.safecrlf": "true"}
    elif name == "fsmonitor":
        config = {"core.fsmonitor": fails}
    elif name == "file protocol":
        config = {"protocol.file.allow": "never"}
    elif name == "excludes":
        (home / ".config" / "git" / "ignore").write_bytes(b"new.txt\n")
    elif name == "filter":
        (home / ".config" / "git" / "attributes").write_bytes(b"* filter=hostile\n")
        config = {"filter.hostile.clean": fails, "filter.hostile.smudge": fails, "filter.hostile.required": "true"}
    else:  # GIT_DIR, GIT_WORK_TREE or GIT_INDEX_FILE, at another repository
        elsewhere = top / "elsewhere"
        subprocess.run(["git", "init", "-q", str(elsewhere)], capture_output=True, check=True, timeout=120)
        env[name] = str({"GIT_DIR": elsewhere / ".git", "GIT_WORK_TREE": elsewhere,
                         "GIT_INDEX_FILE": elsewhere / ".git" / "index"}[name])
    for path, settings in ((home / ".gitconfig", config), (top / "system", system)):
        path.write_bytes(b"")
        for key, value in settings.items():
            subprocess.run(["git", "config", "--file", str(path), key, value], capture_output=True, check=True,
                           timeout=120)
    return env


class OwnGit(unittest.TestCase):
    """Each tool's git under a hostile host config, against a clean one."""

    ctest_labels = ("fake",)

    def setUp(self):
        self.git = shutil.which("git")
        if not self.git:
            self.skipTest("git is missing from PATH")
        self.top = Path(tempfile.mkdtemp(prefix="qiven3-loop-git-"))
        self.addCleanup(shutil.rmtree, self.top, ignore_errors=True)
        (self.top / "own").mkdir()
        own_git(self, self.top / "own")  # the clean config: the case's own
        self.clean, self.dirty = self.top / "clean", self.top / "dirty"
        self.write(self.clean, {".gitattributes": (ROOT / ".gitattributes").read_bytes(), **COMMITTED})
        self.ask(self.clean, "init", "-q")
        self.ask(self.clean, "add", "-A")
        self.ask(self.clean, "commit", "-q", "-m", "a commit")
        shutil.copytree(self.clean, self.dirty)
        self.write(self.dirty, CHANGED)
        self.head = self.ask(self.clean, "rev-parse", "HEAD")

    @staticmethod
    def write(root, files):
        for rel, data in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_bytes(data)

    def ask(self, root, *argv):
        """git's answer under the case's clean config."""
        done = subprocess.run([self.git, *argv], cwd=root, stdin=subprocess.DEVNULL, capture_output=True,
                              timeout=120, check=True)
        return done.stdout.decode("utf-8").strip()

    def computed(self, clone):
        """What each tool gives under the environment os.environ holds: the
        blob ids, the clean and the dirty snapshot, and the sync of the
        dirty one into clone; a tool's exception as FAILED and its text."""
        out = {}
        for key, act in (("blobs", lambda: BLOBS(self.dirty, self.git)),
                         ("clean snapshot", lambda: LINUX["snapshot"](self.clean)),
                         ("snapshot", lambda: LINUX["snapshot"](self.dirty)),
                         ("sync", lambda: LINUX["sync"](self.git, clone, str(self.dirty), out["snapshot"]))):
            try:
                out[key] = act()
            except Exception as e:  # the setting's effect, compared with the clean config's
                out[key] = f"{FAILED}{type(e).__name__}: {e}"
        return out

    def described(self, out, clone):
        """The computed values by what they name, read under the clean
        config: a snapshot by its tree and parents, the clone by its HEAD's
        tree, its index, its status and its files' bytes."""
        snap, synced = out["snapshot"], out["sync"]
        try:
            if not snap.startswith(FAILED):
                snap = (self.ask(self.dirty, "rev-parse", snap + "^{tree}"),
                        self.ask(self.dirty, "rev-parse", snap + "^@"))
            if synced is None:
                synced = (self.ask(clone, "rev-parse", "HEAD^{tree}"), self.ask(clone, "ls-files", "-s"),
                          self.ask(clone, "status", "--porcelain", "--ignored"),
                          {p: (clone / p).read_bytes() for p in sorted({*COMMITTED, *CHANGED})})
        except (subprocess.CalledProcessError, OSError) as e:
            synced = f"{FAILED}{type(e).__name__}: {e}"
        return {"blobs": out["blobs"], "clean snapshot": out["clean snapshot"], "snapshot": snap, "sync": synced}

    def test_hostile_config(self):
        """Each hostile setting leaves every tool's result as the clean
        config's."""
        clone = self.top / "clones" / "clean"
        expected = self.described(self.computed(clone), clone)
        self.assertEqual(expected["clean snapshot"], self.head)
        self.assertIsInstance(expected["blobs"], dict)
        files = expected["sync"][3]  # a text file checked out with LF under eol=lf, a -text one as it was
        self.assertEqual((files["a.txt"], files["tests/fixtures/entries.json"]), (b"two\n", b"[1]\r\n"))
        for i, name in enumerate(SETTINGS):
            with self.subTest(setting=name):
                (self.top / str(i)).mkdir()
                with mock.patch.dict(os.environ, hostile(self.top / str(i), name), clear=True):
                    got = self.computed(self.top / "clones" / str(i))
                self.assertEqual(self.described(got, self.top / "clones" / str(i)), expected)

    def test_no_background_work(self):
        """git's environment holds each setting against background work in
        its command scope, and git reads each in force there over a
        repository's config that says the opposite."""
        scoped = {os.environ[f"GIT_CONFIG_KEY_{i}"]: os.environ[f"GIT_CONFIG_VALUE_{i}"]
                  for i in range(int(os.environ.get("GIT_CONFIG_COUNT", "0")))}
        self.assertEqual({key: scoped.get(key) for key in QUIET}, QUIET)
        for key, value in QUIET.items():
            self.ask(self.clean, "config", "--local", key, "1" if value == "0" else "true")
        self.assertEqual({key: self.ask(self.clean, "config", "--get", key) for key in QUIET}, QUIET)


if __name__ == "__main__":
    unittest.main()
