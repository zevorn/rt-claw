# SPDX-License-Identifier: MIT
# Console buffer and interaction functions for QEMU subprocess I/O.

import os
import re
import threading
import time

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def _strip_ansi(text: str) -> str:
    return _ANSI_RE.sub("", text)


class ConsoleBuffer:
    """
    Wraps a subprocess.Popen with background stdout reader.

    A background thread continuously reads lines from QEMU's stdout
    and appends them to an internal buffer. The main thread polls
    this buffer for pattern matching.
    """

    def __init__(self, proc):
        self.proc = proc
        self._output = ""
        self._lock = threading.Lock()
        self._closed = False
        self._reader = threading.Thread(
            target=self._read_loop, daemon=True
        )
        self._reader.start()

    def _read_loop(self):
        try:
            fd = self.proc.stdout.fileno()
            while True:
                raw = os.read(fd, 256)
                if not raw:
                    break
                text = _strip_ansi(raw.decode("utf-8", errors="replace"))
                with self._lock:
                    self._output += text
        except (ValueError, OSError):
            pass
        finally:
            with self._lock:
                self._closed = True

    def get_lines(self):
        with self._lock:
            return self._output.splitlines(keepends=True)

    def get_output(self):
        with self._lock:
            return self._output

    @property
    def is_alive(self):
        return self.proc.poll() is None

    def send(self, text: str):
        if self.proc.stdin:
            self.proc.stdin.write(text.encode("utf-8"))
            self.proc.stdin.flush()


def wait_for_console_pattern(console: ConsoleBuffer,
                             pattern: str,
                             timeout: float = 60.0,
                             start_offset: int = 0) -> str:
    """
    Wait until `pattern` appears in console output.

    Returns the full output up to (and including) the matching line.
    Raises TimeoutError if not found within timeout seconds.
    Raises RuntimeError if QEMU exits before pattern is found.
    """
    deadline = time.monotonic() + timeout
    settle_deadline = 0.0
    matched_len = -1

    while True:
        output = console.get_output()
        pos = output.find(pattern, start_offset)
        if pos >= 0:
            end = pos + len(pattern)
            newline = output.find("\n", end)

            if newline >= 0:
                return output[:newline + 1]

            if len(output) != matched_len:
                matched_len = len(output)
                settle_deadline = time.monotonic() + 0.2
            elif time.monotonic() >= settle_deadline:
                return output

        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"Pattern {pattern!r} not found within {timeout}s.\n"
                f"--- Console output ({len(output.splitlines())} lines) ---\n"
                f"{output[-2000:]}"
            )

        if not console.is_alive:
            output = console.get_output()
            raise RuntimeError(
                f"QEMU exited (rc={console.proc.returncode}) "
                f"before pattern {pattern!r} was found.\n"
                f"--- Console output ---\n"
                f"{output[-2000:]}"
            )

        time.sleep(0.1)


def exec_command(console: ConsoleBuffer, command: str):
    """Send a command string followed by newline."""
    console.send(command + "\n")


def exec_command_and_wait_for_pattern(console: ConsoleBuffer,
                                      command: str,
                                      pattern: str,
                                      timeout: float = 30.0) -> str:
    """Send a command and wait for a pattern in the output."""
    start_offset = len(console.get_output())
    exec_command(console, command)
    return wait_for_console_pattern(console, pattern, timeout, start_offset)
