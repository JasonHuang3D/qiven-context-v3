"""One bounded answer: the lines a tool prints within its bound, the cut
named. The runner's answer (tools/changed.py), the debugger's transcript
(tools/debug.py) and the hang's stacks (debugger.py) are bounded by
it; the tests read a line's bound here.

- LINE_POINTS: a line's code points at most, the cut's marker within them.
- CUT: the cut's marker, "[cut]".
- clip(line): the line, its line ending dropped, within LINE_POINTS, a
  longer one cut with CUT.
- within(lines, room, cut=CUT, dropped=False): the lines within room lines,
  the first kept; past them, or where the caller left lines out already
  (dropped), the last place is the line cut, so that the cut is named
  within the bound.
"""

LINE_POINTS = 300
CUT = "[cut]"


def clip(line):
    """The line within LINE_POINTS code points, a longer one cut with CUT."""
    line = line.rstrip("\r\n")
    return line if len(line) <= LINE_POINTS else line[:LINE_POINTS - len(CUT)] + CUT


def within(lines, room, cut=CUT, dropped=False):
    """The lines within room lines: past them, or where dropped, the first
    room - 1 and then cut."""
    lines = list(lines)
    return lines if len(lines) <= room and not dropped else lines[:room - 1] + [cut]
