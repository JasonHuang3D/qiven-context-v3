"""The analysis configuration against its list, analysis-exemptions.json in
the build's directory: each check off in a .clang-tidy or a rule set has an
entry with its reason, at the scope that turns it off, and each entry a
check off. A check is off by a -check in Checks, by a rule set's Action
below a warning, by a family or a rule set left out of the root's or the
rule set's own list, or by a directory's file that does not inherit its
parent's, which then starts from clang-tidy's defaults. A scope is the
root's "every file", a directory's own path, or "test code", the two files
the list's test_code names, which the build gives the targets whose own
sources are all test code, its test_code targets (the build's
test-targets.txt, held to them by targets()). Each rule set the list names
has its reason. The rule sets' own rule-off lines are read; what a tool's
command line turns off is not.
"""

import json
from xml.etree import ElementTree

from foundation.devkit.structure import subset

LIST = "foundation/devkit/build/analysis-exemptions.json"
EVERY, TEST = "every file", "test code"
ON = ("Error", "Warning", "Default")


def tidy_off(rel, text, data, add):
    """The checks a .clang-tidy turns off, each (tool, check, scope). The
    root starts from -*, and names every family the list has on."""
    entries = subset.tidy_checks(text)
    if rel == ".clang-tidy":
        scope = EVERY
        entries = entries[1:] if entries[:1] == ["-*"] else entries
        for family in data["tidy_on"]:
            if family not in entries:
                add(rel, f"{family} left out")
    else:
        scope = TEST if rel == data["test_code"]["clang-tidy"] else rel.rpartition("/")[0] + "/"
        if not subset.tidy_inherits(text):
            add(rel, "starts from clang-tidy's defaults, not its parent's checks")
    return {("clang-tidy", entry[1:], scope) for entry in entries if entry.startswith("-")}


def rule_set_off(rel, text, data, add):
    """The rules a rule set turns off, each (tool, check, scope), and its
    includes held to the list's."""
    if rel not in data["rule_sets"]:
        add(rel, "a rule set the list does not name")
        return set()
    tree = ElementTree.fromstring(text)
    included = sorted((node.get("Path"), node.get("Action")) for node in tree.iter("Include"))
    if not data["rule_sets"][rel]["reason"].strip():
        add(LIST, f"{rel}: no reason")
    wanted = sorted((path, "Default") for path in data["rule_sets"][rel]["includes"])
    for path, _ in sorted(set(wanted) - set(included)):
        add(rel, f"{path} left out")
    for path, action in sorted(set(included) - set(wanted)):
        add(rel, f"{path}, Action {action}: an include the list does not name")
    scope = TEST if rel == data["test_code"]["msvc"] else EVERY
    return {("msvc", rule.get("Id"), scope) for rule in tree.iter("Rule") if rule.get("Action") not in ON}


def findings(root, files):
    """Each (file, detail) where the configuration and the list part."""
    found = []
    add = lambda where, detail: found.append((where, detail))
    if LIST not in files:
        return [(LIST, "no list")]
    data = json.loads((root / LIST).read_text(encoding="utf-8"))
    off = {}
    for rel in files:
        reader = tidy_off if rel.endswith(".clang-tidy") else rule_set_off if rel.endswith(".ruleset") else None
        if reader:
            text = (root / rel).read_text(encoding="utf-8-sig")
            off.update({key: rel for key in reader(rel, text, data, add)})
    for rel in sorted(set(data["rule_sets"]) - set(files)):
        add(rel, "a rule set of the list that is not there")
    listed = [(entry["tool"], entry["check"], entry["scope"]) for entry in data["exemptions"]]
    for key in sorted(set(off) - set(listed)):
        add(off[key], f"{key} is off with no entry of the list")
    for key in sorted(set(listed) - set(off)):
        add(LIST, f"{key}: an entry that nothing turns off")
    for key in sorted(key for key in set(listed) if listed.count(key) > 1):
        add(LIST, f"{key}: two entries")
    for entry in data["exemptions"]:
        if not entry["reason"].strip():
            add(LIST, f"{entry['check']}: no reason")
    return sorted(found)


def targets(root, build):
    """The pin both ways in a build's tree, each finding (file, detail): a
    target the build gives test code's analysis (its test-targets.txt) that
    the list's test_code targets do not pin, a test target left out or a
    library's; and a pinned target the build defines (its
    defined-targets.txt) without that analysis, or does not define."""
    pinned = set(json.loads((root / LIST).read_text(encoding="utf-8"))["test_code"]["targets"])
    given = set((build / "test-targets.txt").read_text(encoding="utf-8").split())
    defined = set((build / "defined-targets.txt").read_text(encoding="utf-8").split())
    found = [("test-targets.txt", f"{name}: takes test code's analysis, and the list does not pin it")
             for name in sorted(given - pinned)]
    found += [("test-targets.txt", f"{name}: pinned, and {build} defines it without test code's analysis")
              for name in sorted((pinned & defined) - given)]
    found += [("defined-targets.txt", f"{name}: pinned, and {build} does not define it")
              for name in sorted(pinned - defined - given)]
    return found
