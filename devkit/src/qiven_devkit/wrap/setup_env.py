"""The environment a setup script leaves, captured once per key and kept
between runs in the caller's directory; its fake. See this directory's
README.md.
"""

import hashlib
import json
import os
import re
import secrets
from pathlib import Path

from qiven_devkit.wrap import call
from qiven_devkit.wrap.fake import Fake

CAPTURE = "setup capture"
_WORD_REFUSED = set(" \t\r\n\"%!&|<>^()")
_PATH_REFUSED = set("\"%!\r\n")


class Lacks(call.CallError):
    """The environment a setup left does not carry what the caller asked."""

    answer = "lacks"


class SetupEnvironment:
    """Captures what a setup script leaves, by `shell`, under `limit`
    seconds, and keeps it in the directory `store`."""

    def __init__(self, shell, store, limit):
        self.shell, self.store, self.limit = shell, Path(store), limit

    def environment(self, script, args, base, carries, inputs=()):
        """The whole environment `script` with `args` leaves under `base`,
        kept or captured; Lacks where it does not carry `carries`."""
        args, inputs = [str(arg) for arg in args], [Path(path).absolute() for path in inputs]
        script = Path(script).absolute()
        shell = call.locate(CAPTURE, self.shell, base)
        for word in args:
            if _WORD_REFUSED & set(word) or not word:
                raise ValueError(f"{CAPTURE}: a word the call cannot pass to the script as it is: {word!r}")
        for path in (shell, str(script)):
            if _PATH_REFUSED & set(path):
                raise ValueError(f"{CAPTURE}: a path the call cannot write into its driver: {path!r}")
        key = _key(shell, script, args, base, inputs)
        self.store.mkdir(parents=True, exist_ok=True)
        kept = self.store / f"{key}.json"
        environment = _read(kept, key)
        if environment is None:
            environment = self._capture(shell, script, args, base)
            _write(kept, {"key": key, "script": str(script), "args": args, "environment": environment})
        _check(environment, carries, script, args)
        return environment

    def _capture(self, shell, script, args, base):
        """What the script leaves, read from a second shell's `set`."""
        driver = self.store / f"capture-{secrets.token_hex(8)}.cmd"
        line = " ".join((f'@call "{script}"', *args, "1>&2"))
        driver.write_bytes("\r\n".join((line, '@if not "%ERRORLEVEL%"=="0" exit /b %ERRORLEVEL%',
                                        f'@"{shell}" /d /u /c set', "")).encode("oem"))
        try:
            answer = call.call(CAPTURE, shell, ["/d", "/c", f".\\{driver.name}"], env=base, cwd=self.store,
                               limit=self.limit, said="oem")
        finally:
            driver.unlink(missing_ok=True)
        try:
            text = answer.out.decode("utf-16-le").removeprefix("\N{BYTE ORDER MARK}")
        except UnicodeDecodeError as error:
            raise call.Unreadable(CAPTURE, f"{script.name}: its environment is not UTF-16: {error}") from None
        environment = {}
        for line in text.splitlines():
            name, equals, value = line.partition("=")
            if equals and name:
                environment[name] = value
        if not environment:
            raise call.Unreadable(CAPTURE, f"{script.name} {' '.join(args)}: the shell printed no environment")
        return environment


class FakeSetupEnvironment(Fake):
    """SetupEnvironment's fake: answered by call name; no shell started."""

    def environment(self, script, args, base, carries, inputs=()):
        return self._answer(CAPTURE, script=script, args=list(args), base=base, carries=carries,
                            inputs=list(inputs))


def _key(shell, script, args, base, inputs):
    """The SHA-256 of what a capture's answer rests on, as JSON."""
    rests = {"shell": shell, "script": str(script), "script digest": _digest(script), "args": args,
             "inputs": [[str(path), _digest(path)] for path in inputs],
             "base": sorted([str(name), str(value)] for name, value in base.items())}
    return hashlib.sha256(json.dumps(rests, ensure_ascii=True).encode("ascii")).hexdigest()


def _digest(path):
    """The SHA-256 of a file's bytes; Missing where there is no file."""
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except FileNotFoundError:
        raise call.Missing(CAPTURE, f"no file at {path}") from None


def _read(kept, key):
    """The environment kept under key, or None where none reads back whole."""
    try:
        held = json.loads(kept.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    environment = held.get("environment") if isinstance(held, dict) and held.get("key") == key else None
    if not isinstance(environment, dict) or not environment or not all(
            isinstance(name, str) and isinstance(value, str) for name, value in environment.items()):
        return None
    return environment


def _write(kept, held):
    """held written to kept whole: to a file beside it, which then replaces it."""
    beside = kept.with_name(f"{kept.stem}-{secrets.token_hex(8)}.tmp")
    try:
        beside.write_text(json.dumps(held, ensure_ascii=False, indent=1, sort_keys=True), encoding="utf-8")
        os.replace(beside, kept)
    finally:
        beside.unlink(missing_ok=True)


def _check(environment, carries, script, args):
    """Lacks where environment does not carry each {name: pattern} of carries."""
    folded = {name.upper(): value for name, value in environment.items()}
    for name, pattern in carries.items():
        value = folded.get(name.upper())
        if value is None or not re.fullmatch(pattern, value):
            held = "no such variable" if value is None else f"it holds {value!r}"
            raise Lacks(CAPTURE, f"{script.name} {' '.join(args)}: {name} does not match {pattern!r}: {held}")
