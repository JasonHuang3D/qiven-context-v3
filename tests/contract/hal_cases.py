"""The HAL's cases H1 to H5 and H8, each defined once over a compute
binding (contract/test_hal_fake.py): drive(), a drive whose HAL the case
gives its device set by hal_fake, and drive_once(requests), the drive run on
those requests alone. H6 is the build's (contract/test_hal_build.py); H7
comes with the first feature that builds a back end.
"""

import json
import signal
import struct
import sys

from tests.drive.session import DriveError

GIB = 1 << 30
GPU = {"id": "gpu0", "back_end": "cuda", "name": "a GPU of 12 GiB", "memory": 12 * GIB}
CPU = {"id": "cpu0", "back_end": "cpu", "name": "the CPU", "memory": 1 << 20}


def int64s(values):
    return struct.pack(f"={len(values)}q", *values).hex()


class HalCases:
    def hal_failure(self, d, op, code, **args):
        with self.assertRaises(DriveError) as caught:
            d.call(op, **args)
        error = caught.exception
        self.assertEqual((error.kind, error.code), ("hal", code), error)
        return error

    def test_H1_facts(self):
        """The fake given 4 threads, AVX2 and one GPU of 12 GiB: facts() the same."""
        with self.drive() as d:
            d.call("hal_fake", threads=4, vectors=["avx2"], devices=[GPU])
            self.assertEqual(d.call("hal_facts"),
                             {"cpu_threads": 4, "vectors": ["avx2"], "devices": [GPU]})

    def test_H2_gap(self):
        """No GPU: device("gpu") a gap, no_device; no back end: device("cuda")
        a gap, not_built. A gap is a value, not an error."""
        with self.drive() as d:
            d.call("hal_fake", threads=1, devices=[CPU])
            self.assertEqual(d.call("hal_device", capability="gpu"), {"gap": {"reason": "no_device"}})
            self.assertEqual(d.call("hal_device", capability="cuda"), {"gap": {"reason": "not_built"}})
            self.assertEqual(d.call("hal_device", capability="cpu"), {"device": CPU})

    def test_H3_memory(self):
        """allocate past the device's memory: memory; upload of a span of
        another size: size."""
        with self.drive() as d:
            d.call("hal_fake", devices=[GPU])
            self.hal_failure(d, "hal_allocate", "memory", device=GPU, size=12 * GIB + 1)
            buffer = d.call("hal_allocate", device=GPU, size=16)
            self.hal_failure(d, "hal_upload", "size", buffer=buffer, data="00" * 8)
            self.hal_failure(d, "hal_download", "size", buffer=buffer, size=24)
            self.assertEqual(d.call("hal_counts"),
                             {"allocations": 1, "uploads": 0, "downloads": 0, "runs": 0})

    def test_H4_kernel(self):
        """run of a name no kernel has: kernel."""
        with self.drive() as d:
            d.call("hal_fake", devices=[CPU])
            error = self.hal_failure(d, "hal_run", "kernel", device=CPU, kernel="no_such_kernel")
            self.assertIn("no_such_kernel", error.detail)
            self.assertEqual(d.call("hal_counts")["runs"], 0)

    def test_H5_reference(self):
        """A registered kernel adding two vectors of 1,000 int64 on the fake:
        its CPU reference's result."""
        a = [7 * i - 3_000 for i in range(1000)]
        b = [(-1) ** i * i * i * 1_000_003 for i in range(1000)]
        size = 8 * len(a)
        with self.drive() as d:
            d.call("hal_fake", threads=2, devices=[CPU])
            buffers = [d.call("hal_allocate", device=CPU, size=size) for _ in range(3)]
            d.call("hal_upload", buffer=buffers[0], data=int64s(a))
            d.call("hal_upload", buffer=buffers[1], data=int64s(b))
            d.call("hal_run", device=CPU, kernel="add_i64", buffers=buffers, grid=[1000, 1, 1])
            out = bytes.fromhex(d.call("hal_download", buffer=buffers[2]))
            self.assertEqual(list(struct.unpack(f"={len(a)}q", out)), [x + y for x, y in zip(a, b)])
            self.assertEqual(d.call("hal_counts"),
                             {"allocations": 3, "uploads": 2, "downloads": 1, "runs": 1})

    def test_H8_a_callers_mistake(self):
        """Through the drive, upload into a buffer another fake gave: the
        drive's process ends at once through memory::fail_fast, exit status
        0xC0000409 on Windows, SIGABRT elsewhere, the entry's report naming
        the fail-fast first; no reply to it or after it. The
        control, the drive's own buffer, is answered and the drive ends 0."""
        def requests(other):
            return ({"op": "hal_fake", "devices": [CPU]},
                    {"op": "hal_allocate", "device": CPU, "size": 8, "other": other},
                    {"op": "hal_upload", "buffer": 1, "data": "00" * 8},
                    {"op": "hal_counts"})

        control = self.drive_once(*requests(False))
        self.assertEqual(control.status, 0, control)
        self.assertEqual([json.loads(line)["ok"] for line in control.out.splitlines()], [True] * 4)

        ran = self.drive_once(*requests(True))
        if sys.platform == "win32":
            self.assertEqual(ran.status, 0xC0000409, ran)
        else:
            self.assertEqual(ran.status, -signal.SIGABRT, ran)
        replies = [json.loads(line) for line in ran.out.splitlines()]
        self.assertEqual([(r["ok"], r.get("result")) for r in replies], [(True, None), (True, 1)])
        self.assertTrue(ran.err.startswith(b"fatal: fail-fast: "), ran.err[:200])
