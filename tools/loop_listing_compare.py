"""Two listings of tools/loop_listing.py compared.

loop_listing.py compare BEFORE AFTER [MAP [EXPECTED]]
  The lines that differ, BEFORE's paths renamed by MAP first; exit 0 when
  none do. A field named *_file is not compared: the build names it. MAP: a
  move a line, OLD NEW, from the checkout's root; a path ending in / renames
  what is under it; a target the exemptions name renames as a path does; #
  starts a comment. A line merge NEW OLD... moves each
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
  tighten, never loosen, judged for both listings by BEFORE's exemptions
  alone, its paths under MAP, never by the running checkout's: the
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
    external, unit_external   loosens: a path, a file taken by angle brackets or an
                              /external:env: newly external; a path gone tightens
    digest (ruleset:PATH)     loosens: a rule set's content changed
    line of ruleset:PATH      loosens: a rule set new or gone
    line of exempt:...        loosens: an entry added, or a check's scope grown;
                              tightens: an entry gone, a scope inside one BEFORE has
    reason                    neither; a move may name it
    value (cmake:filter, cmake:exclude, exempt:...), any other field: loosens
  Such a change is its own commit, never a move's.
"""

import re
from collections import Counter
from pathlib import Path

NONE = "-"
SECTION = re.compile(r"ruleset:|exempt:(?:[^:]+:)*")  # a line's key before its path or target


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


def section(key):
    """(the section a listing line's key opens, its path or target); a file's or a filter's opens none."""
    cut = SECTION.match(key).end() if SECTION.match(key) else 0
    return key[:cut], key[cut:]


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
        head, name = section(path)
        found.setdefault(head + renamed(name, moves), []).append(
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


def table_of(old):
    """The exemptions' data narrower reads, from the parsed earlier listing, under its map."""
    data = {"rule_sets": {}, "test_code": {}, "exemptions": []}
    for key, rows in old.items():
        head, name = section(key)
        if head == "exempt:rule_set:":
            data["rule_sets"][name] = {"includes": [Path(i).name for i in rows[0][1]["includes"].split(",")]}
        elif head == "exempt:test_code:":
            data["test_code"][name] = rows[0][1]["value"]
        elif head.startswith("exempt:check:"):
            data["exemptions"].append({"scope": name})
    if not data["test_code"]:
        raise SystemExit("loop_listing: the earlier listing carries no exemptions' data to judge by")
    return data


def exempt_line(key, now, old):
    """Why an exemptions' line named as coming (now listed) or going is
    refused: a check's scope inside one the earlier listing has narrows it."""
    head, scope = section(key)
    if now == NONE:
        return None if head.startswith(("exempt:check:", "exempt:target:", "exempt:not_defined:",
                                        "exempt:rule_set:")) else "leaves the listing"
    if not head.startswith("exempt:check:"):
        return "an exemption added"
    had = [section(other)[1] for other in old if section(other)[0] == head]
    if any(s == "every%20file" or s.endswith("/") and scope.startswith(s) for s in had):
        return None
    return "an exemption's scope grown" if had else "an exemption added"


def judged(key, field, then, now, old, table):
    """Why a named difference is refused; None for a tightening."""
    if field == "line" and key.startswith("exempt:"):
        return exempt_line(key, now, old)
    if field == "line" and key.startswith("ruleset:") and then == NONE:
        return "a rule set no compile took"
    return loosens(field, then, now, *table)


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
    if field == "reason":
        return None
    if field == "digest":
        return "the rule set's content changed"
    if key == "external":
        new = Counter(each(now)) - Counter(each(then))
        return ", ".join(f"{p} is newly external" for p in sorted(new) if p != "none") or None
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


def compare(before, after, moves, merged=(), expected=None):
    """The lines telling how listing after differs from before under moves,
    or the moves refused; a path merged compared from each path before.
    With expected, (path, field, before, after) lines, those lines' differences
    are left out but each loosening one, by before's own exemptions' data, and
    each line naming no difference, both refused."""
    paths = dict.fromkeys(section(line.split()[0])[1] for line in before if line.strip())
    refused = refusals(list(paths), moves, set(merged))
    if refused:
        return refused
    old = parse(before, moves)
    found = list(differences(old, parse(after, moves={})))
    if expected is None:
        return [text for text, _ in found]
    table = narrower(table_of(old))
    named, keys = set(expected), {key for _, key in found}
    out = [text for text, key in found if key not in named]
    out.extend(f"refused: {p} {f}: {why}" for p, f, a, b in sorted(named & keys)
               if (why := judged(p, f, a, b, old, table)))
    return out + [f"refused: stale, {' '.join(line)} names no difference" for line in sorted(named - keys)]


def main(argv):
    """BEFORE AFTER [MAP [EXPECTED]]: the exit code; the differences printed."""
    read = [Path(a).read_text(encoding="utf-8").splitlines() for a in argv[:2]]
    moves, merged = read_map(Path(argv[2]).read_text(encoding="utf-8")) if len(argv) > 2 else ({}, set())
    expected = read_expected(Path(argv[3]).read_text(encoding="utf-8")) if len(argv) == 4 else None
    differ = compare(*read, moves, merged, expected)
    print("\n".join(differ) if differ else "loop_listing: the listings are equal" if not expected else
          f"loop_listing: the listings are equal but for {len(expected)} named differences, none loosening")
    return 1 if differ else 0
