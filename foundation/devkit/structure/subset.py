"""The safe subset's token rules, B18 to B31 but B26, as data, and their
checker, which rules.py's check() runs over v3's tree; and B26's half that reads no C++: the analysis configuration turns
pointer arithmetic off in B26's modules alone.

Each rule reads the tokens of rules.py's lexer: keywords, identifiers and
punctuators outside comments and literals, and outside the preprocessor's
directives, which B27 alone reads; B27 reads comments too. B25's objects and
B28's members are read by a statement's depth: each brace is a namespace's,
a class's or another's (a function's, an initializer's, an enum's). B18
holds in every .hpp and .cpp file; B27 in every file the analysis reads
(layers.Layers.analysed); the other rules of B19 to B31 in those of
in_scope(): by the tree's table (layers.py), each library's of core to
products, the test kit's whole and tests/support/, a vendor's apart; test
code, the table's (the top-level tests/ and a module's tests/), is a test's
for B24. What tokens cannot see, the analysis
preset's checks read (test_analysis.py). Each rule's blind spots, the
text's and then the tokens' own:
- B19: an owning raw pointer or C handle; B20: none of the text's, and
  neither tool sees placement new (test_analysis.py);
- B21: a C-style cast, and a pun by static_cast through void*;
- B22: a C variadic function's declaration or call, which tokens cannot tell
  from a pack or a fold; the tokens read SQLite's own variadic functions'
  calls by name, so that store/engine/ calls sqlite3_config alone;
- B23: a union's member read; B24: a library's own threads; a test's
  std::thread passes when a join is called in its block, whichever thread
  it joins;
- B25: a function's mutable local static; then an object at namespace
  scope or a static member whose first declarator holds parentheses (an
  initializer's, a function pointer's, alignas), or declared after a class
  body's brace;
- B28: a view in a lambda's capture, or behind a pointer or reference
  member, at any depth of its template arguments; then a member whose
  first declarator holds parentheses;
- B29: a std::unique_lock that locks at construction; SQLite's own waits;
- B30: none of the text's; then a bare time() call, read as any function's;
- B31: a library's own abort;
- for every rule, a name a macro hides; a macro of capitals is read as one.
"""

from fnmatch import fnmatchcase
from xml.etree import ElementTree

from foundation.devkit.structure import layers
from foundation.devkit.structure.lexer import lex

CXX = layers.CXX
B18_NAMES = frozenset(("dynamic_cast", "typeid"))
C_HEAP = frozenset(("malloc", "calloc", "realloc", "free", "_aligned_malloc", "_aligned_free"))
B20_NAMES = frozenset(("construct_at", "destroy_at"))
B21_NAMES = frozenset(("reinterpret_cast", "const_cast"))
B22_NAMES = frozenset(("va_list", "va_start", "va_arg", "va_copy", "va_end"))
# SQLite's variadic functions, called by name: sqlite3_config is store/engine/'s alone.
SQLITE_VARIADIC = frozenset(("sqlite3_config", "sqlite3_db_config", "sqlite3_vtab_config", "sqlite3_mprintf",
                             "sqlite3_snprintf", "sqlite3_str_appendf", "sqlite3_log", "sqlite3_test_control"))
B24_NAMES = frozenset(("CreateThread", "_beginthread", "_beginthreadex", "pthread_create"))
B28_VIEWS = frozenset(("string_view", "u8string_view", "u16string_view", "u32string_view", "wstring_view",
                       "basic_string_view", "span"))
B29_STD = frozenset(("mutex", "recursive_mutex", "shared_mutex", "lock_guard", "scoped_lock",
                     "condition_variable"))
B30_NAMES = frozenset(("system_clock", "utc_clock", "file_clock", "GetSystemTime", "GetSystemTimeAsFileTime",
                       "GetSystemTimePreciseAsFileTime", "GetLocalTime", "gettimeofday", "CLOCK_REALTIME"))
B31_CALLS = frozenset(("abort", "exit", "_exit", "_Exit", "quick_exit", "ExitProcess", "TerminateProcess",
                       "terminate"))
# A call's own name, bare: after one of these, or after one of the words.
CALL_AFTER = frozenset((";", "{", "}", "(", ",", "=", "?", ":", "!", "&&", "||", "+", "-", "[", "<<", "==",
                        "!=", "+=", "-="))
CALL_WORDS = frozenset(("return", "co_return", "throw", "else", "do", "case"))
# A statement that declares no object.
NOT_OBJECT = frozenset(("using", "typedef", "concept", "class", "struct", "enum", "union", "namespace",
                        "static_assert", "friend", "extern", "operator", "return", "asm"))

# Where each rule allows what it finds, by path prefix.
ALLOWED = {
    "B19": ("foundation/core/memory/include/qiven/memory/arena.", "foundation/core/memory/src/arena.", "foundation/core/memory/include/qiven/memory/pool.", "foundation/core/memory/src/pool."),
    "B20": ("foundation/core/memory/",),
    # The diagnostic entry's dump thread, which lives as long as the process.
    "B24": ("src/diag/",),
    # The entry's handlers' state, and memory/fail's kept cause, which the
    # terminate handler reads with nothing allocated.
    "B25": ("src/diag/", "foundation/core/memory/include/qiven/memory/fail.", "foundation/core/memory/src/fail."),
    "B30": ("foundation/system/platform/src/windows/", "foundation/system/platform/include/qiven/platform/windows/"),
    # memory/fail's fail-fast, and the diagnostic entry, which ends a broken
    # process itself after its report: B31's two homes.
    "B31": ("foundation/core/memory/include/qiven/memory/fail.", "foundation/core/memory/src/fail.", "src/diag/"),
}
# What the rows' cells allow by name, (rule, file, what), each with its
# reason; rules.py's allowed() reads them. B22: SQLite declares
# sqlite3_config variadic, the one place its interface needs varargs. B25:
# the runner's own cases' two state globals, each by its declarator's name,
# at namespace scope alone. B28: the borrowing types, each with its lifetime
# rule: Type is a type's members, Type::member that member alone.
RUNNER_TEST = "foundation/devkit/test/runner/tests/runner_test.cpp"
CELLS = {
    ("B22", "src/store/engine/", "sqlite3_config"): "SQLite's configuration, variadic in its interface",
    ("B25", RUNNER_TEST, "reached"): "whether a sample case went on past its failed REQUIRE: the sample, a plain "
                                     "case function that takes no state but its Context, sets it, and the case "
                                     "that runs the sample reads it",
    ("B25", RUNNER_TEST, "held_slots"): "as reached's: the pool slots a sample case holds past its end, chained, "
                                        "kept reachable under the system heap, which the case that runs it reads",
    ("B28", "foundation/devkit/test/runner/include/qiven/runner/core.hpp", "Case::name"): "a case's name, a view of "
        "the registering macro's string literals, which have static storage and outlive every Case",
    ("B28", "foundation/system/platform/include/qiven/platform/platform.hpp", "Command"): "the platform's request to start a process, its fields "
                                                     "views of the caller's text and buffers, passed by const "
                                                     "reference into start() or run() and used within that call",
    ("B28", "foundation/services/compute/include/qiven/compute/hal.hpp", "Launch"): "as Command's: the HAL's kernel request, passed into run()",
    ("B28", "foundation/core/memory/include/qiven/memory/arena.hpp", "Arena"): "the span of pages its owner lends, a platform Pages value that "
                                              "outlives the arena, which ends before its pages; memory sits "
                                              "below the platform and cannot hold that type",
    ("B28", "foundation/core/memory/include/qiven/memory/pool.hpp", "Pool"): "as Arena's",
    ("B28", "foundation/core/memory/include/qiven/memory/pool.hpp", "Heap"): "as Arena's",
    ("B28", "src/log/log.hpp", "Name"): "a literal's text, which lasts as long as the process: a Name is made in "
                                        "constant evaluation alone",
    ("B28", "src/log/ring.hpp", "Ring"): "the span its Log takes from its binary's arena, which outlives the Log, "
                                         "the ring's owner",
    ("B28", "src/log/log.hpp", "Log"): "the ring's span and the events' pages, blocks of its binary's arena, "
                                       "which outlives the Log and takes them back at its end",
}
# B26's modules, each with its own cases or fuzzed, where the analysis
# configuration may turn pointer arithmetic off; a directory's .clang-tidy
# serves a module that is a directory, memory alone.
B26_MODULES = ("foundation/core/memory/", "src/qiven3/digest.", "foundation/core/matcher/", "foundation/core/text/")
B26_TIDY = "cppcoreguidelines-pro-bounds-pointer-arithmetic"
B26_MSVC = "C26481"


def directive_lines(tokens, text):
    """The lines of each preprocessor directive, its continuations included."""
    lines, starts, last = set(), set(), 0
    for kind, value, line in tokens:
        if line != last and value == "#" and kind == "punct":
            starts.add(line)
        last = line
    rows = text.split("\n")
    for start in starts:
        line = start
        lines.add(line)
        while line <= len(rows) and rows[line - 1].rstrip("\r").endswith("\\"):
            line += 1
            lines.add(line)
    return lines


def angle_depths(toks):
    """Each token's depth of template angle brackets, and of parentheses."""
    depth, out = 0, []
    for _, value, _ in toks:
        if value in (">", ">>") and depth:
            depth = max(0, depth - len(value))
        out.append(depth)
        if value == "<":
            depth += 1
        elif value in ("(", "["):
            depth += 1
        elif value in (")", "]") and depth:
            depth -= 1
    return out


def strip_head(toks):
    """A statement past its template head, attributes, access label and inline."""
    i = 0
    while i < len(toks):
        value = toks[i][1]
        if value == "template" and i + 1 < len(toks) and toks[i + 1][1] == "<":
            depth, i = 0, i + 1
            while i < len(toks):
                depth += {"<": 1, ">": -1, ">>": -2}.get(toks[i][1], 0)
                i += 1
                if depth <= 0:
                    break
        elif value == "[" and i + 1 < len(toks) and toks[i + 1][1] == "[":
            while i < len(toks) and not (toks[i][1] == "]" and i + 1 < len(toks) and toks[i + 1][1] == "]"):
                i += 1
            i += 2
        elif value in ("public", "private", "protected") and i + 1 < len(toks) and toks[i + 1][1] == ":":
            i += 2
        elif value in ("inline", "export"):
            i += 1
        else:
            break
    return toks[i:]


def class_name(toks):
    """The name a class head declares: its first identifier but a macro's, an
    attribute's or alignas's."""
    i = 1
    while i < len(toks):
        kind, value, _ = toks[i]
        if kind == "id" and i + 1 < len(toks) and toks[i + 1][1] == "(":
            depth, i = 0, i + 1
            while i < len(toks):
                depth += {"(": 1, ")": -1}.get(toks[i][1], 0)
                i += 1
                if depth == 0:
                    break
            continue
        if kind == "id" and value not in ("final", "alignas"):
            return value
        i += 1
    return ""


def opening(head, before):
    """A brace's scope, by the statement before it: (kind, name, declares, keep).
    kind is namespace, class or other; declares, that the head declares an
    object initialized by the brace; keep, that the statement goes on past it
    (a constructor's member initializer)."""
    toks = strip_head(head)
    values = [t[1] for t in toks]
    if not values:
        return "other", "", False, False
    if values[0] == "namespace" or (values[0] == "extern" and len(toks) > 1 and toks[1][0] == "str"):
        return "namespace", "", False, False
    if values[0] == "enum":
        return "other", "", False, False
    if values[0] in ("class", "struct", "union") and "=" not in values:
        return "class", class_name(toks), False, False
    if "=" in values:
        return "other", "", True, False
    if "(" in values:
        member_init = any(a == ")" and b == ":" for a, b in zip(values, values[1:]))
        keep = member_init and before is not None and (before[0] == "id" or before[1] == ">")
        return "other", "", False, keep
    return "other", "", True, False


def statements(tokens):
    """Each declaration at a namespace's or a class's own depth: (scope, name,
    tokens), the tokens of its braces' initializers out; a brace's declaration
    goes on past it to a further declarator's comma."""
    stack, head = [("namespace", "", False, [], False)], []
    for i, tok in enumerate(tokens):
        value = tok[1]
        scope, name = stack[-1][0], stack[-1][1]
        if tok[0] == "punct" and value == "{":
            kind, inner, declares, keep = opening(head, tokens[i - 1] if i else None)
            stack.append((kind, inner, keep, head, declares))
            head = []
        elif tok[0] == "punct" and value == "}":
            if len(stack) > 1:
                _, _, keep, saved, declares = stack.pop()
                head = saved if keep else []
                if declares and i + 1 < len(tokens) and tokens[i + 1][1] == ",":
                    head = saved
                elif declares and stack[-1][0] != "other":
                    yield stack[-1][0], stack[-1][1], saved
        elif tok[0] == "punct" and value == ";":
            if scope != "other" and head:
                yield scope, name, head
            head = []
        else:
            head.append(tok)


def drop_macros(toks):
    """A statement without its macros' calls: a name of capitals, digits and _
    before (, with its arguments, as QIVEN_GUARDED_BY(m)."""
    out, i = [], 0
    while i < len(toks):
        kind, value, _ = toks[i]
        if kind == "id" and value.upper() == value and any(c.isalpha() for c in value) \
                and i + 1 < len(toks) and toks[i + 1][1] == "(":
            depth, i = 0, i + 1
            while i < len(toks):
                depth += {"(": 1, ")": -1}.get(toks[i][1], 0)
                i += 1
                if depth == 0:
                    break
            continue
        out.append(toks[i])
        i += 1
    return out


def declarators(toks):
    """A declaration's declarators, each its tokens before its initializer,
    the first with the declaration's specifiers: split at a comma outside
    parentheses, brackets and, before an initializer, template arguments. A
    comma in an initializer's template arguments splits too, which can only
    add a finding."""
    parts, part, depth, init = [], [], 0, False
    for tok in toks:
        value = tok[1]
        if value == "=" and depth == 0:
            init = True
        elif value == "," and depth == 0:
            parts.append(part)
            part, init = [], False
        elif not init:
            part.append(tok)
        depth += {"(": 1, "[": 1, ")": -1, "]": -1}.get(value, 0)
        if not init:
            depth = max(0, depth + {"<": 1, ">": -1, ">>": -2}.get(value, 0))
    return [*parts, part]


def named(toks, depths):
    """The index of the name a declarator declares: its last identifier
    before an array's bound or a parenthesis of its own."""
    end = next((i for i, t in enumerate(toks) if t[1] in ("[", "(") and depths[i] == 0), len(toks))
    return next((i for i in reversed(range(end)) if toks[i][0] == "id"), 0)


def is_const(toks, depths):
    """A declaration's object const: constexpr, or a const at depth 0 after its last *."""
    if any(t[1] == "constexpr" for t in toks):
        return True
    last_const = max((i for i, t in enumerate(toks) if t[1] == "const" and depths[i] == 0), default=-1)
    last_star = max((i for i, t in enumerate(toks) if t[1] == "*" and depths[i] == 0), default=-1)
    return last_const > last_star


def behind(toks, depths, i):
    """Whether a pointer or reference holds the view at i, or a type around
    it: a *, & or && after it at its own depth or an enclosing one, and not
    in a later argument of the same list. A view at any depth of a member's
    template arguments is held by value otherwise: in a container or a
    wrapper, as std::array<std::string_view, 4>."""
    level, later = depths[i], False
    for (_, value, _), depth in zip(toks[i + 1:], depths[i + 1:]):
        if depth < level:
            level, later = depth, False
        if depth == level and value == ",":
            later = True
        elif depth == level and not later and value in ("*", "&", "&&"):
            return True
    return False


def object_rules(tokens, out):
    """B25's objects at namespace scope and static data members; B28's view
    members: a finding for each declarator, the name an allowance matches its
    own, what a static member never takes, an allowance being a namespace
    scope object's alone."""
    for scope, name, head in statements(tokens):
        parts = declarators(drop_macros(strip_head(head)))
        toks, values = parts[0], [t[1] for t in parts[0]]
        if not toks or (toks[0][0] != "id" and values[0] != "::") or NOT_OBJECT & set(values):
            continue
        if "(" in values:
            continue
        if sum(1 for part in parts for t in part if t[0] == "id") < 2:
            continue
        depths = angle_depths(toks)
        at = named(toks, depths)
        specifiers = toks[:next((i for i in range(at) if depths[i] == 0 and values[i] in ("*", "&", "&&")), at)]
        for part in filter(None, parts):
            whole = toks if part is toks else [*specifiers, *part]
            depths = angle_depths(whole)
            declared, line = whole[named(whole, depths)][1], part[0][2]
            mutable = not is_const(whole, depths)
            if scope == "namespace" and mutable:
                out("B25", "object", line, f"a non-const object at namespace scope, {declared}", declared)
            if scope == "class" and "static" in values and mutable:
                out("B25", "static", line, f"a non-const static data member, {declared}")
            if scope == "class" and "static" not in values and not name.endswith("View"):
                views = [i for i, t in enumerate(whole) if t[1] in B28_VIEWS and not behind(whole, depths, i)]
                if views:
                    out("B28", "", line, f"a {whole[views[0]][1]} member of {name or 'a nameless type'}, {declared}",
                        f"{name}::{declared}")


def is_call(tokens, i):
    """tokens[i] called bare, or through std:: or the global ::."""
    if i + 1 >= len(tokens) or tokens[i + 1][1] != "(":
        return False
    prev = tokens[i - 1] if i else None
    if prev is None or (prev[0] == "punct" and prev[1] in CALL_AFTER) or prev[1] in CALL_WORDS:
        return True
    if prev[1] == "::":
        qualifier = tokens[i - 2] if i >= 2 else None
        return qualifier is None or qualifier[1] == "std" or not (qualifier[0] == "id" or qualifier[1] == ">")
    return False


def after_std(tokens, i):
    return i >= 2 and tokens[i - 1][1] == "::" and tokens[i - 2][1] == "std"


def joined(tokens, i):
    """A join called through . or -> after tokens[i], before its block ends."""
    depth = 0
    for j in range(i + 1, len(tokens) - 1):
        value = tokens[j][1]
        if value == "{":
            depth += 1
        elif value == "}":
            if depth == 0:
                return False
            depth -= 1
        elif value == "join" and tokens[j - 1][1] in (".", "->") and tokens[j + 1][1] == "(":
            return True
    return False


def silences(rest):
    """Whether a pragma's tokens turn findings off: warning's disable,
    suppress or push to level 0, or clang's diagnostic ignored."""
    words = [word for word in rest if word != "("]
    if words[:1] == ["warning"]:
        return bool({"disable", "suppress"} & set(words)) or words[1:4] == ["push", ",", "0"]
    return words[:3] == ["clang", "diagnostic", "ignored"]


def token_rules(tokens, comments, skip, subset, analysed, test, out):
    for text, line in comments:
        words = "".join(c if c.isalnum() or c == "_" else " " for c in text).split()
        if analysed and any(w.startswith("NOLINT") for w in words):
            out("B27", "nolint", line, "NOLINT in a comment")
    for i, (kind, value, line) in enumerate(tokens):
        prev = tokens[i - 1][1] if i else ""
        nxt = tokens[i + 1][1] if i + 1 < len(tokens) else ""
        if line in skip:
            if analysed and value == "pragma" and prev == "#":
                rest = [t[1] for t in tokens[i + 1:i + 8] if t[2] == line]
                if silences(rest):
                    out("B27", "pragma", line, "#pragma " + " ".join(rest))
            continue
        if kind != "id":
            continue
        if value in B18_NAMES:
            out("B18", "", line, value)
        if analysed and value == "suppress" and prev == "::" and i >= 2 and tokens[i - 2][1] == "gsl":
            out("B27", "suppress", line, "gsl::suppress")
        elif analysed and value in ("__pragma", "_Pragma") and silences([t[1] for t in tokens[i + 1:i + 8]]):
            out("B27", "pragma", line, value + " turning findings off")
        if not subset:
            continue
        if value == "new":
            if prev == "operator":
                out("B19", "operator", line, "operator new")
            elif nxt == "(":
                out("B20", "placement", line, "placement new")
            else:
                out("B19", "new", line, "new")
        elif value == "delete" and prev != "=":
            out("B19", "operator" if prev == "operator" else "delete", line,
                "operator delete" if prev == "operator" else "delete")
        elif value in C_HEAP and is_call(tokens, i):
            out("B19", "c heap", line, value + " called")
        elif value in B20_NAMES:
            out("B20", "construct", line, value)
        elif value in B21_NAMES:
            out("B21", "", line, value)
        elif value in B22_NAMES:
            out("B22", "", line, value)
        elif value in SQLITE_VARIADIC and is_call(tokens, i):
            out("B22", "call", line, value + " called, a variadic function", value)
        elif value == "union":
            out("B23", "", line, "union")
        elif value == "detach":
            out("B24", "detach", line, "detach")
        elif value in B24_NAMES:
            out("B24", "create", line, value)
        elif value == "thread" and after_std(tokens, i) and not (test and joined(tokens, i)):
            out("B24", "thread", line, "std::thread, joined in no block of a test's" if test
                else "std::thread, where core's threads are std::jthread")
        elif value == "thread_local":
            out("B25", "thread_local", line, "thread_local")
        elif value in B29_STD and after_std(tokens, i):
            out("B29", "", line, "std::" + value)
        elif value == "lock" and nxt == "(" and i + 2 < len(tokens) and tokens[i + 2][1] == ")" \
                and (prev in (".", "->") or is_call(tokens, i)):
            out("B29", "lock", line, "lock() with no deadline")
        elif value == "INFINITE":
            out("B29", "infinite", line, "INFINITE")
        elif value in B30_NAMES or (value == "time" and is_call(tokens, i) and prev == "::"):
            out("B30", "", line, value)
        elif value == "assert" and nxt == "(":
            out("B31", "", line, "assert")
        elif value in B31_CALLS and is_call(tokens, i) and (value != "terminate" or after_std(tokens, i)):
            out("B31", "", line, value)


def tidy_checks(text):
    """The entries of a .clang-tidy file's Checks, in their order."""
    entries, inside = [], False
    for row in text.splitlines():
        stripped = row.split("#", 1)[0].strip()
        if not row[:1].isspace() and ":" in stripped:
            key, _, value = stripped.partition(":")
            inside = key.strip() == "Checks"
            stripped = value.strip()
        if inside:
            entries += [e.strip(" '\"") for e in stripped.replace(">-", "").split(",")]
    return [e for e in entries if e]


def tidy_inherits(text):
    """A .clang-tidy file's InheritParentConfig, true or not."""
    for row in text.splitlines():
        key, _, value = row.split("#", 1)[0].partition(":")
        if not row[:1].isspace() and key.strip() == "InheritParentConfig":
            return value.strip(" '\"").lower() == "true"
    return False


def config_rules(root, files, add):
    """B26's configuration: a .clang-tidy whose Checks leave pointer
    arithmetic's check off, outside a module of B26's that is a directory;
    and a rule set that turns C26481 off, for every file. A file that
    inherits its parent's configuration starts from the parent's checks,
    which the parent's own file is read for; one that does not starts from
    clang-tidy's defaults, where the check is off."""
    for rel in files:
        name = rel.rsplit("/", 1)[-1]
        text = (root / rel).read_bytes().decode("utf-8", "replace")
        if name.endswith(".clang-tidy"):
            on = tidy_inherits(text)
            for entry in tidy_checks(text):
                if fnmatchcase(B26_TIDY, entry.lstrip("-")):
                    on = not entry.startswith("-")
            where = rel[:-len(name)]
            if not on and not (where and any(m.endswith("/") and where.startswith(m) for m in B26_MODULES)):
                add("B26", rel, "tidy", f"{B26_TIDY} off outside B26's modules", None)
        elif name.endswith(".ruleset"):
            for rule in ElementTree.fromstring(text.lstrip("﻿")).iter("Rule"):
                if rule.get("Id") == B26_MSVC and rule.get("Action") == "None":
                    add("B26", rel, "ruleset", f"{B26_MSVC} off for every file", None)


def in_scope(rel, tree):
    """Whether B19 to B31 hold in rel, by the tree's table (layers.Layers):
    a library's file of core to products, a file of the test kit, or one of
    tests/support/; a vendored file never."""
    return rel not in layers.VENDORED and (
        tree.library(rel) or rel.startswith(layers.KIT) or tree.as_tests(rel).startswith(layers.SUPPORT))


def check(root, files, add, tree):
    """B18 to B31 but B26 over a tree's files, and B26's configuration;
    add(rule, where, kind, detail, what) takes each finding, what the name
    an allowance matches: B22's function's, B25's object's, B28's member's;
    tree, the tree's table (layers.Layers), gives the subset's files and
    test code."""
    config_rules(root, files, add)
    for rel in files:
        if not rel.endswith(CXX):
            continue
        text = (root / rel).read_bytes().decode("utf-8", "replace")
        tokens, comments = lex(text)
        subset = in_scope(rel, tree)

        def out(rule, kind, line, detail, what=None, rel=rel):
            add(rule, rel, kind, f"line {line}: {detail}", what)

        skip = directive_lines(tokens, text)
        token_rules(tokens, comments, skip, subset, tree.analysed(rel), tree.test_code(rel), out)
        if subset:
            object_rules([t for t in tokens if t[2] not in skip], out)
