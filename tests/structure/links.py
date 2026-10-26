"""The link lines of a Ninja build, read from its build.ninja: each link
edge's output and the libraries of its LINK_LIBRARIES, in order.

CMake's Ninja generator writes each executable's and shared library's link
as an edge of a rule named <LANG>_<KIND>_LINKER__<target>..., its libraries
in the edge's LINK_LIBRARIES variable, the objects before them on the edge.
A library is a word of that variable that is not a flag: a path, or -l and
a name, or -framework and a name; a flag (a word starting with "-") is
not. Paths compare with "/" for "\\" and, for a .lib, without regard to
case, as Windows names files.

A library twice on one line is a finding (964): Apple's linker warns that
it ignores the second, and CI's macOS link makes its warnings errors.
"""

import re
from pathlib import Path

# The link edges: an executable's, a shared library's or a module's.
LINK_RULE = re.compile(r"^[A-Za-z]+_(?:EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY|SHARED_MODULE)_LINKER__")
# A word of a variable: a quoted run, or a run of non-blanks.
WORD = re.compile(r'"[^"]*"|\S+')


def unescape(text):
    """Ninja's escapes: "$$" a dollar sign, "$ " a blank, "$:" a colon."""
    return re.sub(r"\$([$ :])", r"\1", text)


def edges(text):
    """Each link edge of build.ninja's text, (its outputs, its variables)."""
    found = []
    current = None
    for line in text.splitlines():
        if line.startswith("build "):
            current = None
            head = line[len("build "):]
            match = re.match(r"((?:[^:$]|\$.)*):\s*(\S+)", head)
            if match and LINK_RULE.match(match.group(2)):
                outputs = re.findall(r"(?:[^\s$]|\$.)+", match.group(1))
                if "|" in outputs:
                    outputs = outputs[:outputs.index("|")]
                current = ([unescape(output) for output in outputs], {})
                found.append(current)
        elif current is not None and line.startswith(" ") and "=" in line:
            name, _, value = line.partition("=")
            current[1][name.strip()] = unescape(value.strip())
        elif line and not line.startswith(" "):
            current = None
    return found


def key(word):
    """A library's name for comparison, or None for a flag."""
    word = word.strip('"')
    if word.startswith("-l") and len(word) > 2:
        return word
    if word.startswith("-"):
        return None
    word = word.replace("\\", "/")
    return word.lower() if word.lower().endswith(".lib") else word


def libraries(value):
    """The libraries of a LINK_LIBRARIES value, in order: each word not a
    flag, -framework taking the word after it."""
    words = WORD.findall(value)
    out = []
    index = 0
    while index < len(words):
        word = words[index]
        if word == "-framework" and index + 1 < len(words):
            out.append("-framework " + words[index + 1])
            index += 2
            continue
        name = key(word)
        if name is not None:
            out.append(name)
        index += 1
    return out


def link_lines(text):
    """Each link line of build.ninja's text: {output: [library, ...]}."""
    lines = {}
    for outputs, variables in edges(text):
        for output in outputs:
            lines[output.replace("\\", "/")] = libraries(variables.get("LINK_LIBRARIES", ""))
    return lines


def twice(lines):
    """Each line naming a library more than once: {output: [library, ...]},
    each such library once, in the order of its first place."""
    found = {}
    for output, names in lines.items():
        seen, again = set(), []
        for name in names:
            if name in seen and name not in again:
                again.append(name)
            seen.add(name)
        if again:
            found[output] = again
    return found


def read(build):
    """The link lines of the Ninja build in the directory build."""
    return link_lines(Path(build, "build.ninja").read_text(encoding="utf-8"))
