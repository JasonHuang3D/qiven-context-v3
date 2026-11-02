"""The checks a move must leave as they were: listed from a commit's committed
tree, and two listings compared.

loop_listing.py list CHECKOUT COMMIT SCRATCH OUT
  COMMIT's committed tree, by git archive, into SCRATCH/tree (SCRATCH new or
  empty, outside CHECKOUT), its analysis preset configured into SCRATCH/build
  by the caller's cmake and compiler, then OUT: a line for each file walked,
  analysed or given a rule set or a clang-tidy step, sources generated in the
  build tree as BUILD/<path>:
    PATH module=M layer=L scopes=S ruleset=R tidy=C tidy_from=F [tidy_file=P]
      [unit=U unit_ruleset=R unit_tidy=C unit_tidy_from=F]
  S: each scope of the commit's own rules taking it (walker, walked, layered,
  test_code, library, core, subset, k15, analysed). R: each compile of it,
  by the rule sets it names (joined by |), none for a compile naming none,
  the compiles sorted and joined by ,; - for no compile. C: its
  clang-tidy step's configuration, path@digest: the file the step names, or
  the nearest .clang-tidy above the source for a step naming none; for a
  file the build composes, composed@digest, P its path, and F what it was
  composed from; - for no step. F: the configurations the step rests on, as
  its build line lists them. A header the build analyses names the unit it
  compiles through, U, as the build's own qiven3_header_units reads it (by
  loop_listing.cmake; a COMMIT whose build names none is refused), with U's
  rule set and configuration. Then the build's
  header filters, as lines named cmake:filter and cmake:exclude.

loop_listing.py compare BEFORE AFTER [MAP [EXPECTED]]
  The lines that differ, BEFORE's paths renamed by MAP first; exit 0 when
  none do. A field named *_file is not compared: the build names it. MAP: a
  move a line, OLD NEW, from the checkout's root; a path ending in / renames
  what is under it; # starts a comment. A line merge NEW OLD... moves each
  OLD to NEW as one merge. The map is refused, each move named, exit 1, when
  an OLD is no path of BEFORE nor a directory above one, when two OLDs with
  paths or two paths of BEFORE move to one NEW, or when a NEW is a path of
  BEFORE, or a directory above one, that no move takes away; a NEW a merge
  line names is not refused so. Each path a merge takes is compared with
  NEW's line of AFTER, named NEW (from OLD) where NEW takes more than one.
  EXPECTED: a line for each difference a move names, PATH FIELD BEFORE
  AFTER, as the compare prints it (PATH as AFTER names it; FIELD line,
  BEFORE or AFTER listed, for a path only one listing has); # starts a
  comment. With it, exit 0 when each difference is named and none loosens;
  printed and exit 1: each difference not named, each named one loosening
  (refused: PATH FIELD: why) and each line naming none (stale). A move may
  tighten, never loosen, by the checkout's analysis-exemptions.json: the
  library's rule set is the one the test scope's includes; narrower are the
  test scope's rule set and configuration, every other rule set it lists
  and each directory scope's .clang-tidy. Each field, named, is
    line                      loosens: a path leaving the listing; a new one tightens
    module, layer, unit       loosens: to -; any other change tightens
    scopes                    loosens: a scope lost but test_code, test_code gained;
                              tightens: test_code lost, a scope gained
    ruleset, unit_ruleset     tightens: each compile the library's rule set alone,
                              no fewer compiles; else loosens (none, narrower, other)
    tidy, tidy_from, unit_tidy, unit_tidy_from
                              tightens: the root .clang-tidy alone where another was;
                              else loosens (-, none, narrower, composed, content changed)
    value (cmake:filter, cmake:exclude), any other field: loosens
"""

import hashlib
import io
import json
import os
import re
import runpy
import subprocess
import sys
import tarfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
GIT = runpy.run_path(str(HERE / "loop_git.py"))  # git's one door, by runpy: under -I this directory is off sys.path
NONE = "-"
SCOPES = ("walker", "walked", "layered", "test_code", "library", "core", "subset", "k15")


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


def rulesets(commands, root, build):
    """{path: [rule sets of each compile]} from the build's compile_commands.json
    text: a compile's rule sets joined by |, none for a compile naming none."""
    found = {}
    for entry in json.loads(commands):
        named = sorted({rel_of(ruleset, root, build) for ruleset in
                        re.findall(r'/analyze:ruleset(?!directory)\s*("[^"]+"|\S+)', entry["command"])})
        found.setdefault(rel_of(entry["file"], root, build), []).append("|".join(named) or "none")
    return {path: sorted(compiles) for path, compiles in found.items()}


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


def cmake_lines(args):
    done = subprocess.run(["cmake", f"-DPython3_EXECUTABLE={sys.executable}", *args],
                          capture_output=True, text=True, check=True)
    return [line for line in done.stderr.splitlines() if line]


def answers(lines):
    return dict(line.split("=", 1) for line in lines if "=" in line)


def scopes(tree, paths):
    """{path: (module, layer, scopes)} by tree's own structure rules, read in
    a child that imports them from tree."""
    env = dict(os.environ, PYTHONPATH=str(tree))
    done = subprocess.run([sys.executable, "-P", __file__, "scopes", str(tree), str(paths)], env=env,
                          capture_output=True, text=True, check=True)
    return json.loads(done.stdout)


def child_scopes(tree, paths):
    from tests.structure import layers, rules, subset, test_log_includes
    from tests.structure.tree import source_files
    root = Path(tree).resolve()
    if not Path(layers.__file__).resolve().is_relative_to(root):
        raise SystemExit(f"loop_listing: the structure rules read are not {root}'s")
    walker = set(source_files(root))
    table = layers.of(root)
    found = {}
    for f in sorted(set(Path(paths).read_text(encoding="utf-8").splitlines()) | walker):
        cxx = f.endswith(subset.CXX)
        taken = (f in walker, layers.walked(f), layers.layered(f), table.test_code(f), table.library(f),
                 rules.is_core(f, table), cxx and subset.in_scope(f, table),
                 cxx and f.endswith((".hpp", ".cpp")) and bool(test_log_includes.read(f, table)))
        found[f] = (table.module(f), table.layer(f), [name for name, on in zip(SCOPES, taken) if on])
    print(json.dumps(found))


def listing(checkout, commit, scratch):
    """The listing's lines for commit, its tree made and configured under scratch."""
    scratch = Path(scratch).resolve()
    if scratch.is_relative_to(Path(checkout).resolve()) or (scratch.exists() and any(scratch.iterdir())):
        raise SystemExit(f"loop_listing: {scratch} is not a new or empty directory outside {checkout}")
    root, build = scratch / "tree", scratch / "build"
    files = committed_tree(checkout, commit, root)
    paths = scratch / "paths.txt"
    paths.write_text("\n".join(files) + "\n", encoding="utf-8")
    with (scratch / "configure.log").open("w", encoding="utf-8") as log:
        subprocess.run(["cmake", "-S", str(root), "--preset", "analysis", "-B", str(build)], stdout=log,
                       stderr=subprocess.STDOUT, check=True)
    trees = str(root / "foundation" / "devkit" / "build" / "trees.cmake")
    analysed = answers(cmake_lines([f"-DROOT={root.as_posix()}", "-DSHOW=analysed",
                                    f"-DPATHFILE={paths.as_posix()}", "-P", trees]))
    units = answers(cmake_lines([f"-DROOT={root.as_posix()}", "-P", str(HERE / "loop_listing.cmake")]))
    regex = units.pop("ROOT")
    filters = [line.replace(regex, "ROOT") for line in
               cmake_lines([f"-DROOT={root.as_posix()}", "-DSHOW=filters", "-P", trees])]
    sets = rulesets((build / "compile_commands.json").read_text(encoding="utf-8"), root, build)
    steps = tidy_steps((build / "build.ninja").read_text(encoding="utf-8", errors="replace"), root, build)
    return lines_of(scopes(root, paths), analysed, units, sets, steps, filters, root, build)


def lines_of(scoped, analysed, units, sets, steps, filters, root, build):
    """The listing's lines from what was read; analysed maps a path to the
    build's answer (TRUE or FALSE first), filters are the build's, the
    checkout's root in them as ROOT."""
    def checks(f):
        fields = {"ruleset": ",".join(sets[f]) if f in sets else NONE}
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


def read_map(text):
    """({old: new}, the NEW paths named merges) of a rename map's text."""
    moves, merged = {}, set()
    for line in text.splitlines():
        words = line.split("#", 1)[0].split()
        if len(words) > 2 and words[0] == "merge":
            moves.update(dict.fromkeys(words[2:], words[1]))
            merged.add(words[1])
        elif len(words) == 2:
            moves[words[0]] = words[1]
        elif words:
            raise SystemExit(f"loop_listing: a map line is OLD NEW or merge NEW OLD...: {line}")
    return moves, merged


def mover(path, moves):
    """The OLD of moves taking path: its own, else the longest directory above it; None for none."""
    under = [old for old in moves if old == path or old.endswith("/") and path.startswith(old)]
    return max(under, key=len) if under else None


def renamed(path, moves):
    old = mover(path, moves)
    return path if old is None else moves[old] + path[len(old):]


def refusals(paths, moves, merged):
    """The moves refused against the earlier listing's paths, each named: an OLD naming none of them; two
    OLDs with paths, or two paths, taking one NEW; a NEW already among them and staying; unless NEW is merged."""
    def under(path, at):
        return path == at or at.endswith("/") and path.startswith(at)
    out, into, taken = [], {}, {}
    for old, new in sorted(moves.items()):
        if not any(under(p, old) for p in paths):
            out.append(f"refused: {old} is no path of the earlier listing")
        elif new not in merged:
            taken.setdefault(new, []).append(old)
            out.extend(f"refused: {old} moves to {new}, where {p} stays" for p in paths
                       if under(p, new) and mover(p, moves) is None)
    out.extend(f"refused: {' and '.join(olds)} move to one path, {new}" for new, olds in taken.items()
               if len(olds) > 1)
    for p in paths:
        into.setdefault(renamed(p, moves), []).append(p)
    for new, olds in sorted(into.items()):
        news = {moves[mover(p, moves)] if mover(p, moves) else None for p in olds}  # a path staying: None
        if len(olds) > 1 and None not in news and not news <= merged:
            out.append(f"refused: {' and '.join(olds)} become one path, {new}")
    return out


def parse(lines, moves):
    """{path: [(path before, {field: value})]}, paths and path values renamed by moves."""
    def value(text):
        return "".join(part if part in ",|" else "@".join([renamed(part.split("@")[0], moves), *part.split("@")[1:]])
                       for part in re.split(r"([,|])", text))
    found = {}
    for line in lines:
        path, *fields = line.split()
        found.setdefault(renamed(path, moves), []).append(
            (path, {k: value(v) for k, v in (field.split("=", 1) for field in fields) if not k.endswith("_file")}))
    return found


def read_expected(text):
    """The (path, field, before, after) lines of an expected differences' text."""
    lines = [line.split("#", 1)[0].split() for line in text.splitlines()]
    for words in lines:
        if words and len(words) != 4:
            raise SystemExit(f"loop_listing: an expected line is PATH FIELD BEFORE AFTER: {' '.join(words)}")
    return [tuple(words) for words in lines if words]


def narrower(data):
    """(the library's rule set, {configuration narrower than the library's: what it is}) of the exemptions' data."""
    test, sets = data["test_code"], data["rule_sets"]
    library = next(r for r in sets if Path(r).name in sets[test["msvc"]]["includes"])
    found = {r: "a rule set narrower than the library's" for r in sets if r != library}
    found.update({test["msvc"]: "the test scope's rule set", test["clang-tidy"]: "the test scope's configuration"})
    found.update({e["scope"] + ".clang-tidy": f"the exemptions of {e['scope']}" for e in data["exemptions"]
                  if e["scope"].endswith("/")})
    return library, found


def loosens(field, then, now, library, narrow):
    """Why a named difference loosens the checks; None for a tightening."""
    def each(value):
        return [part.split("@")[0] for part in re.split(r"[,|]", value) if part != NONE]
    key = field.removeprefix("unit_")
    if field == "line":
        return "leaves the listing" if now == NONE else None
    if field in ("module", "layer", "unit"):
        return f"leaves its {field}" if now == NONE else None
    if field == "scopes":
        lost, gained = set(each(then)) - set(each(now)), set(each(now)) - set(each(then))
        why = [f"leaves {s}" for s in sorted(lost - {"test_code"})] + ["enters test_code"] * ("test_code" in gained)
        return ", ".join(why) or None
    if key not in ("ruleset", "tidy", "tidy_from"):
        return f"{field} is no field a move may change"
    to, fewer = each(now), now == NONE or then != NONE and now.count(",") < then.count(",")
    if key == "ruleset" and not fewer and set(now.split(",")) == {library}:
        return None
    if key != "ruleset" and to == [".clang-tidy"] and each(then) != to:
        return None
    if not to or "none" in to or key == "ruleset" and fewer:
        return "leaves the analysis" if not to or "none" in to else "a compile of it is lost"
    if to == each(then):
        return "its configuration's content changes"
    return next((f"enters {narrow[p]}" for p in to if p in narrow), f"is {now}, not the library's alone")


def differences(old, new):
    """(the line telling it, (path, field, before, after)) for each difference of parsed listings."""
    for path in sorted(set(old) | set(new)):
        if path not in new:
            yield f"- {path} (only before)", (path, "line", "listed", NONE)
        elif path not in old:
            yield f"+ {path} (only after)", (path, "line", NONE, "listed")
        else:
            now = new[path][0][1]
            for was, then in old[path]:
                name = path if len(old[path]) == 1 else f"{path} (from {was})"
                for k in sorted(set(then) | set(now)):
                    if then.get(k) != now.get(k):
                        yield f"~ {name} {k}: {then.get(k, NONE)} -> {now.get(k, NONE)}", \
                            (path, k, then.get(k, NONE), now.get(k, NONE))


def compare(before, after, moves, merged=(), expected=None, data=None):
    """The lines telling how listing after differs from before under moves,
    or the moves refused; a path merged compared from each path before.
    With expected, (path, field, before, after) lines, those lines' differences
    are left out but each loosening one, by data, the exemptions', and each
    line naming no difference, both refused."""
    refused = refusals([line.split()[0] for line in before if line.strip()], moves, set(merged))
    if refused:
        return refused
    found = list(differences(parse(before, moves), parse(after, moves={})))
    if expected is None:
        return [text for text, _ in found]
    library, narrow = narrower(data)
    named, keys = set(expected), {key for _, key in found}
    out = [text for text, key in found if key not in named]
    out.extend(f"refused: {p} {f}: {why}" for p, f, a, b in sorted(named & keys)
               if (why := loosens(f, a, b, library, narrow)))
    return out + [f"refused: stale, {' '.join(line)} names no difference" for line in sorted(named - keys)]


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
        read = [Path(a).read_text(encoding="utf-8").splitlines() for a in argv[1:3]]
        moves, merged = read_map(Path(argv[3]).read_text(encoding="utf-8")) if len(argv) > 3 else ({}, set())
        expected = read_expected(Path(argv[4]).read_text(encoding="utf-8")) if len(argv) == 5 else None
        data = json.loads((HERE.parent / "foundation/devkit/build/analysis-exemptions.json").read_text(
            encoding="utf-8")) if expected is not None else None
        differ = compare(*read, moves, merged, expected, data)
        print("\n".join(differ) if differ else "loop_listing: the listings are equal" if not expected else
              f"loop_listing: the listings are equal but for {len(expected)} named differences, none loosening")
        return 1 if differ else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
