"""The store's cases on the in-memory reference store, through
qiven3-drive-fake --store memory: every system of the CI matrix.

The in-memory store declares none of durable, files and processes: each
case needing one is not applicable here, never passed.
"""

import unittest

from tests.contract.store_cases import StoreBinding, StoreCases
from tests.contract.store_more import StoreMore


class Memory(StoreBinding, StoreCases, StoreMore, unittest.TestCase):
    STORE = "memory"
    CAPABILITIES = {"durable": False, "files": False, "processes": False}
    ctest_labels = ("fake",)
