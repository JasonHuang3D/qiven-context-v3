"""The navigation aid's commands over clangd (tools/nav.py): a name's
symbols and their references, callers and unresolved uses; a name's type
on a line; a header's declarations.

Loaded by tools/nav.py by runpy.run_path, given LSP, tools/nav_lsp.py's
names.
"""

import re
from pathlib import Path

Refusal, path_of, uri = LSP["Refusal"], LSP["path_of"], LSP["uri"]  # noqa: F821, given by nav.py

NAME = re.compile(r"(?:::)?(?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*")
SPECIAL = re.compile(r"//|/\*|R\"|\"|'")
RAW = re.compile(r"R\"([^ ()\\\t\n]{0,16})\(")
FUNCTIONS = {6, 9, 12}  # method, constructor, function
KINDS = {3: "namespace", 5: "class", 6: "method", 8: "field", 9: "constructor", 10: "enum", 12: "function",
         13: "variable", 14: "constant", 22: "enumerator", 23: "struct", 26: "type parameter"}
OCCURRENCES = 400  # the uses of a file asked at most
COVER = 24  # the symbols of a name whose references the index's scan takes at most
CALLER_FILES = 12  # the files whose functions callers asks at most
LOAD_WAIT = 120  # seconds a query waits for the index's loading at most
DECLARATION = 200  # a declaration's characters at most
UNRESOLVED = ("lines where the name is called or used as a member and clangd returned nothing there, "
              "such as a call through a template parameter: read them by hand")


def listed(items):
    return {"count": len(items), "shown": len(items), "items": list(items)}


def rel(root, path):
    try:
        return Path(path).resolve().relative_to(root).as_posix()
    except ValueError:
        return Path(path).as_posix()


def code_lines(text):
    """The text's lines with comments and string and character literals
    blanked, each column kept."""
    chars = list(text)
    i, n = 0, len(text)
    while True:
        found = SPECIAL.search(text, i)
        if found is None:
            break
        i, token = found.start(), found.group()
        if token == "//":
            j = text.find("\n", i)
        elif token == "/*":
            j = text.find("*/", i + 2)
            j = -1 if j < 0 else j + 2
        elif token == 'R"':
            raw = RAW.match(text, i)
            if raw is None or (i and (text[i - 1].isalnum() and text[i - 1] not in "uUL8" or text[i - 1] == "_")):
                i += 1
                continue
            j = text.find(")" + raw.group(1) + '"', raw.end())
            j = -1 if j < 0 else j + len(raw.group(1)) + 2
        else:
            if token == "'" and i and text[i - 1].isalnum():  # a digit separator
                i += 1
                continue
            j = i + 1
            while j < n and text[j] not in (token, "\n"):
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
        j = n if j < 0 else j
        for k in range(i, j):
            if chars[k] != "\n":
                chars[k] = " "
        i = j
    return "".join(chars).split("\n")


def uses(line, word):
    """Each use of word on a code line: (column, how), how a call, a member,
    a member call, or None for any other use."""
    for found in re.finditer(r"(?<![\w~])" + re.escape(word) + r"(?!\w)", line):
        before, after = line[:found.start()].rstrip(), line[found.end():].lstrip()
        member = before.endswith((".", "->", "::"))
        call = after.startswith("(") or re.match(r"<[^;{}()]*>\s*\(", after) is not None
        how = "member call" if member and call else "member" if member else "call" if call else None
        yield found.start(), how


def column(lines, line, col):
    """A character column as the protocol's UTF-8 bytes."""
    return len(lines[line][:col].encode("utf-8"))


def full_name(info):
    container = (info.get("containerName") or "").rstrip(":")
    return f"{container}::{info['name']}" if container else info["name"]


def matches(name, parts):
    """Whether a symbol's full name ends with the query's components."""
    have = [re.sub(r"<.*>", "", p) for p in name.split("::") if p and p != "(anonymous)"]
    return have[-len(parts):] == parts


def at_line(root, location, key):
    if not location:
        return None
    return f"{rel(root, path_of(location['uri']))}:{location[key]['start']['line'] + 1}"


def add(found, root, infos, path, line, col):
    """symbolInfo's symbols into found by USR; their USRs."""
    usrs = []
    for info in infos or []:
        usr = info.get("usr")
        if not usr:
            continue
        usrs.append(usr)
        found.setdefault(usr, {"name": full_name(info),
                               "declaration": at_line(root, info.get("declarationRange"), "range"),
                               "definition": at_line(root, info.get("definitionRange"), "range"),
                               "at": (path, line, col)})
    return usrs


def scan_file(clangd, root, path, word, found, unresolved):
    """symbolInfo at each use of word in the file: its symbols into found,
    a call or member use with none into unresolved. The USRs it uses, and
    how many uses went unasked."""
    lines = clangd.open(path)
    code = code_lines("\n".join(lines))
    usrs, asked, unasked = set(), 0, 0
    for number, text in enumerate(code):
        for col, how in uses(text, word):
            if asked >= OCCURRENCES:
                unasked += 1
                continue
            asked += 1
            byte = column(lines, number, col)
            got = add(found, root, clangd.at("textDocument/symbolInfo", path, number, byte), path, number, byte)
            usrs.update(got)
            if not got and how:
                places = clangd.at("textDocument/definition", path, number, byte) or []
                said = (f"no symbol; definition by name: {', '.join(at_line(root, p, 'range') for p in places[:4])}"
                        if places else "nothing")
                unresolved.append({"at": f"{rel(root, path)}:{number + 1}", "use": how,
                                   "text": lines[number].strip()[:120], "clangd": said})
    return usrs, unasked


def references(clangd, root, symbol):
    path, line, col = symbol["at"]
    got = clangd.at("textDocument/references", path, line, col, context={"includeDeclaration": False}) or []
    places = {(rel(root, path_of(r["uri"])), r["range"]["start"]["line"] + 1, path_of(r["uri"])) for r in got}
    return sorted(places, key=lambda p: (p[0], p[1]))


def index_scan(root, files, word, covered, skip):
    """The index's unresolved: each call or member use of word in the
    checkout's C and C++ files that no reference or declaration covers."""
    out = []
    pattern = re.compile(r"(?<!\w)" + re.escape(word) + r"(?!\w)")
    for name in sorted(files or ()):
        if name in skip:
            continue
        try:
            text = (root / name).read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if not pattern.search(text):
            continue
        raw = text.split("\n")
        for number, line in enumerate(code_lines(text)):
            for _, how in uses(line, word):
                if how and (name, number + 1) not in covered:
                    out.append({"at": f"{name}:{number + 1}", "use": how, "text": raw[number].strip()[:120],
                                "clangd": "no reference in the index"})
                    break
    return out


def flatten(symbols, parents=()):
    """documentSymbol's tree as (symbol, its parents)."""
    for symbol in symbols or []:
        yield symbol, parents
        yield from flatten(symbol.get("children"), (*parents, symbol))


def enclosing(clangd, root, places):
    """callers: references grouped by the function that holds them."""
    groups, files = {}, []
    for name, line, path in places:
        if name not in files:
            files.append(name)
        if len(files) > CALLER_FILES:
            caller = f"(not asked: past {CALLER_FILES} files)"
        else:
            caller = holder(clangd, path, line)
        groups.setdefault((name, caller), []).append(line)
    return [{"caller": caller, "file": name, "lines": lines} for (name, caller), lines in groups.items()]


def holder(clangd, path, line):
    if path not in clangd.symbols:
        clangd.open(path)
        clangd.symbols[path] = list(flatten(clangd.request("textDocument/documentSymbol",
                                                           {"textDocument": {"uri": uri(path)}})))
    best = None
    for symbol, parents in clangd.symbols[path]:
        r = symbol["range"]
        if symbol.get("kind") in FUNCTIONS and r["start"]["line"] + 1 <= line <= r["end"]["line"] + 1:
            if best is None or r["start"]["line"] >= best[0]["range"]["start"]["line"]:
                best = (symbol, parents)
    if best is None:
        return "(outside a function)"
    names = [p["name"] for p in best[1] if p.get("kind") in (5, 23)] + [best[0]["name"]]
    return "::".join(names)


def named(args, clangd, files, state, commit):
    """def, refs and callers: the data, its symbols listed."""
    root, word = args.root, args.name.split("::")[-1]
    parts = [p for p in args.name.split("::") if p]
    found, unresolved, in_file, unasked, skip = {}, [], set(), 0, set()
    clangd.symbols = {}
    path = (root / args.file).resolve() if args.file else None
    if path is not None and not path.is_file():
        raise Refusal("failed", "NO_FILE", f"no file {args.file}", "give a file of the checkout")
    if state["present"]:  # a file opened loads the database, and the index with it
        clangd.open(path or args.opener)
        state["loaded"] = clangd.wait_index(LOAD_WAIT, loaded_only=True)
    if path is not None:
        in_file, unasked = scan_file(clangd, root, path, word, found, unresolved)
        skip.add(rel(root, path))
    if state["present"]:
        for item in clangd.request("workspace/symbol", {"query": word}) or []:
            if item.get("name") != word:
                continue
            path = path_of(item["location"]["uri"])
            start = item["location"]["range"]["start"]
            add(found, root, clangd.at("textDocument/symbolInfo", path, start["line"], start["character"]),
                path, start["line"], start["character"])
    answered = [s for usr, s in found.items() if matches(s["name"], parts) and (not args.file or usr in in_file)]
    covered = set()
    others = [s for s in found.values() if s not in answered] if state["present"] else []
    asked = answered + others[:max(0, COVER - len(answered))]
    for symbol in asked:
        symbol["references"] = references(clangd, root, symbol)
        covered.update((name, line) for name, line, _ in symbol["references"])
        for key in ("declaration", "definition"):
            if symbol[key]:
                name, _, line = symbol[key].rpartition(":")
                covered.add((name, int(line)))
    if state["present"]:
        unresolved += index_scan(root, files, word, covered, skip)
    items = []
    for symbol in answered:
        item = {"name": symbol["name"], "declaration": symbol["declaration"], "definition": symbol["definition"]}
        places = symbol["references"]
        if args.command == "refs":
            item["references"] = listed([f"{name}:{line}" for name, line, _ in places])
        elif args.command == "callers":
            item["references"] = len(places)
            item["callers"] = listed(enclosing(clangd, root, places))
        items.append(item)
    scope = "the index" if state["present"] else f"the translation unit of {rel(root, (root / args.file).resolve())}"
    if args.file and state["present"]:
        scope += f", narrowed to the symbols {args.file} uses or declares"
    data = {"command": args.command, "name": args.name, "scope": scope, "symbols": listed(items),
            "unresolved": {"label": UNRESOLVED, "lines": listed(unresolved)}}
    if len(asked) < len(answered) + len(others):
        data["uncovered"] = (f"unresolved took the references of {len(asked)} of the "
                             f"{len(answered) + len(others)} symbols named {word}")
    if unasked:
        data["unasked"] = f"{unasked} uses of {word} in {args.file} past the {OCCURRENCES} asked"
    if clangd.errors:
        data["clangd_errors"] = listed(clangd.errors)
    return data


def declaration(code, symbol):
    """A declaration's text from its range, up to its body or its end, a
    constructor's up to its initializers, its template's head before it."""
    start, end = symbol["range"]["start"], symbol["range"]["end"]
    text = "\n".join(code[start["line"]:end["line"] + 1])
    offset = start["character"]
    text = text[offset:] if end["line"] > start["line"] else text[offset:end["character"]]
    depth, out = 0, []
    for i, ch in enumerate(text):
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif depth == 0 and (ch in "{;" or (symbol.get("kind") == 9 and ch == ":" and ":" not in text[i - 1:i + 2:2])):
            break
        out.append(ch)
    head = code[start["line"] - 1].strip() if start["line"] else ""
    text = " ".join("".join(out).split())
    if head.startswith("template") and not text.startswith("template"):
        text = " ".join(head.split()) + " " + text
    return text[:DECLARATION]


def api(code, symbols):
    """api's items: each declaration outside a function, a nested
    namespace's parts once."""
    items = []
    for symbol, parents in flatten(symbols):
        if any(p.get("kind") in FUNCTIONS for p in parents):
            continue
        if (symbol.get("kind") == 3 and parents and parents[-1].get("kind") == 3
                and parents[-1]["range"]["start"]["line"] == symbol["range"]["start"]["line"]):
            continue
        container = "::".join(p["name"] for p in parents if p.get("kind") != 3)
        line = symbol["range"]["start"]["line"] + 1
        text = declaration(code, symbol)
        items.append(f"{line} {container}: {text}" if container else f"{line}: {text}")
    return items


def single(args, clangd_type, folder):
    """type and api: one file opened over the stripped database."""
    root = args.root
    path = (root / args.file).resolve()
    if not path.is_file():
        raise Refusal("failed", "NO_FILE", f"no file {args.file}", "give a file of the checkout")
    with clangd_type(args.clangd, folder, root) as clangd:
        lines = clangd.open(path)
        code = code_lines("\n".join(lines))
        if args.command == "api":
            symbols = clangd.request("textDocument/documentSymbol", {"textDocument": {"uri": uri(path)}})
            items = api(code, symbols)
            return "ok", "OK", "", {"command": "api", "file": rel(root, path), "declarations": listed(items)}
        word = args.name.split("::")[-1]
        number = args.line - 1
        cols = [col for col, _ in uses(code[number], word)] if number < len(code) else []
        if not cols:
            raise Refusal("failed", "NOT_ON_LINE", f"{word} is not used on {args.file}:{args.line}",
                          "give the line where the name is used")
        byte = column(lines, number, cols[0])
        hover = clangd.at("textDocument/hover", path, number, byte) or {}
        value = hover.get("contents", {})
        text = value.get("value", "") if isinstance(value, dict) else str(value)
        shown = [line for line in text.split("\n") if line.strip()]
        found = [line[len("Type: "):] for line in shown if line.startswith("Type: ")]
        kind = shown[0] if shown else None
        typed = found[0] if found else (shown[-1] if shown else None)
        data = {"command": "type", "at": f"{rel(root, path)}:{args.line}:{cols[0] + 1}", "name": word,
                "kind": kind, "type": typed, "hover": listed(shown)}
        return ("ok", "OK", "", data) if shown else ("missing", "NO_SYMBOL", "clangd gave no hover there", data)
