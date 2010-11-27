import select26
import select
import unittest

class TestSelect26(unittest.TestCase):

    def test_alias(self):
        self.assert_(select26.select is select.select)
        self.assert_(select26.error is select.error)
        if hasattr(select, "poll"):
            self.assert_(select26.poll is select.poll)


def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestSelect26))
    return suite

if __name__ == "__main__":
    unittest.main(defaultTest="test_suite")

