"""Two listings of tools/loop_listing.py compared.

loop_listing.py compare BEFORE AFTER [MAP [EXPECTED]]
  The lines that differ, BEFORE's paths renamed by MAP first; exit 0 when
  none do. A field named *_file is not compared: the build names it. MAP: a
  move a line, OLD NEW, from the checkout's root; a path ending in / renames
  what is under it; a target the exemptions name renames as a path does; #
  starts a comment. A line merge NEW OLD... moves each
  OLD to NEW as one merge. A move's MAP and EXPECTED are its own files,
  named for it and kept in its commit, tools/moves/<move>.map and
  tools/moves/<move>.expected, and read against the commit before it, so
  BEFORE is that commit's listing: a file moved twice is two maps and two
  compares. An OLD two lines name is refused, both lines named, exit 1. The
  map is refused, each move named, exit 1, when
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
                              tightens: test_code lost, a scope gained; library and
                              core follow the file's layer alone: either lost, its
                              layer's difference named too, neither loosens nor
                              tightens
    ruleset, unit_ruleset     tightens: each compile the library's rule set alone,
                              no fewer compiles; else loosens (none, narrower, other)
    tidy, tidy_from, unit_tidy, unit_tidy_from
                              tightens: the root .clang-tidy alone where another was;
                              else loosens (-, none, narrower, composed, content changed)
    external, unit_external   loosens: a path, a file taken by angle brackets or an
                              /external:env: newly external; a path gone tightens
    options, unit_options     loosens: any change, told as each option dropped, each
                              added that weakens (/w, /W0 to /W3, /WX-, /wd, /wo,
                              /w1 to /w4, /Wv, /external:W0 to W3, /analyze-, an
                              /analyze:...-), each other added, or its order; but
                              a change of a rule set's path alone, in its place,
                              tightens when its ruleset field's change is named and
                              tightens, else loosens
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
OF_LAYER = frozenset(("library", "core"))  # scopes the structure's layers decide; a listing carries no such data
PATHED = ("/analyze:rulesetdirectory", "-analyze:rulesetdirectory", "/analyze:ruleset", "-analyze:ruleset")
RULE_SET_PATH = re.compile(f"({'|'.join(map(re.escape, PATHED))})[^,|]*")  # an option naming a rule set's path
WEAKER = re.compile(r"[/-](?:w|W[0-3]|WX-|w[o1-4]\d+|wd\d+|Wv(?::\S*)?|external:W[0-3]|analyze-|analyze:\S*-)")


def read_map(text):
    """({old: new}, the NEW paths named merges) of a rename map's text; an OLD two lines name is refused."""
    moves, merged, where = {}, set(), {}
    for at, line in enumerate(text.splitlines(), 1):
        words = line.split("#", 1)[0].split()
        if len(words) > 2 and words[0] == "merge":
            olds, new = words[2:], words[1]
            merged.add(new)
        elif len(words) == 2:
            olds, new = words[:1], words[1]
        elif words:
            raise SystemExit(f"loop_listing: a map line is OLD NEW or merge NEW OLD...: {line}")
        else:
            continue
        for old in olds:
            if old in where:
                raise SystemExit(f"loop_listing: refused: {old} is named twice: {where[old]}; line {at}: {line}")
            where[old] = f"line {at}: {line}"
            moves[old] = new
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
    def one(part):
        head = next((p for p in PATHED if part.startswith(p)), "")  # an option naming a path: the path renamed
        rest = part[len(head):]
        return head + "@".join([renamed(rest.split("@")[0], moves), *rest.split("@")[1:]])

    def value(text):
        return "".join(part if part in ",|" else one(part) for part in re.split(r"([,|])", text))
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


def layer_named(old, lines):
    """(path, scopes) of each file of the parsed earlier listing whose layer's difference is among lines."""
    changed = {(p, was) for p, field, was, _ in lines if field == "layer"}
    return {(p, then.get("scopes")) for p, rows in old.items() for _, then in rows
            if (p, then.get("layer")) in changed}


def judged(key, field, then, now, old, table, layered=(), ruled=()):
    """Why a named difference is refused; None for a tightening; ruled: the
    (path, field) of each ruleset field's named change that tightens."""
    if field.removeprefix("unit_") == "options" and RULE_SET_PATH.sub(r"\1", then) == RULE_SET_PATH.sub(r"\1", now):
        return None if (key, field.replace("options", "ruleset")) in ruled else \
            "its rule set's path changes, and no tightening of its ruleset field is named"
    if field == "line" and key.startswith("exempt:"):
        return exempt_line(key, now, old)
    if field == "line" and key.startswith("ruleset:") and then == NONE:
        return "a rule set no compile took"
    return loosens(field, then, now, *table, OF_LAYER if field == "scopes" and (key, then) in layered else ())


def loosens(field, then, now, library, narrow, follows=()):
    """Why a named difference loosens the checks; None for a tightening; a scope of follows may be lost."""
    def each(value):
        return [part.split("@")[0] for part in re.split(r"[,|]", value) if part != NONE]
    key = field.removeprefix("unit_")
    if field == "line":
        return "leaves the listing" if now == NONE else None
    if field in ("module", "layer", "unit"):
        return f"leaves its {field}" if now == NONE else None
    if field == "scopes":
        lost, gained = set(each(then)) - set(each(now)), set(each(now)) - set(each(then))
        why = [f"leaves {s}" for s in sorted(lost - {"test_code", *follows})]
        why += ["enters test_code"] * ("test_code" in gained)
        return ", ".join(why) or None
    if field == "reason":
        return None
    if field == "digest":
        return "the rule set's content changed"
    if key == "external":
        new = Counter(each(now)) - Counter(each(then))
        return ", ".join(f"{p} is newly external" for p in sorted(new) if p != "none") or None
    if key == "options":
        return options_changed(then, now)
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


def options_changed(then, now):
    """Why a change of a file's compile options is refused, always: each
    option dropped, each added that weakens a check, each other added, or
    the options moved between its compiles or within one."""
    def each(value):
        return Counter(part for part in re.split(r"[,|]", value) if part not in (NONE, "none"))
    gained = sorted(each(now) - each(then))
    parts = (("drops", sorted(each(then) - each(now))), ("weakens by", [o for o in gained if WEAKER.fullmatch(o)]),
             ("adds", [o for o in gained if not WEAKER.fullmatch(o)]))
    return "; ".join(f"{how} {', '.join(found)}" for how, found in parts if found) or \
        "its options change order or compile"


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
    layered = layer_named(old, named & keys)
    out = [text for text, key in found if key not in named]
    ruled = {(p, f) for p, f, a, b in named & keys
             if f.removeprefix("unit_") == "ruleset" and not judged(p, f, a, b, old, table, layered)}
    out.extend(f"refused: {p} {f}: {why}" for p, f, a, b in sorted(named & keys)
               if (why := judged(p, f, a, b, old, table, layered, ruled)))
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
