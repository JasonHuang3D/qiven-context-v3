"""The store's cases and the ruled store's (checked_cases.py) on the
in-memory reference store, through qiven3-drive-fake --store memory: every
system of the CI matrix.

The in-memory store declares none of durable, files and processes: each
case needing one is not applicable here, never passed.
"""

import unittest

from tests.contract.checked_cases import CheckedCases
from tests.contract.store_cases import StoreBinding, StoreCases
from tests.contract.store_more import StoreMore
from tests.contract.store_rest import StoreRest


class Memory(StoreBinding, StoreCases, StoreMore, StoreRest, CheckedCases, unittest.TestCase):
    STORE = "memory"
    CAPABILITIES = {"durable": False, "files": False, "processes": False}
    ctest_labels = ("fake",)
