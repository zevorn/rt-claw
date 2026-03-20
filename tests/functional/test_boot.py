# SPDX-License-Identifier: MIT
# Boot and banner tests for all platforms.

from rtclaw_test import RTClawQemuTest


class TestBoot(RTClawQemuTest):
    """Verify QEMU boots and the rt-claw banner appears."""

    def test_banner(self):
        """Boot banner must appear."""
        self.wait_for_boot()

    def test_shell_ready(self):
        """Shell prompt must appear (ESP32 platforms only)."""
        self.wait_for_shell_prompt()
