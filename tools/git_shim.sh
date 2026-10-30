#!/bin/sh
# The counting shim on Linux (tools/loop_shim.py places a copy
# first on PATH under git's program name): each start appends one line, its
# arguments, to the file QIVEN3_SHIM_LOG names, then runs the program
# QIVEN3_SHIM_REAL names in its place. A start with either variable unset,
# or that cannot log, exits 125 with a line on standard error, so that no
# start goes uncounted. Not a part of the product: the loop's own tool.
if [ -z "$QIVEN3_SHIM_LOG" ] || [ -z "$QIVEN3_SHIM_REAL" ]; then
    echo "shim: QIVEN3_SHIM_LOG or QIVEN3_SHIM_REAL unset" >&2
    exit 125
fi
line=$(printf '%s' "$*" | tr '\r\n' '  ')  # one line a start, written by one append
printf '%s %s\n' "$PWD" "$line" >> "$QIVEN3_SHIM_LOG" || {
    echo "shim: the log cannot be written" >&2
    exit 125
}
exec "$QIVEN3_SHIM_REAL" "$@"
