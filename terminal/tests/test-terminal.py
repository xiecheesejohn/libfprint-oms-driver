#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run with dbus-run-session. No sudo or real authentication is invoked."""
import importlib.util
import io
import os
from pathlib import Path
import subprocess
import sys
import unittest

PATH = Path(__file__).parents[1] / "fingerprint-tui.py"
spec = importlib.util.spec_from_file_location("fingerprint_tui", PATH)
tui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tui)


class TerminalTests(unittest.TestCase):
    def test_noninteractive_exit_and_stdout(self):
        result = subprocess.run([sys.executable, str(PATH), "--", sys.executable,
                                 "-c", "import sys; print('native output'); sys.exit(7)"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(result.stdout, "native output\n")
        self.assertEqual(result.stderr, "")

    def test_input_is_passed_to_command(self):
        result = subprocess.run([sys.executable, str(PATH), "--", sys.executable, "-c",
                                 "import sys; sys.stdout.buffer.write(sys.stdin.buffer.read())"],
                                input=b"test input\n", capture_output=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b"test input\n")
        self.assertEqual(result.stderr, b"")

    def test_missing_command_exit(self):
        result = subprocess.run([sys.executable, str(PATH), "--", "/nonexistent/fingerprint-tui-test"],
                                capture_output=True)
        self.assertEqual(result.returncode, 127)

    def test_child_signal_status(self):
        bus = tui.Gio.bus_get_sync(tui.Gio.BusType.SESSION, None)
        display = tui.Display(stream=io.StringIO(), language="en", check_echo=False)
        result = tui.run_command([sys.executable, "-c", "import os,signal;os.kill(os.getpid(),signal.SIGTERM)"],
                                 display, bus=bus, service="io.github.xiecheesejohn.SignalTest")
        self.assertEqual(result, 143)

    def test_private_bus_states_and_command_exit(self):
        Gio, GLib = tui.Gio, tui.GLib
        bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        service = "io.github.xiecheesejohn.TerminalTest"
        bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                      "RequestName", GLib.Variant("(su)", (service, 0)), None,
                      Gio.DBusCallFlags.NONE, 2000, None)
        stream = io.StringIO()
        display = tui.Display(stream=stream, language="en", check_echo=False)

        def emit(member, signature, args):
            bus.emit_signal(None, "/net/reactivated/Fprint/Device/0", tui.IFACE,
                            member, GLib.Variant(signature, args))
            return False

        GLib.timeout_add(100, emit, "VerifyFingerSelected", "(s)", ("any",))
        GLib.timeout_add(180, emit, "VerifyStatus", "(sb)", ("verify-no-match", True))
        GLib.timeout_add(240, emit, "VerifyFingerSelected", "(s)", ("any",))
        GLib.timeout_add(320, emit, "VerifyStatus", "(sb)", ("verify-match", True))
        result = tui.run_command([sys.executable, "-c", "import time,sys;time.sleep(.6);sys.exit(9)"],
                                 display, bus=bus, service=service)
        self.assertEqual(result, 9)
        text = stream.getvalue()
        for expected in ("Touch the fingerprint sensor", "No match", "Fingerprint matched"):
            self.assertIn(expected, text)
        self.assertNotIn("\r", text)
        self.assertNotIn("\033", text)
        self.assertEqual(display.mode, "success")
        bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                      "ReleaseName", GLib.Variant("(s)", (service,)), None,
                      Gio.DBusCallFlags.NONE, 2000, None)


if __name__ == "__main__":
    unittest.main(verbosity=2)
