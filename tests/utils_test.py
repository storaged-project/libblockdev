import unittest

from gi.repository import BlockDev
BlockDev.init(None)

class UtilsExecLoggingTest(unittest.TestCase):
    log = ""

    def my_log_func(self, level, msg):
        # not much to verify here
        self.assertTrue(isinstance(level, int))
        self.assertTrue(isinstance(msg, str))

        self.log += msg + "\n"

    def test_logging(self):
        """Verify that setting up and using exec logging works as expected"""

        succ, err = BlockDev.utils_init_logging(self.my_log_func)
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, err = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)
        self.assertIs(err, None)

        succ, out, err = BlockDev.utils_exec_and_capture_output(["echo", "hi"])
        self.assertTrue(succ)
        self.assertIs(err, None)
        self.assertEqual(out, "hi\n")

        self.assertIn("Running true", self.log)
        self.assertIn("Running echo hi", self.log)
