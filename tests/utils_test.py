import unittest
import re

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

        match = re.search(r'Running \[(\d+)\] true', self.log)
        self.assertIsNot(match, None)
        task_id1 = match.group(1)
        match = re.search(r'Running \[(\d+)\] echo hi', self.log)
        self.assertIsNot(match, None)
        task_id2 = match.group(1)

        self.assertIn("...done [%s]" % task_id1, self.log)
        self.assertIn("...done [%s]" % task_id2, self.log)

        # reset logging -> nothing more should appear in the log
        succ, err = BlockDev.utils_init_logging(None)
        self.assertTrue(succ)
        self.assertIs(err, None)

        old_log = self.log
        succ, err = BlockDev.utils_exec_and_report_error(["true"])
        self.assertTrue(succ)
        self.assertIs(err, None)
        self.assertEqual(old_log, self.log)
