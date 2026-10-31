"""The platform's memory checks, each defined once over an adapter
(contract/platform_cases.py names the binding's part): each operation of
the drive's platform half within its stated budget by the arena's count;
a budget a byte short of its peak, MemoryError budget and nothing held;
and, under QIVEN_FAULTS, its fault sweep: each allocation on its arena,
and each platform call, the fake's and the Windows adapter's win/files
and win/facts calls, failed in turn.

The budgets are the text's for package 1: 64 KiB for each member, read's
plus the file's size, run's plus its caps; pages(n) has none of its own.
"""

import itertools
import sys

from tests.drive.session import debug_library, has_faults

KIB = 1 << 10
MEMBER = 64 * KIB
MIB = 1 << 20
# A file read whole, and its size.
DATA = bytes(range(256)) * 12


def operations(case):
    """Each operation with a function giving its arguments on this
    adapter: an absolute path under the files' root, a program that runs
    where the adapter has processes, a text stdout() can write before the
    reply's line (none on an adapter writing to the drive's own stream)."""
    root = case.files_root()
    data = root / "data.bin"
    data.write_bytes(DATA)
    fresh = itertools.count()
    argv = [sys.executable, "-I", "-c", "print(1)"] if "processes" in case.TAGS else ["none"]
    text = "a\u00eb" if case.ADAPTER == "fake" else ""

    def emptied():
        made = root / f"emptied-{next(fresh)}"
        made.mkdir()
        return {"path": str(made)}

    listed = root / "listed"
    listed.mkdir(exist_ok=True)
    for name in ("a-name-past-a-short-string's-buffer.pending", "b.tmp"):
        (listed / name).write_bytes(b"x")

    def renamed():
        made = root / f"renamed-{next(fresh)}.tmp"
        made.write_bytes(b"x")
        return {"path": str(made), "name": made.stem + ".pending"}

    return (
        ("name", lambda: {}),
        ("config_dir", lambda: {"create": False}),
        ("resolve", lambda: {"path": str(root)}),
        ("normcase", lambda: {"path": "Q:/Qiven3-Normcase/A Path Past A Short String's Buffer"}),
        ("facts", lambda: {"path": str(root)}),
        ("synced", lambda: {"path": str(root)}),
        ("replace", lambda: {"path": str(root / "replaced.txt"), "data": "00"}),
        ("create", lambda: {"path": str(root / f"made-{next(fresh)}.txt")}),
        ("remove_directory", emptied),
        ("list", lambda: {"path": str(listed)}),
        ("remove", lambda: {"path": str(root / "none-there")}),
        ("rename", renamed),
        ("read", lambda: {"path": str(data), "size": len(DATA)}),
        ("program", lambda: {"name": "a-program-on-no-path-entry"}),
        ("run", lambda: {"argv": argv, "caps": {"out": KIB, "err": KIB}}),
        ("code_page", lambda: {}),
        ("stdout", lambda: {"text": text}),
        ("residual", lambda: {"paths": [str(root)]}),
        ("random_bytes", lambda: {"n": 64}),
        ("wipe", lambda: {"n": 32}),
        ("now", lambda: {}),
        ("pages", lambda: {"n": 1}),
    )


def stated(op, args):
    """The operation's stated budget for these arguments."""
    if op == "pages":
        return 0
    if op == "read":
        return MEMBER + args["size"]
    if op == "run":
        return MEMBER + args["caps"]["out"] + args["caps"]["err"]
    return MEMBER


def give_back(drive, op, reply):
    """Frees the pages a pages request took."""
    if op == "pages" and reply.get("ok"):
        drive.call("pages_free", id=reply["result"]["id"])


class PlatformMemory:
    def test_budgets(self):
        """Each operation within its stated budget by the count, nothing
        left on its arena and no page held after; then with a budget a byte
        short of its peak, MemoryError budget, nothing held."""
        with self.drive() as d:
            for op, args in operations(self):
                with self.subTest(op=op):
                    given = args()
                    reply = d.reply(op, **given)
                    # pages(1) holds its page until it is freed.
                    self.assertEqual(reply["held"], 4096 if op == "pages" else 0, reply)
                    give_back(d, op, reply)
                    memory = reply["memory"]
                    self.assertEqual(memory["budget"], stated(op, given), reply)
                    self.assertLessEqual(memory["peak"], memory["budget"], reply)
                    self.assertEqual(memory["left"], 0, reply)
                    if memory["peak"] == 0 or debug_library():
                        continue
                    short = d.reply(op, budget=memory["peak"] - 1, **args())
                    self.assertFalse(short["ok"], short)
                    self.assertEqual((short["error"]["kind"], short["error"]["code"]), ("memory", "budget"), short)
                    self.assertEqual((short["memory"]["left"], short["held"]), (0, 0), short)

    def test_adapter_for(self):
        """adapter_for, the call current() makes, takes no arena of the
        drive's and holds nothing after."""
        with self.drive() as d:
            reply = d.reply("adapter_for", system="linux")
            self.assertNotIn("memory", reply)
            self.assertEqual(reply["held"], 0)

    def test_faults(self):
        """Under QIVEN_FAULTS, each operation's K-th allocation, and its
        K-th platform call, failed for K from 1 until it completes: each
        failed run a typed error, or for facts' and residual's platform
        calls the failed read unknown (session.unknown_read), nothing held,
        the drive answering; at completion K - 1 calls counted. Built without it, a
        fault is refused, not applicable."""
        with self.drive() as d:
            if not has_faults():
                reply = d.reply("now", fault={"k": 1, "of": "allocation"})
                self.assertEqual(reply["error"]["kind"], "request", reply)
                self.assertTrue(reply["error"]["detail"].startswith("not applicable:"), reply)
                self.skipTest("not applicable: built without QIVEN_FAULTS")
            if debug_library():
                self.skipTest("not applicable: MSVC's debug library allocates in a noexcept constructor")
            kinds = ("allocation", "platform")
            for op, args in operations(self):
                for of in kinds:
                    with self.subTest(op=op, of=of):
                        k, reply = d.sweep(op, of, **args())
                        give_back(d, op, reply)
                        self.assertEqual(reply["fault"]["calls"], k - 1, reply)
