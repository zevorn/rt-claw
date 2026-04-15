# SPDX-License-Identifier: MIT
# MicroPython smoke test for the RT-Thread vexpress-a9 platform.

import os
import time
import unittest

from rtclaw_test import (
    RTClawQemuTest,
    exec_command_and_wait_for_pattern,
    wait_for_console_pattern,
)


@unittest.skipUnless(
    os.environ.get("RTCLAW_TEST_PLATFORM", "esp32c3-qemu")
    == "vexpress-a9-qemu",
    "MicroPython smoke test requires vexpress-a9-qemu",
)
class TestVexpressMicroPython(RTClawQemuTest):
    """Verify the RT-Thread MicroPython REPL starts and executes code."""

    def setUp(self):
        super().setUp()
        wait_for_console_pattern(
            self.console, "msh />", timeout=self.platform.boot_timeout
        )

    def test_python_repl_executes_code(self):
        """The python shell must execute a simple expression."""
        output = exec_command_and_wait_for_pattern(
            self.console,
            "python",
            ">>> ",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("MicroPython", output)
        self.assertIn(">>> ", output)
        time.sleep(1.0)

        start_offset = len(self.console.get_output())
        self.console.send("print(40 + 2)\r")
        output = wait_for_console_pattern(
            self.console,
            "42",
            timeout=self.platform.shell_timeout,
            start_offset=start_offset,
        )
        self.assertIn("42", output)
