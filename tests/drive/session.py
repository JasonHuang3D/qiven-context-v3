"""Drives qiven3-drive: a request a line in, a reply a line out.

A request is the operation's name and its arguments; the reply's result
comes back, or its typed error is raised as DriveError. Bytes cross as
hexadecimal and come back as bytes here; a deadline in milliseconds.
"""

import json
import os
import subprocess
import threading
from pathlib import Path

from tests.drive import binaries

DRIVE = "qiven3-drive"


class DriveError(Exception):
    """A reply's error: kind "platform" or "hal" with code, operation and
    detail; "request" or "exception" with detail alone."""

    def __init__(self, error):
        super().__init__(json.dumps(error, sort_keys=True))
        self.kind = error.get("kind")
        self.code = error.get("code")
        self.operation = error.get("operation")
        self.detail = error.get("detail", "")


def wire(request):
    """A request's canonical line."""
    return (json.dumps(request, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def build_option(name, build=None):
    """A cache variable of a build directory (QIVEN3_BUILD's by default), or
    None when the build's cache does not set it."""
    build = Path(build) if build else binaries.directory("QIVEN3_BUILD")
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(name + ":"):
            return line.split("=", 1)[1].strip()
    return None


def has_hal(build=None):
    """Whether the build has the HAL: QIVEN3_HAL is on unless set off."""
    value = build_option("QIVEN3_HAL", build)
    return value is None or value.upper() not in ("OFF", "0", "FALSE", "NO", "N")


class Drive:
    """One qiven3-drive process; a context manager that ends it."""

    def __init__(self, *args, env=None, program=None, timeout=binaries.TIMEOUT):
        self.program = Path(program) if program else binaries.path(DRIVE)
        self.process = subprocess.Popen(
            [str(self.program), *map(str, args)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env)
        # A drive that hangs is killed, so the read below ends.
        self.watchdog = threading.Timer(timeout, self.process.kill)
        self.watchdog.daemon = True
        self.watchdog.start()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def close(self):
        """Ends the drive: its exit status and standard error."""
        try:
            if self.process.stdin and not self.process.stdin.closed:
                self.process.stdin.close()
            err = self.process.stderr.read() if self.process.stderr else b""
            status = self.process.wait(timeout=binaries.TIMEOUT)
        finally:
            self.watchdog.cancel()
            for stream in (self.process.stdout, self.process.stderr):
                if stream:
                    stream.close()
        return status, err

    def reply(self, op, **args):
        """The whole reply to one request."""
        self.process.stdin.write(wire({"op": op, **args}))
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line.endswith(b"\n"):
            raise RuntimeError(f"{DRIVE} ended without a reply to {op}: {line!r}")
        return json.loads(line)

    def call(self, op, **args):
        """The result, or None when the operation gives none; DriveError."""
        reply = self.reply(op, **args)
        if not reply.get("ok"):
            raise DriveError(reply["error"])
        return reply.get("result")

    # The platform's operations used by the cases, bytes as bytes.
    def config_dir(self, create=False):
        return self.call("config_dir", create=create)

    def random_bytes(self, n):
        return bytes.fromhex(self.call("random_bytes", n=n))

    def now(self):
        return self.call("now")

    def code_page(self):
        return self.call("code_page")


def environment(**changes):
    """This process's environment with each change: a value set, None removed."""
    env = dict(os.environ)
    for name, value in changes.items():
        if value is None:
            env.pop(name, None)
        else:
            env[name] = value
    return env


def once(args, *requests, env=None):
    """Runs the drive on these requests alone: its status and raw bytes."""
    data = b"".join(wire(r) for r in requests)
    done = subprocess.run([str(binaries.path(DRIVE)), *map(str, args)], input=data,
                          capture_output=True, env=env, timeout=binaries.TIMEOUT, check=False)
    return binaries.Ran(done.returncode, done.stdout, done.stderr)
