# SPDX-License-Identifier: MIT
# Base test class for rt-claw Linux native functional tests.

import os
import shutil
import signal
import subprocess
import tempfile
import unittest

from rtclaw_test.cmd import ConsoleBuffer, wait_for_console_pattern
from rtclaw_test.platform import PROJECT_ROOT, BUILD_DIR


LINUX_BINARY = os.path.join(BUILD_DIR, "linux", "platform", "linux", "rtclaw")


class RTClawLinuxTest(unittest.TestCase):
    """
    Base class for rt-claw Linux native functional tests.

    Manages the native subprocess lifecycle:
    - setUp: create isolated KV dir, launch binary with pipe I/O
    - tearDown: terminate process, clean up temp files
    """

    console: ConsoleBuffer
    _proc: subprocess.Popen
    _tmpdir: str
    _kv_dir: str

    BOOT_MARKER = "rt-claw"
    SHELL_PROMPT = "type /help for commands"
    BOOT_TIMEOUT = 15
    SHELL_TIMEOUT = 10

    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(LINUX_BINARY):
            raise unittest.SkipTest(
                f"Linux binary not found: {LINUX_BINARY} "
                f"(run 'make build-linux' first)"
            )

    def setUp(self):
        self._tmpdir = tempfile.mkdtemp(prefix="rtclaw-linux-test-")
        self._kv_dir = os.path.join(self._tmpdir, "kv")
        os.makedirs(self._kv_dir, exist_ok=True)
        self.console = self._launch()

    def tearDown(self):
        self._shutdown()

        if hasattr(self, "_outcome"):
            result = self._outcome.result
            if result and (result.failures or result.errors):
                self._dump_log()

        if os.path.isdir(self._tmpdir):
            shutil.rmtree(self._tmpdir, ignore_errors=True)

    def _launch(self) -> ConsoleBuffer:
        """Start the Linux native binary and return a ConsoleBuffer."""
        env = os.environ.copy()
        env["HOME"] = self._tmpdir
        env["TERM"] = "dumb"

        self._proc = subprocess.Popen(
            [LINUX_BINARY],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            env=env,
        )

        return ConsoleBuffer(self._proc)

    def _shutdown(self):
        """Terminate: SIGTERM -> wait(5s) -> SIGKILL -> close pipes."""
        if not hasattr(self, "_proc"):
            return

        if self._proc.poll() is None:
            self._proc.send_signal(signal.SIGTERM)
            try:
                self._proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._proc.kill()
                self._proc.wait(timeout=3)

        if self._proc.stdin:
            self._proc.stdin.close()
        if self._proc.stdout:
            self._proc.stdout.close()

    def restart(self):
        """Kill and restart the process (for persistence tests)."""
        self._shutdown()
        self.console = self._launch()

    def wait_for_boot(self, timeout=0):
        """Wait for the boot banner to appear."""
        if timeout <= 0:
            timeout = self.BOOT_TIMEOUT
        wait_for_console_pattern(
            self.console, self.BOOT_MARKER, timeout
        )

    def wait_for_shell_prompt(self, timeout=0):
        """Wait for the shell prompt to appear."""
        if timeout <= 0:
            timeout = self.BOOT_TIMEOUT
        wait_for_console_pattern(
            self.console, self.SHELL_PROMPT, timeout
        )

    def _dump_log(self):
        """Print console output for debugging failed tests."""
        output = (
            self.console.get_output() if hasattr(self, "console") else ""
        )
        if output:
            print(f"\n--- Linux output ---")
            print(output[-3000:])
            print("--- end ---")
