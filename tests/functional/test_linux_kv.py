# SPDX-License-Identifier: MIT
# KV persistence tests for Linux native platform.

from rtclaw_test import (
    RTClawLinuxTest,
    exec_command_and_wait_for_pattern,
)


class TestLinuxKvPersistence(RTClawLinuxTest):
    """Test that long-term memory persists across process restarts."""

    def test_ltm_survives_restart(self):
        """'/remember' data must survive kill + restart."""
        self.wait_for_shell_prompt()

        exec_command_and_wait_for_pattern(
            self.console, "/remember persist_key persist_value",
            "Remembered:",
            timeout=self.platform.shell_timeout,
        )

        self.restart()
        self.wait_for_shell_prompt()

        output = exec_command_and_wait_for_pattern(
            self.console, "/memories", "persist_key",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("persist_key", output)
        self.assertIn("persist_value", output)

    def test_multiple_ltm_entries(self):
        """Multiple '/remember' entries must all persist."""
        self.wait_for_shell_prompt()

        exec_command_and_wait_for_pattern(
            self.console, "/remember alpha first_val",
            "Remembered:",
            timeout=self.platform.shell_timeout,
        )
        exec_command_and_wait_for_pattern(
            self.console, "/remember beta second_val",
            "Remembered:",
            timeout=self.platform.shell_timeout,
        )

        self.restart()
        self.wait_for_shell_prompt()

        output = exec_command_and_wait_for_pattern(
            self.console, "/memories", "alpha",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("alpha", output)

        output = exec_command_and_wait_for_pattern(
            self.console, "/memories", "beta",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("beta", output)

    def test_forget_persists(self):
        """'/forget' removal must persist across restarts."""
        self.wait_for_shell_prompt()

        exec_command_and_wait_for_pattern(
            self.console, "/remember temp_key temp_val",
            "Remembered:",
            timeout=self.platform.shell_timeout,
        )
        exec_command_and_wait_for_pattern(
            self.console, "/forget temp_key", "Forgot:",
            timeout=self.platform.shell_timeout,
        )

        self.restart()
        self.wait_for_shell_prompt()

        output = exec_command_and_wait_for_pattern(
            self.console, "/memories", "0/16 entries",
            timeout=self.platform.shell_timeout,
        )
        self.assertNotIn("temp_key", output)

    def test_ai_config_persists(self):
        """'/ai_set' config must persist across restarts."""
        self.wait_for_shell_prompt()

        exec_command_and_wait_for_pattern(
            self.console,
            "/ai_set model persistent-model-42",
            "Model saved:",
            timeout=self.platform.shell_timeout,
        )

        self.restart()
        self.wait_for_shell_prompt()

        output = exec_command_and_wait_for_pattern(
            self.console, "/ai_status", "Model:",
            timeout=self.platform.shell_timeout,
        )
        self.assertIn("persistent-model-42", output)
