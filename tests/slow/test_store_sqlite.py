"""The slow group's store cases on the SQLite store, through
qiven3-drive-fake --store sqlite: the full suite only, never the fake
preset (labelled drive, not fake).

Its store and capabilities are contract/test_store_sqlite.py's binding's.
"""

import unittest

from tests.contract import test_store_sqlite as contract
from tests.contract.store_cases import StoreBinding
from tests.slow.store_peak import StorePeak


class Sqlite(StoreBinding, StorePeak, unittest.TestCase):
    STORE = contract.Sqlite.STORE
    CAPABILITIES = contract.Sqlite.CAPABILITIES
    ctest_labels = ("drive",)
