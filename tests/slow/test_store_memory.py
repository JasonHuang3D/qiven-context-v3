"""The slow group's store cases on the in-memory reference store, through
qiven3-drive-fake --store memory: the full suite only, never the fake
preset (labelled drive, not fake).

Its store and capabilities are contract/test_store_memory.py's binding's.
"""

import unittest

from tests.contract import test_store_memory as contract
from tests.contract.store_cases import StoreBinding
from tests.slow.store_peak import StorePeak


class Memory(StoreBinding, StorePeak, unittest.TestCase):
    STORE = contract.Memory.STORE
    CAPABILITIES = contract.Memory.CAPABILITIES
    ctest_labels = ("drive",)
