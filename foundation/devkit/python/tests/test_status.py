"""status: a status off the table is named unnamed, in the form of its kind."""

import unittest

from foundation.devkit.python import status


class Unnamed(unittest.TestCase):
    ctest_labels = ("fake",)

    def test_off_the_table(self):
        """A system status in hexadecimal, a signal by its number."""
        self.assertEqual(status.line(0xC0000001), "status: 0xC0000001 unnamed")
        self.assertEqual(status.line(-31), "status: signal 31 unnamed")


if __name__ == "__main__":
    unittest.main()
