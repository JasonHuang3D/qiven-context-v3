"""The test gate's command line, by the test framework's design: it runs a gate of tools/gates.json, each of its
steps in order, a step a configuration's build, its selection and its ctest,
by the module foundation/devkit/test/gate (its README holds the method;
gates.py reads this file), and prints one answer, a JSON envelope on one
line. A step with a reach runs only where a file of its reach changed since
its configuration's last run, or where an ask names a test its store's
records show it lists; the answer names each step its reach left out. A
red's ask names the gate that owns its configuration, gates.json's owners
(commit the daily tree's and the release tree's).

    py -3.15 -I tools/gate.py <gate> [--again <what>]... [--plan]
    py -3.15 -I tools/gate.py --help

<what> is test:<name>, module:<name> or red, repeatable: it adds to the
selection and narrows nothing. --plan builds and selects, runs no test, and
answers the selection's count and its expected time. Exit 0 for an answer
whose status is ok, 1 for any other, 2 for a usage error, its usage on
standard error and no answer. QIVEN_VCVARS names vcvars64.bat where it is
not found. QIVEN_CDB names the pinned cdb, a tool as vcvars is: unset, or
naming a file off its pin, each step answers TOOL_MISSING, no test run.
"""

import argparse
import os
import sys
from pathlib import Path

# Under -I the checkout is off sys.path; appended, so that the standard library still wins.
sys.path.append(str(Path(__file__).resolve().parents[1]))
from foundation.devkit.python import git
from foundation.devkit.python import system
from foundation.devkit.python import table as module_table
from foundation.devkit.python.build_tree import ROOT
from foundation.devkit.test.gate import envelope, gates, key, run, selection, store

GATES = ROOT / "tools" / "gates.json"


def _faulted(words):
    """The answer of a gate that could not be read: INTERNAL, its reason."""
    spelled = store.spelled(store.run_id())
    line = envelope.bounded_line("INTERNAL", envelope.action("INTERNAL", words), spelled,
                                 f"runtime://operation/{spelled}", {"reason": words, "steps": []}, [], {})
    return line, envelope.exit_status("INTERNAL")


class _Tools:
    """The tools found once a run, Missing kept for every step that asks."""

    def __init__(self, version):
        self.version, self.found, self.missing = version, None, None

    def __call__(self):
        if self.found is None and self.missing is None:
            try:
                self.found = run.find_tools(self.version, os.environ.get("QIVEN_VCVARS"))
            except run.Missing as missing:
                self.missing = missing
        if self.missing is not None:
            raise self.missing
        return run.Runner(self.found)


def _parser(names):
    parser = argparse.ArgumentParser(prog="py -3.15 -I tools/gate.py", description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("gate", choices=sorted(names) or None, help="the gate of tools/gates.json to run")
    parser.add_argument("--again", action="append", default=[], metavar="WHAT", type=selection.ask,
                        help="test:<name>, module:<name> or red: run them again; repeatable")
    parser.add_argument("--plan", action="store_true", help="build and select, run no test")
    return parser


def main(argv):
    try:
        data, fault = gates.load(GATES), None
    except gates.BadGates as bad:
        data, fault = {"gates": {}}, str(bad)
    args = _parser(data["gates"]).parse_args(argv)  # a usage error: its usage on stderr, exit 2
    if fault:
        line, status = _faulted(fault)
        print(line)
        return status
    system.kill_on_close()  # before any start of vcvars, cmake, ninja or ctest: each dies with this run
    name, owners = args.gate, gates.owners_of(data)
    steps, run_id = gates.steps_of(data["gates"][name], ROOT), store.run_id()
    tools, known = _Tools(data["toolset"]), run.presets(ROOT)
    files = trees = None
    if any(reach for _, reach in steps):
        try:
            table = module_table.load(ROOT)
            files, trees = key.files(ROOT, table), table["trees"]
        except (OSError, ValueError, git.GitFailed):
            files = None  # every reach step runs, and its own step answers the table or git
    answers, left = [], {}
    for one, reach in steps:
        why = (gates.out_of_reach(ROOT, one, reach, files, trees, args.again)
               if reach and files is not None else None)
        if why:
            left[one.preset] = why
            continue
        answers.append(run.step(ROOT, one, tools, run=run_id, gate=name, argv=sys.argv, asks=args.again,
                                plan=args.plan, owners=owners, known=known, alone=len(steps) == 1))
    line, status = envelope.answer(run_id, answers, reach=left)
    print(line)
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
