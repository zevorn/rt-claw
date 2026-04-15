# SPDX-License-Identifier: MIT
# Shell command tests for Linux native platform.

from rtclaw_test import (
    RTClawLinuxTest,
    exec_command_and_wait_for_pattern,
)


class TestLinuxShell(RTClawLinuxTest):
    """Test shell commands via piped stdin/stdout."""

    def setUp(self):
        super().setUp()
        self.wait_for_shell_prompt()

    def test_help(self):
        """'/help' must list available commands."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/help",
            "Anything else is sent directly to AI.",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("/help", output)
        self.assertIn("/log", output)
        self.assertIn("/remember", output)
        self.assertIn("/quit", output)

    def test_log_off(self):
        """'/log off' must disable log output."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/log off", "Log output: OFF",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("OFF", output)

    def test_log_on(self):
        """'/log on' must enable log output."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/log on", "Log output: ON",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("ON", output)

    def test_log_level_debug(self):
        """'/log level debug' must set log level."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/log level debug", "Log level: debug",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("debug", output)

    def test_log_level_error(self):
        """'/log level error' must set log level."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/log level error", "Log level: error",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("error", output)

    def test_history_empty(self):
        """'/history' must show 0 messages initially."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/history", "messages",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("0 messages", output)

    def test_clear(self):
        """'/clear' must clear conversation memory."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/clear", "cleared",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("cleared", output)

    def test_remember_and_memories(self):
        """'/remember' saves, '/memories' lists it."""
        exec_command_and_wait_for_pattern(
            self.console, "/remember mykey myvalue",
            "Remembered:",
            timeout=self.platform.shell_timeout,
        )
        output = exec_command_and_wait_for_pattern(
            self.console, "/memories", "mykey",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("mykey", output)

    def test_forget(self):
        """'/forget' removes a long-term memory."""
        exec_command_and_wait_for_pattern(
            self.console, "/remember forgetme tempval",
            "Remembered:",
            timeout=self.platform.shell_timeout,
        )
        output = exec_command_and_wait_for_pattern(
            self.console, "/forget forgetme", "Forgot:",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("forgetme", output)

    def test_ai_status(self):
        """'/ai_status' must show Model field."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/ai_status", "Model:",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("Model:", output)
        self.assertIn("API URL:", output)

    def test_ai_set_model(self):
        """'/ai_set model' must update the model."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/ai_set model test-model-123",
            "Model saved:",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("test-model-123", output)
        output = exec_command_and_wait_for_pattern(
            self.console, "/ai_status", "Model:",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("test-model-123", output)

    def test_ai_set_url(self):
        """'/ai_set url' must update the API URL."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/ai_set url https://test.example.com/v1",
            "API URL saved:",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("https://test.example.com/v1", output)

    def test_ip(self):
        """'/ip' must show network info."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/ip", "Network:",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("Network:", output)

    def test_unknown_command(self):
        """Unknown /command must show error message."""
        output = exec_command_and_wait_for_pattern(
            self.console, "/nonexistent_cmd_xyz", "Unknown command",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("Unknown command", output)

    def test_quit(self):
        """'/quit' must cause the process to exit."""
        self.console.send("/quit\n")
        try:
            self._proc.wait(timeout=5)
        except Exception:
            self.fail("Process did not exit after /quit")
        self.assertIsNotNone(self._proc.returncode)
