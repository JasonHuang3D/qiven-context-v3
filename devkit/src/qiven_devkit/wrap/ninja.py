"""Ninja's wrapper: a tree's targets built, and Ninja's report of what ran read
in one place, "no work to do" read as nothing; its fake. See this
directory's README.md.
"""

import os
import re
from collections import namedtuple

from qiven_devkit.wrap import call
from qiven_devkit.wrap.fake import Fake

BUILD = "ninja build"
_STATUS = "@ninja-step %f/%t@ "  # NINJA_STATUS: the form of each step's line
_STEP = re.compile(rb"@ninja-step \d+/\d+@ ")
_NO_WORK = b"ninja: no work to do."
_FAILED = b"FAILED: "


class Built(namedtuple("Built", ("steps", "nothing", "said"))):
    """What a build ran: `steps`, each step's name in Ninja's order; `nothing`,
    Ninja's word that no target asked needed a step; `said`, its other lines."""

    __slots__ = ()


class BuildFailed(call.CallError):
    """A step of the build failed: `built`, what ran; `failed`, each failed
    step's line after "FAILED: "."""

    answer = "build failed"

    def __init__(self, call_name, reason, built, failed):
        super().__init__(call_name, reason)
        self.built, self.failed = built, failed


class Ninja:
    """Ninja, as `program` names it, run under `env` with `limit` seconds a call."""

    def __init__(self, program, env, limit):
        self.program, self.env, self.limit = program, env, limit

    def build(self, tree, targets=()):
        """Built of a build of targets (Ninja's default where none) in tree;
        BuildFailed where a step failed."""
        if not os.path.isabs(os.fspath(tree)):
            raise ValueError(f"{BUILD}: tree must be an absolute path, not {str(tree)!r}")
        targets = [str(target) for target in targets]
        env = {key: value for key, value in self.env.items() if key.upper() != "NINJA_STATUS"}
        env["NINJA_STATUS"] = _STATUS
        answer = call.call(BUILD, self.program, targets, env=env, cwd=tree, limit=self.limit, takes=(0, 1))
        built, failed = _report(answer)
        if answer.status and failed:
            raise BuildFailed(BUILD, f"{len(failed)} step(s) failed in {tree}: " + " | ".join(failed),
                              built, failed)
        if answer.status:
            raise call.Failed(BUILD, f"ninja ended with status {answer.status} in {tree}: {_last(built)}",
                              answer.status)
        if not built.steps and not built.nothing:
            raise call.Unreadable(BUILD, f"neither a step nor '{_NO_WORK.decode()}' in its answer: {_last(built)}")
        return built


class FakeNinja(Fake):
    """Ninja's fake: its method, answered by call name; no Ninja started."""

    def build(self, tree, targets=()):
        return self._answer(BUILD, tree=tree, targets=list(targets))


def _report(answer):
    """(Built, failed) of a build's answer: the one place Ninja's report is read."""
    steps, said, failed, nothing = [], [], [], False
    for line in answer.out.splitlines():
        found = _STEP.search(line)
        if found:
            if nothing:
                raise call.Unreadable(BUILD, f"a step after '{_NO_WORK.decode()}': {line[:200]!r}")
            before, name = line[:found.start()], line[found.end():]
            if before.strip():
                said.append(_text(before))
            try:
                steps.append(name.decode("utf-8"))
            except UnicodeDecodeError as error:
                raise call.Unreadable(BUILD, f"a step's name is not UTF-8: {error}: {name[:200]!r}") from None
            continue
        if line.rstrip() == _NO_WORK:
            nothing = True
        elif line.startswith(_FAILED):
            failed.append(_text(line[len(_FAILED):]).strip())
        if line.strip():
            said.append(_text(line))
    said += [_text(line) for line in answer.err.splitlines() if line.strip()]
    return Built(steps, nothing, said), failed


def _last(built):
    """The last lines Ninja wrote but its steps', for a reason."""
    return " | ".join(built.said[-20:])[-4000:] or "it wrote nothing but its steps"


def _text(line):
    """A line Ninja wrote, for a reader: UTF-8 with errors replaced."""
    return line.decode("utf-8", "replace").rstrip()
