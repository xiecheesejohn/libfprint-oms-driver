#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Read-only fingerprint reader feedback alongside a native terminal command."""
import argparse
import locale
import os
import signal
import subprocess
import sys
import termios

try:
    from gi.repository import Gio, GLib
except ImportError:
    Gio = GLib = None

SERVICE = "net.reactivated.Fprint"
IFACE = "net.reactivated.Fprint.Device"


class Display:
    def __init__(self, stream=sys.stderr, language=None, check_echo=True):
        self.stream = stream
        self.chinese = (language or os.environ.get("FPRINT_TUI_LANG", locale.getlocale()[0] or "en")).startswith("zh")
        self.color = stream.isatty() and "NO_COLOR" not in os.environ
        self.check_echo = check_echo
        self.opened = False
        self.line = False
        self.dots = 0
        self.mode = "idle"

    def can_write(self):
        if not self.check_echo:
            return True
        try:
            # Inspect terminal mode only; never read input or change termios.
            return bool(termios.tcgetattr(sys.stdin.fileno())[3] & termios.ECHO)
        except (OSError, termios.error, ValueError):
            return False

    def write(self, text, color=None):
        if not self.can_write():
            return
        if color and self.color:
            text = f"\033[{color}m{text}\033[0m"
        self.stream.write(text)
        self.stream.flush()

    def state(self, mode):
        self.mode = mode
        if not self.can_write():
            return
        if mode == "idle":
            self.close()
            return
        if not self.opened:
            title = "指纹读取器" if self.chinese else "Fingerprint reader"
            self.write(f"\n╭─ {title} ────────────────────\n", "36")
            self.opened = True
        if self.line:
            self.write("\n")
            self.line = False
        messages = {
            "waiting": ("○", "请触摸指纹传感器", "Touch the fingerprint sensor", "36"),
            "scanning": ("◉", "正在读取指纹", "Reading fingerprint", "36"),
            "success": ("✓", "指纹匹配", "Fingerprint matched", "32"),
            "failure": ("×", "未匹配，请重试或使用密码", "No match; retry or use your password", "33"),
        }
        symbol, zh, en, color = messages[mode]
        self.write(f"│ {symbol} {zh if self.chinese else en}", color)
        self.line = True
        self.dots = 0
        if mode in ("success", "failure"):
            self.close()

    def pulse(self):
        # Append dots: never move the cursor, erase a line, or cover a prompt.
        if self.mode in ("waiting", "scanning") and self.line and self.dots < 18:
            self.write(" ·", "36")
            self.dots += 1
        return True

    def close(self):
        if self.opened:
            self.write(("\n" if self.line else "") + "╰─────────────────────────────────\n", "36")
        self.opened = self.line = False


class Monitor:
    def __init__(self, display, bus=None, service=SERVICE):
        self.display = display
        self.bus = bus if bus is not None else Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        self.subscriptions = []
        self.subscriptions.append(self.bus.signal_subscribe(
            service, IFACE, None, None, None, Gio.DBusSignalFlags.NONE, self.device_signal))
        self.subscriptions.append(self.bus.signal_subscribe(
            service, "org.freedesktop.DBus.Properties", "PropertiesChanged", None,
            IFACE, Gio.DBusSignalFlags.NONE, self.properties_signal))
        self.pulse_timer = GLib.timeout_add(800, display.pulse)
        self.limit_timer = GLib.timeout_add_seconds(45, self.stop)

    def device_signal(self, connection, sender, path, interface, member, parameters):
        if not path.startswith("/net/reactivated/Fprint/Device/"):
            return
        args = parameters.unpack()
        if member == "VerifyFingerSelected":
            self.display.state("waiting")
        elif member == "VerifyStatus":
            status, done = args
            if status == "verify-match":
                self.display.state("success")
                self.stop()
            elif status in ("verify-no-match", "verify-retry-scan", "verify-swipe-too-short",
                            "verify-finger-not-centered", "verify-remove-and-retry", "verify-unknown-error"):
                self.display.state("failure")
            elif status == "verify-disconnected":
                self.display.state("idle")

    def properties_signal(self, connection, sender, path, interface, member, parameters):
        if not path.startswith("/net/reactivated/Fprint/Device/"):
            return
        _, changed, _ = parameters.unpack()
        if changed.get("finger-present") is True and self.display.mode in ("waiting", "scanning"):
            self.display.state("scanning")

    def stop(self):
        for subscription in self.subscriptions:
            self.bus.signal_unsubscribe(subscription)
        self.subscriptions.clear()
        if self.pulse_timer:
            GLib.source_remove(self.pulse_timer)
            self.pulse_timer = None
        if self.limit_timer:
            GLib.source_remove(self.limit_timer)
            self.limit_timer = None
        self.display.close()
        return False


def run_command(command, display, bus=None, service=SERVICE):
    # stdout/stderr/stdin are inherited, not piped or proxied. sudo owns all
    # password handling and any PTY it chooses to create under its own policy.
    try:
        monitor = Monitor(display, bus, service)
    except Exception:
        os.execvp(command[0], command)
    try:
        process = subprocess.Popen(command)
    except OSError:
        monitor.stop()
        raise
    previous = {}
    for signum in (signal.SIGINT, signal.SIGQUIT):
        previous[signum] = signal.signal(signum, lambda *_: None)
    loop = GLib.MainLoop()
    result = [None]

    def poll():
        result[0] = process.poll()
        if result[0] is None:
            return True
        loop.quit()
        return False

    GLib.timeout_add(80, poll)
    try:
        loop.run()
    finally:
        monitor.stop()
        for signum, handler in previous.items():
            signal.signal(signum, handler)
    return result[0] if result[0] >= 0 else 128 - result[0]


def demo():
    display = Display(check_echo=False)
    display.write("指纹状态演示（不进行身份验证）\n" if display.chinese
                  else "Fingerprint feedback demo (no authentication)\n")
    loop = GLib.MainLoop()
    steps = iter(["waiting", "scanning", "failure", "waiting", "success", "idle"])
    display.state(next(steps))
    pulse = GLib.timeout_add(500, display.pulse)
    def advance():
        try:
            display.state(next(steps))
            return True
        except StopIteration:
            loop.quit()
            return False
    GLib.timeout_add(900, advance)
    loop.run()
    GLib.source_remove(pulse)
    display.close()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", action="store_true", help="Show sample states; no authentication")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.demo:
        if GLib is None:
            parser.error("Install python3-gobject to run the demo.")
        return demo()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("Example: fingerprint-tui -- sudo -i")
    bypass = (os.environ.get("FPRINT_TUI") == "0" or GLib is None
              or not sys.stdin.isatty() or not sys.stderr.isatty()
              or os.environ.get("TERM", "dumb") == "dumb")
    if bypass:
        os.execvp(command[0], command)
    return run_command(command, Display())


if __name__ == "__main__":
    try:
        sys.exit(main())
    except OSError as error:
        print(f"fingerprint-tui: {error}", file=sys.stderr)
        sys.exit(127 if isinstance(error, FileNotFoundError) else 126)
