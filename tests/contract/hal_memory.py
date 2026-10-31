"""The HAL's memory checks, defined once over a compute binding
(contract/hal_cases.py names its part): each member, host side, within
the 64 KiB budget by its arena's count, a budget a byte short of its peak
MemoryError budget, and, under QIVEN_FAULTS, each allocation on its
arena failed in turn. The fake HAL makes no platform call.
"""

from tests.contract.hal_cases import CPU, GPU, int64s
from tests.drive.session import debug_library, has_faults

MEMBER = 64 << 10
SIZE = 8 * 64


def operations(d):
    """Each member with its arguments on the drive d, whose fake has the
    CPU and the GPU: buffers made for those that take one."""
    d.call("hal_fake", threads=2, devices=[CPU, GPU])
    a, b, out, freed = (d.call("hal_allocate", device=CPU, size=SIZE) for _ in range(4))
    data = int64s(list(range(SIZE // 8)))
    return (
        ("hal_facts", {}),
        ("hal_device", {"capability": "gpu"}),
        ("hal_allocate", {"device": GPU, "size": 16}),
        ("hal_upload", {"buffer": a, "data": data}),
        ("hal_download", {"buffer": a}),
        ("hal_run", {"device": CPU, "kernel": "add_i64", "buffers": [a, b, out], "grid": [SIZE // 8, 1, 1]}),
        ("hal_free", {"buffer": freed}),
    )


class HalMemory:
    def test_hal_budgets(self):
        """Each member within 64 KiB by the count, nothing left; a budget a
        byte short of its peak, MemoryError budget."""
        with self.drive() as d:
            for op, args in operations(d):
                with self.subTest(op=op):
                    reply = d.reply(op, **args)
                    self.assertTrue(reply["ok"], reply)
                    memory = reply["memory"]
                    self.assertEqual(memory["budget"], MEMBER)
                    self.assertLessEqual(memory["peak"], MEMBER)
                    self.assertEqual(memory["left"], 0)
                    if memory["peak"] == 0 or op == "hal_free" or debug_library():
                        continue
                    short = d.reply(op, budget=memory["peak"] - 1, **args)
                    self.assertEqual((short["error"]["kind"], short["error"]["code"]), ("memory", "budget"), short)

    def test_hal_faults(self):
        """Under QIVEN_FAULTS each member's K-th allocation failed for K
        from 1 until it completes: each failed run a typed error, nothing
        held, the drive answering."""
        if not has_faults():
            self.skipTest("not applicable: built without QIVEN_FAULTS")
        if debug_library():
            self.skipTest("not applicable: MSVC's debug library allocates in a noexcept constructor")
        with self.drive() as d:
            for op, args in operations(d):
                with self.subTest(op=op):
                    k, reply = d.sweep(op, "allocation", **args)
                    self.assertTrue(reply["ok"], reply)
                    self.assertEqual(reply["fault"]["calls"], k - 1, reply)
