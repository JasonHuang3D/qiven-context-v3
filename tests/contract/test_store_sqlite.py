"""The store's cases on the SQLite store, through qiven3-drive-fake
--store sqlite: every system of the CI matrix.

SQLite declares durable, files and processes, and is the store of
"own: sqlite"'s cases.
"""

import unittest

from tests.contract.store_cases import StoreBinding, StoreCases
from tests.contract.store_more import StoreMore


class Sqlite(StoreBinding, StoreCases, StoreMore, unittest.TestCase):
    STORE = "sqlite"
    CAPABILITIES = {"durable": True, "files": True, "processes": True}
    ctest_labels = ("fake",)
