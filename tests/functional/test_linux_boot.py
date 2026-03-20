# SPDX-License-Identifier: MIT
# Boot and banner tests for Linux native platform.

from rtclaw_test import RTClawLinuxTest


class TestLinuxBoot(RTClawLinuxTest):
    """Verify the Linux native binary boots and shows the banner."""

    def test_banner(self):
        """Boot banner must appear."""
        self.wait_for_boot()

    def test_shell_prompt(self):
        """Shell prompt must appear after boot."""
        self.wait_for_shell_prompt()
