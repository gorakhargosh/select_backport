import select_backport
import select
import unittest

class TestSelectBackport(unittest.TestCase):

    def test_alias(self):
        self.assert_(select_backport.select is select.select)
        self.assert_(select_backport.error is select.error)
        if hasattr(select, "poll"):
            self.assert_(select_backport.poll is select.poll)


def test_suite():
    suite = unittest.TestSuite()
    suite.addTest(unittest.makeSuite(TestSelectBackport))
    return suite

if __name__ == "__main__":
    unittest.main(defaultTest="test_suite")

