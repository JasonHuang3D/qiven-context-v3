"""The checks a move must leave as they were: listed from a commit's committed
tree, and two listings compared.

loop_listing.py list CHECKOUT COMMIT SCRATCH OUT
  COMMIT's committed tree, by git archive, into SCRATCH/tree (SCRATCH new or
  empty, outside CHECKOUT), its analysis preset configured into SCRATCH/build
  by the caller's cmake and compiler, then OUT: a line for each file walked,
  analysed or given a rule set or a clang-tidy step, sources generated in the
  build tree as BUILD/<path>:
    PATH module=M layer=L scopes=S ruleset=R [external=E] tidy=C tidy_from=F
      [tidy_file=P] [unit=U unit_ruleset=R [unit_external=E] unit_tidy=C
      unit_tidy_from=F]
  S: each scope of the commit's own rules taking it (walker, walked, layered,
  test_code, library, core, subset, k15, analysed). R: each compile of it,
  by the rule sets it names (joined by |), none for a compile naming none,
  the compiles sorted and joined by ,; - for no compile. E: as R, the
  directories inside the checkout each compile takes as external
  (/external:I, -external:I, -isystem, -imsvc), each ending in /; each
  repository file a compile with /external:anglebrackets reaches by an angle
  include, or through a file so reached, walked from its source through
  every include found, active or not, by the compile's own search: /I and
  external directories in its order, a quoted include first beside its
  includer and those above it (a file in the build tree or outside the
  checkout stays out); and each
  /external:env:VAR as spelled; left out where no compile takes one. A
  header's own external read is its unit's, unit_external. C: its
  clang-tidy step's configuration, path@digest: the file the step names, or
  the nearest .clang-tidy above the source for a step naming none; for a
  file the build composes, composed@digest, P its path, and F what it was
  composed from; - for no step. F: the configurations the step rests on, as
  its build line lists them. A header the build analyses names the unit it
  compiles through, U, as the build's own qiven3_header_units reads it (by
  loop_listing.cmake; a COMMIT whose build names none is refused), with U's
  rule set and configuration. Then the build's
  header filters, as lines named cmake:filter and cmake:exclude; for each
  rule set a compile or the exemptions' data names, ruleset:PATH digest=D, D
  covering it and each rule set it includes, an include's path read as that
  include's digest, so that a rename alone keeps D (an include is read beside
  the rule set, then in the compile's /analyze:rulesetdirectory, or any
  compile's for a rule set no compile names; one unread refuses the
  listing); and the commit's analysis-exemptions.json, whole, a sorted line
  for each entry: exempt:check:TOOL:CHECK:SCOPE, exempt:target:T,
  exempt:not_defined:PRESET:T, exempt:rule_set:R includes=PATHS,
  exempt:test_code:KEY and exempt:KEY value=V, their other fields beside
  them, spaces, # and % written as %20, %23 and %25. A COMMIT without that
  file is refused: its listing could judge no compare. COMMIT's tree must be
  of this listing's time: a tree whose tests/structure/layers.py has no
  read, or whose table the structure refuses, is refused with the error.

loop_listing.py compare BEFORE AFTER [MAP [EXPECTED]]
  tools/loop_listing_compare.py: two listings compared.
"""

import functools
import hashlib
import io
import json
import os
import re
import runpy
import subprocess
import sys
import tarfile
from pathlib import Path, PurePosixPath
from xml.etree import ElementTree

HERE = Path(__file__).resolve().parent
GIT = runpy.run_path(str(HERE / "loop_git.py"))  # git's one door, by runpy: under -I this directory is off sys.path
COMPARE = runpy.run_path(str(HERE / "loop_listing_compare.py"))  # the compare, by runpy too
NONE = COMPARE["NONE"]
SCOPES = ("walker", "walked", "layered", "test_code", "library", "core", "subset", "k15")
EXTERNAL = ("/external:I", "-external:I", "-isystem", "/imsvc", "-imsvc")  # a compile's external directories
ENVIRONMENT = ("/external:env:", "-external:env:")  # each kept as spelled: what it names is the machine's
ANGLED = re.compile(r"(?:^|\s)[/-]external:anglebrackets(?=\s|$)")  # a compile taking an angle include as external
DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*(?:<([^>\n]+)>|"([^"\n]+)")', re.M)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()[:12]


def committed_tree(checkout, commit, tree):
    """The files of commit, written under tree; their paths."""
    data = GIT["run"](GIT["which"](), ["-C", str(checkout), "archive", "--format=tar", commit], check=True).stdout
    with tarfile.open(fileobj=io.BytesIO(data)) as archive:
        archive.extractall(tree, filter="data")
        return sorted(member.name for member in archive.getmembers() if member.isfile())


def rel_of(text, root, build):
    """A path the build names, absolute or from the build tree, from root;
    BUILD/<path> under the build tree."""
    path = Path(text.replace("$:", ":").replace("$ ", " ").strip('"'))
    path = (path if path.is_absolute() else build / path).resolve()
    if path.is_relative_to(build):
        return "BUILD/" + path.relative_to(build).as_posix()
    if path.is_relative_to(root):
        return path.relative_to(root).as_posix()
    return "OUTSIDE/" + path.as_posix()


def flagged(command, flags):
    """What follows each of flags in a compile's command: the rest of its word, or the next word."""
    found, each = [], iter(re.findall(r'(?:\\"|[^\s"]|"[^"]*")+', command))
    for word in each:
        word = word.replace('"', "")
        flag = next((f for f in flags if word.startswith(f)), None)
        if flag:
            found.append(word[len(flag):] or next(each, "").replace('"', ""))
    return found


def rule_files(command):
    """(the rule sets a compile names, the rule-set directories it names)."""
    named = flagged(command, ("/analyze:ruleset",))
    return ([r for r in named if not r.startswith("directory")],
            [r.removeprefix("directory") for r in named if r.startswith("directory")])


def each_compile(commands, root, build, named):
    """{path: [what named finds in each compile]} from compile_commands.json's
    text: a compile's joined by |, none for a compile naming none."""
    found = {}
    for entry in json.loads(commands):
        found.setdefault(rel_of(entry["file"], root, build), []).append("|".join(named(entry)) or "none")
    return {path: sorted(compiles) for path, compiles in found.items()}


def rulesets(commands, root, build):
    """{path: [rule sets of each compile]}."""
    return each_compile(commands, root, build, lambda entry: sorted(
        {rel_of(ruleset, root, build) for ruleset in rule_files(entry["command"])[0]}))


def angle_included(source, dirs, directives, there):
    """The files a compile of source with include directories dirs takes as
    external by angle brackets: each it reaches by an angle include, at any
    depth, and each such a file includes, by quotes too, as cl takes them,
    found as cl finds them: a quoted one beside its includer, then beside those
    above it, then in dirs; an angle one in dirs alone. A file reached either
    way counts, whichever comes first. A source the configure has not made
    reads as none."""
    found, seen = set(), set()

    def walk(path, stack, taken):
        stack = (os.path.dirname(path), *stack)
        for angle, quoted in directives(path):
            near = () if angle else stack
            paths = (os.path.normpath(os.path.join(d, angle or quoted)) for d in (*near, *dirs))
            at, under = next(filter(there, paths), None), taken or bool(angle)
            if at and under:
                found.add(at)
            if at and (at, under) not in seen:
                seen.add((at, under))
                walk(at, stack, under)
    if there(source):
        walk(source, (), False)
    return found


def externals(commands, root, build):
    """{path: [what each compile takes as external]}: its repository
    directories, each ending in /, the repository files it reaches by an angle
    include under /external:anglebrackets, and its /external:env: as spelled.
    A directory or a file outside the checkout stays out, and a file with
    none."""
    directives = functools.cache(lambda path: DIRECTIVE.findall(Path(path).read_text("utf-8", "replace")))
    there = functools.cache(os.path.isfile)

    def inside(entry):
        command = entry["command"]
        paths = [rel_of(where, root, build) + "/" for where in flagged(command, EXTERNAL)]
        if ANGLED.search(command):
            dirs = [str(build / where) for where in flagged(command, ("/I", "-I", *EXTERNAL))]
            paths += [rel_of(at, root, build) for at in angle_included(
                os.path.normpath(build / entry["file"]), dirs, directives, there)]
        spelled = [flag + name for flag in ENVIRONMENT for name in flagged(command, (flag,))]
        return sorted({coded(p) for p in (*paths, *spelled) if not p.startswith(("BUILD/", "OUTSIDE/"))})
    return {path: compiles for path, compiles in each_compile(commands, root, build, inside).items()
            if set(compiles) != {"none"}}


def ruleset_digest(path, dirs):
    """A rule set's digest: its parsed content, each include's path read as
    that include's digest. It is read at path or, by its name, from dirs; one
    that cannot be read refuses the listing."""
    path = next((p for p in (path, *(Path(d) / path.name for d in dirs)) if p.is_file()), path)
    try:
        rules = ElementTree.fromstring(path.read_bytes())
    except (OSError, ElementTree.ParseError) as error:
        raise SystemExit(f"loop_listing: a rule set cannot be read: {path}: {error}") from error
    for include in rules.iter("Include"):
        include.set("Path", ruleset_digest(path.parent / include.get("Path", ""), dirs))
    return hashlib.sha256(ElementTree.tostring(rules)).hexdigest()[:12]


def ruleset_lines(commands, root, build, listed=()):
    """A sorted line for each rule set a compile names, and each of listed, the
    repository paths the exemptions' data names, with its digest."""
    found, every = {}, {}
    for entry in json.loads(commands):
        named, dirs = rule_files(entry["command"])
        every.update(dict.fromkeys(dirs))
        for ruleset in named:
            found.setdefault(rel_of(ruleset, root, build), ruleset_digest(build / ruleset, dirs))
    for rel in listed:
        found.setdefault(rel, ruleset_digest(root / rel, every))
    return [f"ruleset:{rel} digest={value}" for rel, value in sorted(found.items())]


def coded(text):
    """text as one word of a listing's line."""
    return text.replace("%", "%25").replace(" ", "%20").replace("#", "%23")


def exempt_lines(data):
    """The exemptions' data, whole, a sorted line for each entry (see the
    module's text): an include the data lists as a repository path."""
    def rest(entry, *taken):
        return "".join(f" {k}={flat(v)}" for k, v in sorted(entry.items()) if k not in taken)

    def flat(value):
        return coded(value if isinstance(value, str) else json.dumps(value, sort_keys=True, separators=(",", ":")))

    def include(ruleset, name):
        at = (PurePosixPath(ruleset).parent / name).as_posix()
        return at if at in sets else name

    data = dict(data)
    sets, test, checks = data.pop("rule_sets"), dict(data.pop("test_code")), data.pop("exemptions")
    out = [f"exempt:check:{e['tool']}:{e['check']}:{coded(e['scope'])}{rest(e, 'tool', 'check', 'scope')}"
           for e in checks]
    out.extend(f"exempt:target:{target}" for target in test.pop("targets"))
    out.extend(f"exempt:not_defined:{e['preset']}:{e['target']}{rest(e, 'preset', 'target')}"
               for e in test.pop("not_defined"))
    out.extend(f"exempt:rule_set:{ruleset} includes={','.join(include(ruleset, i) for i in entry['includes'])}"
               f"{rest(entry, 'includes')}" for ruleset, entry in sets.items())
    out.extend(f"exempt:test_code:{key} value={flat(value)}" for key, value in test.items())
    return sorted([*out, *(f"exempt:{key} value={flat(value)}" for key, value in data.items())])


def nearest(rel, root):
    """The nearest .clang-tidy above rel, from root; - for none."""
    for parent in (root / rel).parents:
        if (parent / ".clang-tidy").is_file():
            return (parent / ".clang-tidy").relative_to(root).as_posix()
        if parent == root:
            break
    return NONE


def tidy_steps(ninja, root, build):
    """{path: (config, from, file)} for each clang-tidy step of build.ninja's text."""
    steps, inputs, command = {}, [], None
    for line in ninja.splitlines():
        if line.startswith("build "):
            inputs = line.split(": ", 1)[1].split(" || ")[0].split(" | ")[0].split()[1:] if ": " in line else []
            command = None
        elif line.startswith("  COMMAND = ") and "clang-tidy" in line:
            command = line
        elif command and line.startswith("  DESC = clang-tidy "):
            rel = rel_of(str(root / line[len("  DESC = clang-tidy "):].strip()), root, build)
            named = re.findall(r'--config-file=("[^"]+"|\S+)', command)
            rests = sorted({rel_of(i, root, build) for i in inputs if i.endswith(".clang-tidy")})
            # A generated source's step naming none finds none: its search up starts in the build tree.
            config, at = rel_of(named[0], root, build) if named else nearest(rel, root), None
            if not named and rel.startswith("BUILD/"):
                config = "none"
            if config.startswith("BUILD/"):
                config, at = "composed", config
                rests.remove(at)
            steps[rel] = (config, rests, at)
            command = None
    return steps


def with_digest(rel, root, build):
    if rel in (NONE, "none"):
        return rel
    path = build / rel[len("BUILD/"):] if rel.startswith("BUILD/") else root / rel
    return f"{rel}@{digest(path)}"


def ran(command, log=None, **how):
    """command's run; failed, it refuses the listing with its own text."""
    done = subprocess.run(command, capture_output=True, text=True, errors="replace", **how)
    if log:
        Path(log).write_text(done.stdout + done.stderr, encoding="utf-8")
    if done.returncode:
        raise SystemExit(f"loop_listing: {Path(command[0]).name} failed, exit {done.returncode}: {' '.join(command)}\n"
                         f"{done.stdout}{done.stderr}")
    return done


def cmake_lines(args):
    done = ran(["cmake", f"-DPython3_EXECUTABLE={sys.executable}", *args])
    return [line for line in done.stderr.splitlines() if line]


def answers(lines):
    return dict(line.split("=", 1) for line in lines if "=" in line)


def scopes(tree, paths):
    """{files: {path: (module, layer, scopes)}, exemptions: the list's path} by
    tree's own structure rules, read in a child that imports them from tree."""
    env = dict(os.environ, PYTHONPATH=str(tree))
    return json.loads(ran([sys.executable, "-P", __file__, "scopes", str(tree), str(paths)], env=env).stdout)


def child_scopes(tree, paths):
    from tests.structure import layers  # alone first: the rest read the table as they load, refused or not
    root = Path(tree).resolve()
    if not Path(layers.__file__).resolve().is_relative_to(root):
        raise SystemExit(f"loop_listing: the structure rules read are not {root}'s")
    table, errors = layers.read(root)
    if errors:
        raise SystemExit("loop_listing: the structure refuses this tree's table:\n" + "\n".join(map(str, errors)))
    from tests.structure import exemptions, rules, subset, test_log_includes
    from tests.structure.tree import source_files
    walker = set(source_files(root))
    found = {}
    for f in sorted(set(Path(paths).read_text(encoding="utf-8").splitlines()) | walker):
        cxx = f.endswith(subset.CXX)
        taken = (f in walker, layers.walked(f), layers.layered(f), table.test_code(f), table.library(f),
                 rules.is_core(f, table), cxx and subset.in_scope(f, table),
                 cxx and f.endswith((".hpp", ".cpp")) and bool(test_log_includes.read(f, table)))
        found[f] = (table.module(f), table.layer(f), [name for name, on in zip(SCOPES, taken) if on])
    print(json.dumps({"files": found, "exemptions": exemptions.LIST}))


def listing(checkout, commit, scratch):
    """The listing's lines for commit, its tree made and configured under scratch."""
    scratch = Path(scratch).resolve()
    if scratch.is_relative_to(Path(checkout).resolve()) or (scratch.exists() and any(scratch.iterdir())):
        raise SystemExit(f"loop_listing: {scratch} is not a new or empty directory outside {checkout}")
    root, build = scratch / "tree", scratch / "build"
    files = committed_tree(checkout, commit, root)
    paths = scratch / "paths.txt"
    paths.write_text("\n".join(files) + "\n", encoding="utf-8")
    scoped = scopes(root, paths)
    if scoped["exemptions"] not in files:
        raise SystemExit(f"loop_listing: {commit} has no {scoped['exemptions']}, which a compare judges by")
    data = json.loads((root / scoped["exemptions"]).read_text(encoding="utf-8"))
    ran(["cmake", "-S", str(root), "--preset", "analysis", "-B", str(build)], log=scratch / "configure.log")
    trees = str(root / "foundation" / "devkit" / "build" / "trees.cmake")
    analysed = answers(cmake_lines([f"-DROOT={root.as_posix()}", "-DSHOW=analysed",
                                    f"-DPATHFILE={paths.as_posix()}", "-P", trees]))
    units = answers(cmake_lines([f"-DROOT={root.as_posix()}", "-P", str(HERE / "loop_listing.cmake")]))
    regex = units.pop("ROOT")
    filters = [line.replace(regex, "ROOT") for line in
               cmake_lines([f"-DROOT={root.as_posix()}", "-DSHOW=filters", "-P", trees])]
    commands = (build / "compile_commands.json").read_text(encoding="utf-8")
    sets = rulesets(commands, root, build)
    steps = tidy_steps((build / "build.ninja").read_text(encoding="utf-8", errors="replace"), root, build)
    return [*lines_of(scoped["files"], analysed, units, sets, steps, filters, root, build,
                      externals(commands, root, build)), *ruleset_lines(commands, root, build, data["rule_sets"]),
            *exempt_lines(data)]


def lines_of(scoped, analysed, units, sets, steps, filters, root, build, external=()):
    """The file and filter lines from what was read; analysed maps a path to the
    build's answer (TRUE or FALSE first), filters are the build's, the
    checkout's root in them as ROOT."""
    def checks(f):
        fields = {"ruleset": ",".join(sets[f]) if f in sets else NONE}
        if f in external:
            fields["external"] = ",".join(external[f])
        config, rests, at = steps.get(f, (NONE, [], None))
        fields["tidy"] = f"composed@{digest(build / at[len('BUILD/'):])}" if at else with_digest(config, root, build)
        fields["tidy_from"] = ",".join(with_digest(r, root, build) for r in rests) or NONE
        if at:
            fields["tidy_file"] = at
        return fields

    out = []
    for f in sorted(set(scoped) | set(sets) | set(steps)):
        module, layer, taken = scoped.get(f, (None, None, []))
        if analysed.get(f, "FALSE").split()[0] == "TRUE" or f.startswith("BUILD/"):
            taken = [*taken, "analysed"]
        if "walked" not in taken and "analysed" not in taken and f not in sets and f not in steps:
            continue
        fields = {"module": module or NONE, "layer": layer or NONE, "scopes": ",".join(taken) or NONE, **checks(f)}
        if f in units:
            fields["unit"] = units[f]
            fields.update({f"unit_{k}": v for k, v in checks(units[f]).items()})
        out.append(" ".join([f.replace(" ", "%20"), *(f"{k}={v}" for k, v in fields.items())]))
    for line in filters:
        key, _, value = line.partition("=")
        value = value.replace(" ", "%20")
        out.append(f"cmake:{key} value={value}")
    return out


def main(argv):
    if argv[:1] == ["scopes"] and len(argv) == 3:
        child_scopes(*argv[1:])
        return 0
    if argv[:1] == ["list"] and len(argv) == 5:
        lines = listing(*argv[1:4])
        Path(argv[4]).write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"loop_listing: {len(lines)} lines in {argv[4]}")
        return 0
    if argv[:1] == ["compare"] and len(argv) in (3, 4, 5):
        return COMPARE["main"](argv[1:])
    print(__doc__)
    print(COMPARE["__doc__"])
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
