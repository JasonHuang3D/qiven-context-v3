"""The loop's tools run git under their own environment, never the host's
config (tools/loop_run.py's git_env, 1016), shown in two parts. On the
fake git (tests/drive/fake_git.py), under each hostile host setting's
inherited environment, every git start each tool makes (the selection's
blob ids, tools/loop_select.py; the snapshot of a clean and of a dirty
working tree and the clone's sync, tools/loop_linux.py), recorded at git's
door, runs under the environment git_env gives under that inheritance,
a snapshot's own GIT_INDEX_FILE beside it: the same starts, with the same
results, as under a clean config. On this system's git, in one scratch
repository, that environment leaves each hostile setting unread: git's
config as git lists it with its scopes is the clean config's, a global
excludes file ignores nothing and a global attributes file names no
filter, where the host's environment alone shows each. The hostile
settings, made as text under a scratch directory, git pointed at them by
the home directories and the system config's variable: signing by a
program that fails, hooks that fail, core.autocrlf true (system and
global), core.safecrlf true, an fsmonitor that fails, the file protocol
refused, a global excludes file naming an untracked file, a global
attributes file and its filter that fails; and an inherited GIT_DIR,
GIT_WORK_TREE or GIT_INDEX_FILE pointing elsewhere. And git starts no
background work under that environment: the settings against it are in
its command scope and in force over a repository's config that says the
opposite. The tools' results on git itself, line endings included:
tests/drive/test_loop.py's test_sync."""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.drive.own_git import FAKE, git, own_git
from tests.drive.test_loop_ci import load_loop
from tools import loop_git

LOOP = load_loop()
BLOBS = LOOP.SELECT["blobs"]
LINUX = LOOP.LINUX["inside"].__globals__
DOORS = (BLOBS.__globals__, LOOP.RUN["git_capture"].__globals__)  # where the tools find git's door
git_env, LOOP_WHO = LOOP.RUN["git_env"], LOOP.RUN["LOOP_WHO"]
# The scratch repository's committed files and the working tree's change.
COMMITTED = {"a.txt": b"one\n", "tests/fixtures/entries.json": b"[]\r\n", "run.bat": b"@echo off\r\n"}
CHANGED = {"a.txt": b"two\r\n", "tests/fixtures/entries.json": b"[1]\r\n", "new.txt": b"new\n"}
HOOKS = ("post-checkout", "reference-transaction", "pre-commit", "post-commit", "post-index-change")
CONFIG = ("signing", "hooks", "autocrlf", "safecrlf", "fsmonitor", "file protocol", "excludes", "filter")
SETTINGS = (*CONFIG, "GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE")
# Each tool's git commands, in order; the sync's fetch, checkout and clean
# answered at the door, git's own (test_sync).
COMMANDS = {"blobs": ["ls-files", "ls-files", "hash-object"],
            "clean snapshot": ["rev-parse", "rev-parse", "add", "write-tree", "rev-parse"],
            "snapshot": ["rev-parse", "rev-parse", "add", "write-tree", "rev-parse", "commit-tree"],
            "sync": ["init", "fetch", "checkout", "clean"]}
GITS_OWN = ("fetch", "checkout", "clean")
OWN_INDEX = {"GIT_INDEX_FILE": (str(Path("TOP") / "index"), None)}  # a snapshot's own, beside git_env's
FAILED = "failed: "  # a tool's exception, as computed
# No background work (git_env): what git would otherwise start after a
# command and leave writing in the repository, and its value in force.
QUIET = {"maintenance.auto": "false", "maintenance.autoDetach": "false", "gc.auto": "0", "gc.autoDetach": "false"}


def ini(settings):
    """git's config text of {key: value}, each key section.name or
    section.subsection.name."""
    lines = []
    for key, value in settings.items():
        section, _, name = key.rpartition(".")
        head, _, sub = section.partition(".")
        lines += [f'[{head} "{sub}"]' if sub else f"[{head}]", f'\t{name} = "{value}"']
    return "".join(line + "\n" for line in lines).encode("utf-8")


def hostile(top, names):
    """(the environment of a host whose git config under top turns each
    setting of names against git, the lines git's config list shows of it
    under that environment alone): the inherited environment less every
    GIT_ variable, its home directories under top, git's system config a
    file there; then the settings."""
    home, fails = top / "home", (top / "no-such-program").as_posix()
    (home / ".config/git").mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(HOME=str(home), USERPROFILE=str(home), XDG_CONFIG_HOME=str(home / ".config"),
               GIT_CONFIG_SYSTEM=str(top / "system"))
    config, system, elsewhere = {}, {}, top / "elsewhere"
    for name in names:
        if name == "signing":
            config.update({"commit.gpgsign": "true", "tag.gpgsign": "true", "gpg.program": fails})
        elif name == "hooks":
            (top / "hooks").mkdir()
            for hook in HOOKS:
                (top / "hooks" / hook).write_bytes(b"#!/bin/sh\nexit 1\n")
                (top / "hooks" / hook).chmod(0o755)
            config["core.hooksPath"] = (top / "hooks").as_posix()
        elif name == "autocrlf":
            config["core.autocrlf"] = system["core.autocrlf"] = "true"
        elif name == "safecrlf":
            config.update({"core.autocrlf": "true", "core.safecrlf": "true"})
        elif name == "fsmonitor":
            config["core.fsmonitor"] = fails
        elif name == "file protocol":
            config["protocol.file.allow"] = "never"
        elif name == "excludes":
            (home / ".config/git/ignore").write_bytes(b"new.txt\n")
        elif name == "filter":
            (home / ".config/git/attributes").write_bytes(b"* filter=hostile\n")
            config.update({"filter.hostile.clean": fails, "filter.hostile.smudge": fails,
                           "filter.hostile.required": "true"})
        else:  # GIT_DIR, GIT_WORK_TREE or GIT_INDEX_FILE, at another repository
            env[name] = str({"GIT_DIR": elsewhere / ".git", "GIT_WORK_TREE": elsewhere,
                             "GIT_INDEX_FILE": elsewhere / ".git" / "index"}[name])
    (home / ".gitconfig").write_bytes(ini(config))
    (top / "system").write_bytes(ini(system))
    scopes = (("global", config), ("system", system))
    return env, [f"{scope}\t{k.lower()}={v}" for scope, settings in scopes for k, v in settings.items()]


def against(env, own):
    """env's differences from own, git_env's environment under the same
    inheritance: {key: (env's value, own's)}, the scratch directory each
    names its git files in read as TOP."""
    def read(e):
        top = e.get("GIT_CONFIG_GLOBAL")
        top = Path(top).parent if top else None
        return {k: v.replace(str(top), "TOP").replace(top.as_posix(), "TOP") if top else v for k, v in e.items()}

    a, b = read(env), read(own)
    return {k: (a.get(k), b.get(k)) for k in sorted(a.keys() | b.keys()) if a.get(k) != b.get(k)}


def write(root, files):
    for rel, data in files.items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_bytes(data)


class Tools(unittest.TestCase):
    """Each tool's git starts on the fake, recorded at the door, under a
    hostile host config against a clean one."""

    ctest_labels = ("fake",)

    def setUp(self):
        self.top = Path(tempfile.mkdtemp(prefix="qiven3-loop-git-"))
        self.addCleanup(shutil.rmtree, self.top, ignore_errors=True)
        (self.top / "own").mkdir()
        own_git(self, self.top / "own", fake=True)  # the clean config: the case's own
        self.clean, self.dirty = self.top / "clean", self.top / "dirty"
        write(self.clean, COMMITTED)
        for argv in (("init", "-q"), ("add", "-A"), ("commit", "-q", "-m", "a commit")):
            git(self.clean, *argv)
        shutil.copytree(self.clean, self.dirty)
        write(self.dirty, CHANGED)
        self.head = git(self.clean, "rev-parse", "HEAD")

    def starts(self, clone):
        """Under os.environ: what each tool gives (the blob ids, the clean
        and the dirty snapshot, the sync of the dirty one into clone; a
        tool's exception as FAILED and its text), each git start it makes:
        (the tool, git's argv, against() of its environment), and the GIT_
        variables of git_env's environment, an inherited one never among
        them."""
        own = git_env(Path(tempfile.mkdtemp(dir=self.top)), *LOOP_WHO)
        made, out, tool = [], {}, [None]

        def door(git, argv, cwd=None, env=None, data=None, timeout=300, check=False):
            made.append((tool[0], tuple(argv), against(os.environ if env is None else env, own)))
            if git != FAKE:
                raise AssertionError(f"a git not the fake: {git}")
            words = list(argv)
            while words[:1] == ["-c"]:
                words = words[2:]
            if words[0] in GITS_OWN:
                return subprocess.CompletedProcess(argv, 0, b"", b"")
            return loop_git.run(git, argv, cwd=cwd, env=env, data=data, timeout=timeout, check=check)

        sync = LINUX["sync"]
        with mock.patch.dict(DOORS[0], git_run=door), mock.patch.dict(DOORS[1], git_run=door):
            for key, act in (("blobs", lambda: BLOBS(self.dirty, loop_git.which())),
                             ("clean snapshot", lambda: LINUX["snapshot"](self.clean)),
                             ("snapshot", lambda: LINUX["snapshot"](self.dirty)),
                             ("sync", lambda: sync(loop_git.which(), clone, str(self.dirty), out["snapshot"]))):
                tool[0] = key
                try:
                    out[key] = act()
                except Exception as e:  # the setting's effect, compared with the clean config's
                    out[key] = f"{FAILED}{type(e).__name__}: {e}"
        return out, made, {k: v for k, (v, _) in against(own, {}).items() if k.upper().startswith("GIT_")}

    def test_hostile_config(self):
        """Each hostile setting leaves every tool's git starts and results
        as the clean config's, each start under git_env's environment."""
        expected = self.starts(self.top / "clones" / "clean")
        out, made, _ = expected
        self.assertEqual(out["clean snapshot"], self.head)
        self.assertEqual(sorted(out["blobs"]), sorted({*COMMITTED, *CHANGED}))
        self.assertRegex(out["snapshot"], r"^[0-9a-f]{40}$")
        self.assertNotEqual(out["snapshot"], self.head)
        self.assertIsNone(out["sync"])
        self.assertEqual({key: [argv[0] if argv[0] != "-c" else argv[4] for tool, argv, _ in made if tool == key]
                          for key in COMMANDS}, COMMANDS)
        for tool, argv, diff in made:
            own_index = tool.endswith("snapshot") and argv[0] in ("add", "write-tree")
            self.assertEqual(diff, OWN_INDEX if own_index else {}, (tool, argv))
        for i, name in enumerate(SETTINGS):
            with self.subTest(setting=name):
                (self.top / str(i)).mkdir()
                env, _ = hostile(self.top / str(i), (name,))
                with mock.patch.dict(os.environ, env, clear=True):
                    got = self.starts(self.top / "clones" / str(i))
                self.assertEqual(got, expected)


class Git(unittest.TestCase):
    """git_env's environment on this system's git, in one scratch
    repository."""

    ctest_labels = ("fake",)
    ctest_git = True  # starts git: listed on the loop's build alone (foundation/devkit/test/runner/list_tests.py)

    def setUp(self):
        self.top = Path(tempfile.mkdtemp(prefix="qiven3-loop-git-"))
        self.addCleanup(shutil.rmtree, self.top, ignore_errors=True)
        (self.top / "own").mkdir()
        own_git(self, self.top / "own")
        if not loop_git.which():
            self.skipTest("git is missing from PATH")
        self.repo, self.scratch = self.top / "repo", self.top / "env"
        self.scratch.mkdir()
        git(self.top, "init", "-q", str(self.repo))

    def ask(self, env, *argv):
        """(git's status, its output) in the repository under env; its
        errors apart (under the host's environment alone, the fsmonitor's
        failure to start)."""
        done = loop_git.run(loop_git.which(), argv, cwd=self.repo, env=env, timeout=120)
        return done.returncode, done.stdout.decode("utf-8", "replace").strip()

    def test_hostile_config(self):
        """Under git_env over the hostile settings' inherited environment,
        git lists the clean config's config, ignores no file the host's
        excludes name and finds no filter the host's attributes set; under
        that environment alone it shows each."""
        (self.top / "host").mkdir()
        inherited, shown = hostile(self.top / "host", CONFIG)
        clean = git_env(self.scratch, *LOOP_WHO)  # under the case's own environment
        with mock.patch.dict(os.environ, inherited, clear=True):
            own = git_env(self.scratch, *LOOP_WHO)
        listing = ("config", "--list", "--show-scope")
        ignore, attr = ("check-ignore", "--no-index", "new.txt"), ("check-attr", "filter", "--", "a.txt")
        status, plain = self.ask(inherited, *listing)
        self.assertEqual(status, 0, plain)
        self.assertEqual([x for x in shown if x not in plain.splitlines()], [], plain)
        self.assertEqual((self.ask(inherited, *ignore), self.ask(inherited, *attr)),
                         ((0, "new.txt"), (0, "a.txt: filter: hostile")))
        self.assertEqual(self.ask(own, *listing), self.ask(clean, *listing))
        self.assertEqual((self.ask(own, *ignore), self.ask(own, *attr)),
                         ((1, ""), (0, "a.txt: filter: unspecified")))

    def test_no_background_work(self):
        """git's environment holds each setting against background work in
        its command scope, and git reads each in force there, after a
        repository's config that says the opposite."""
        scoped = {os.environ[f"GIT_CONFIG_KEY_{i}"]: os.environ[f"GIT_CONFIG_VALUE_{i}"]
                  for i in range(int(os.environ.get("GIT_CONFIG_COUNT", "0")))}
        self.assertEqual({key: scoped.get(key) for key in QUIET}, QUIET)
        opposite = {key: "1" if value == "0" else "true" for key, value in QUIET.items()}
        with (self.repo / ".git" / "config").open("ab") as f:
            f.write(ini(opposite))
        status, text = self.ask(None, "config", "--show-scope", "--get-regexp", r"^(maintenance|gc)\.")
        self.assertEqual(status, 0, text)
        read = {}
        for line in text.splitlines():
            scope, _, rest = line.partition("\t")
            key, _, value = rest.partition(" ")
            read.setdefault(key, []).append((scope, value))
        self.assertEqual({key: read.get(key.lower()) for key in QUIET},
                         {key: [("local", opposite[key]), ("command", value)] for key, value in QUIET.items()})


if __name__ == "__main__":
    unittest.main()
