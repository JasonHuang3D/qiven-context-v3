"""The navigation aid's client of clangd (tools/nav.py): clangd found, run
over a compilation database, and spoken to by the language server protocol
on its standard input and output, one process a call.

- clangd: QIVEN3_CLANGD, else LLVM's under ProgramFiles on Windows, else
  the one on PATH. Absent, the call refuses with CLANGD_ABSENT.
- Each request waits at most WAIT seconds, a file's first parse within
  them; a request clangd answers with an error is answered None, its
  message kept in errors.
- Positions are in UTF-8 bytes (the protocol's positionEncodings), lines
  and columns 0-based on the wire, 1-based in every answer of nav.py.

Loaded by tools/nav.py by runpy.run_path: it imports nothing of this
directory.
"""

import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
from pathlib import Path

WAIT = 300  # seconds a request waits at most
CLOSE = 30  # seconds shutdown and exit wait at most
INDEX_TOKEN = "backgroundIndexProgress"


class Refusal(Exception):
    """A call nav.py refuses: its envelope's status, code, message and
    remedy."""

    def __init__(self, status, code, message, next_action):
        super().__init__(message)
        self.status, self.code, self.next_action = status, code, next_action


def find_clangd():
    """clangd's path: QIVEN3_CLANGD, else LLVM's under ProgramFiles on
    Windows, else PATH's."""
    given = os.environ.get("QIVEN3_CLANGD")
    if given:
        path = Path(given)
    elif sys.platform == "win32":
        path = Path(os.environ.get("ProgramFiles", "")) / "LLVM" / "bin" / "clangd.exe"
    else:
        found = shutil.which("clangd")
        path = Path(found) if found else None
    if path is None or not path.is_file():
        raise Refusal("not_ready", "CLANGD_ABSENT", f"clangd is missing{': ' + str(path) if path else ' from PATH'}",
                      "name clangd by QIVEN3_CLANGD")
    return path


def clangd_version(clangd):
    """clangd's version, from --version, or None."""
    try:
        text = subprocess.run([str(clangd), "--version"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for word in text.split():
        if word[:1].isdigit() and word.count(".") == 2:
            return word
    return None


def uri(path):
    return urllib.parse.urljoin("file:", urllib.request.pathname2url(os.path.abspath(path)))


def path_of(location_uri):
    return Path(urllib.request.url2pathname(urllib.parse.urlparse(location_uri).path))


class Clangd:
    """One clangd over the database's directory; a context manager that
    shuts it down."""

    def __init__(self, clangd, database_dir, root, background=False):
        argv = [str(clangd), f"--compile-commands-dir={database_dir}",
                f"--background-index={'true' if background else 'false'}", "--clang-tidy=false",
                "--enable-config=false", "--header-insertion=never", "--limit-results=0",
                "--limit-references=0", "--log=error"]
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                     cwd=str(root))
        self.inbox = queue.Queue()
        self.next_id = 0
        self.errors = []
        self.progress = []  # the background index's progress values, in order
        self.opened = {}  # path: its text's lines
        threading.Thread(target=self._read, daemon=True).start()
        self.request("initialize", {
            "processId": os.getpid(), "rootUri": uri(root),
            "capabilities": {"general": {"positionEncodings": ["utf-8"]},
                             "window": {"workDoneProgress": True},
                             "textDocument": {"documentSymbol": {"hierarchicalDocumentSymbolSupport": True},
                                              "hover": {"contentFormat": ["plaintext"]}}}})
        self.notify("initialized", {})

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _read(self):
        out = self.proc.stdout
        try:
            while True:
                length = None
                while True:
                    line = out.readline()
                    if not line:
                        return
                    if line in (b"\r\n", b"\n"):
                        break
                    name, _, value = line.partition(b":")
                    if name.strip().lower() == b"content-length":
                        length = int(value)
                if length is None:
                    return
                self.inbox.put(json.loads(out.read(length)))
        except (OSError, ValueError):
            return
        finally:
            self.inbox.put(None)

    def send(self, message):
        data = json.dumps({"jsonrpc": "2.0", **message}).encode("utf-8")
        try:
            self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data) + data)
            self.proc.stdin.flush()
        except OSError as e:
            raise Refusal("failed", "CLANGD_FAILED", f"clangd stopped: {e}", "run the call again") from e

    def notify(self, method, params):
        self.send({"method": method, "params": params})

    def take(self, end):
        """The next message by end (time.monotonic()), the server's own
        requests answered and its notifications kept."""
        while True:
            try:
                message = self.inbox.get(timeout=max(0.0, end - time.monotonic()))
            except queue.Empty:
                return None
            if message is None:
                self.inbox.put(None)
                raise Refusal("failed", "CLANGD_FAILED", f"clangd ended ({self.proc.poll()})", "run the call again")
            method = message.get("method")
            if method is None:
                return message
            if "id" in message:
                self.send({"id": message["id"], "result": None})
            elif method == "$/progress" and message["params"].get("token") == INDEX_TOKEN:
                self.progress.append(message["params"].get("value", {}))

    def request(self, method, params, wait=WAIT):
        self.next_id += 1
        ident = self.next_id
        self.send({"id": ident, "method": method, "params": params})
        end = time.monotonic() + wait
        while True:
            message = self.take(end)
            if message is None:
                raise Refusal("failed", "CLANGD_TIMEOUT", f"clangd gave no answer to {method} in {wait} s",
                              "narrow the call by --file, or run it again")
            if message.get("id") == ident:
                if "error" in message:
                    self.errors.append(f"{method}: {message['error'].get('message', '')}"[:200])
                    return None
                return message.get("result")

    def open(self, path):
        """The file opened in clangd, once; its lines."""
        path = Path(path)
        if path not in self.opened:
            text = path.read_text(encoding="utf-8", errors="replace")
            language = "c" if path.suffix == ".c" else "cpp"
            self.notify("textDocument/didOpen", {"textDocument": {"uri": uri(path), "languageId": language,
                                                                  "version": 1, "text": text}})
            self.opened[path] = text.split("\n")
        return self.opened[path]

    def at(self, method, path, line, column, **extra):
        """A request at a position, 0-based, of a file it opens."""
        self.open(path)
        return self.request(method, {"textDocument": {"uri": uri(path)},
                                     "position": {"line": line, "character": column}, **extra})

    def wait_index(self, wait, loaded_only):
        """Wait for the background index: with loaded_only, until its first
        task, the shards' loading, is done; else until it ends and stays
        ended for a second. Whether it got there within wait seconds."""
        end = time.monotonic() + wait
        quiet = None
        while time.monotonic() < end:
            done = self.index_state()
            if loaded_only and done[0] >= 1:
                return True
            if not loaded_only and done[1]:
                quiet = quiet or time.monotonic() + 1.0
                if time.monotonic() >= quiet:
                    return True
            else:
                quiet = None
            message = self.take(min(end, time.monotonic() + 0.25))
            if message is not None:
                continue
        return False

    def index_state(self):
        """(tasks done, ended) by the last progress value clangd sent."""
        if not self.progress:
            return 0, False
        last = self.progress[-1]
        if last.get("kind") == "end":
            return 1 << 30, True
        done = str(last.get("message", "")).split("/")[0]
        return (int(done) if done.isdigit() else 0), False

    def close(self):
        if self.proc.poll() is None:
            try:
                self.request("shutdown", None, wait=CLOSE)
                self.notify("exit", None)
                self.proc.wait(timeout=CLOSE)
            except (Refusal, subprocess.TimeoutExpired):
                self.proc.kill()
                self.proc.wait(timeout=CLOSE)
        for stream in (self.proc.stdin, self.proc.stdout):
            try:
                stream.close()
            except OSError:
                pass
