"""S12's peak, defined once over a store, through the fake's drive started
with --store: mixed into the slow group's bindings
(slow/test_store_sqlite.py, slow/test_store_memory.py) with the contract
cases' frame (contract/store_cases.py's StoreBinding). Its 20,000 entries
are past the fast group's minute under the thread sanitizer: it runs in the
full suite only. S12's other parts stay in contract/store_more.py.
"""

from tests.contract.store_entries import SCOPE, Maker, canonical, header, writer_of

KIB = 1024
# S12's 20,000 entries of 1 KiB on the in-memory store: its state's heap.
HEAP = 128 * KIB * KIB
# The engine's budget a connection: 2,000 KiB of page cache and 1 MiB.
ENGINE_BUDGET = 2000 * KIB + KIB * KIB


class StorePeak:
    def test_S12_peak(self):
        """S12: an export of 20,000 entries of 1 KiB bodies (at most; made
        and appended in the drive by its fill) read whole in the drive: its
        20,000 lines; the reads' arenas, the caller's and the export's own,
        under 2 MiB [proposal] (F80); on SQLite the engine heap's growth
        over the reads, its page cache's slots included, within the
        engine's budget a connection: the slots alone pass 2 MiB once the
        export's cache fills (notes). Needs: none."""
        with self.drive() as drive:
            self.needs(drive)
            m, store = Maker(drive), drive.store_make(heap=HEAP)
            probe = m.claim(SCOPE, "implementer", 1, None, 0, text="")
            content = m.content("A")
            content["claim"] = "Entry {seq}: " + "f" * (KIB - len(probe[1]) - len("Entry 00000: "))
            writer = writer_of("implementer", header(m.fixture("A"))["writer"])
            done = store.fill(SCOPE, "implementer", 20000, canonical(content), "{seq}", writer)
            self.assertEqual((done["appended"], done["head"]["seq"]), (20000, 20000))
            self.assertEqual(len(store.body(done["head"]["id"])), KIB)
            x = store.export()
            drained = x.drain()
            x.close()
            self.assertEqual(drained["count"], 20000)
            self.assertLess(drained["peak"] + drained.get("own", 0), 2 * KIB * KIB)
            self.assertLessEqual(drained["engine_most"] - drained["engine_before"], ENGINE_BUDGET)
