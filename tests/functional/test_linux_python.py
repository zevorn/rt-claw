# SPDX-License-Identifier: MIT
# Python command tests for Linux native platform.

from rtclaw_test import (
    RTClawLinuxTest,
    exec_command_and_wait_for_pattern,
)


class TestLinuxPython(RTClawLinuxTest):
    """Test direct Python execution on Linux native platform."""

    def setUp(self):
        super().setUp()
        self.wait_for_shell_prompt()

    def test_python_command_executes_code(self):
        """'/python' must execute a simple expression."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/python print(40 + 2)", "42",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("42", output)
