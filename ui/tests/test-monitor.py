#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run with dbus-run-session. Fake services never touch the real system bus."""
import os
from pathlib import Path
import time
os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["QT_QUICK_BACKEND"] = "software"
from gi.repository import Gio, GLib
from PySide6.QtCore import QCoreApplication, QMetaObject, QObject, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtQuick import QQuickItem

SERVICE = "io.github.xiecheesejohn.FprintTest"
DEVICE = "/net/reactivated/Fprint/Device/1"
IFACE = "net.reactivated.Fprint.Device"
flags = {"finger-needed": False, "finger-present": False}
app = QGuiApplication([])
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
owner = Gio.bus_own_name_on_connection(bus, SERVICE, Gio.BusNameOwnerFlags.NONE, None, None)
info = Gio.DBusNodeInfo.new_for_xml('''<node>
<interface name="net.reactivated.Fprint.Manager"><method name="GetDefaultDevice"><arg direction="out" type="o"/></method></interface>
<interface name="net.reactivated.Fprint.Device">
<property name="finger-needed" type="b" access="read"/>
<property name="finger-present" type="b" access="read"/>
<signal name="VerifyStatus"><arg type="s"/><arg type="b"/></signal>
<signal name="VerifyFingerSelected"><arg type="s"/></signal>
</interface></node>''')

def manager_call(connection, sender, path, interface, method, parameters, invocation):
    if method != "GetDefaultDevice":
        raise AssertionError("Only read-only discovery is allowed")
    invocation.return_value(GLib.Variant("(o)", (DEVICE,)))

def read_property(connection, sender, path, interface, key):
    return GLib.Variant("b", flags[key])

manager = bus.register_object("/net/reactivated/Fprint/Manager", info.interfaces[0], manager_call, None, None)
device = bus.register_object(DEVICE, info.interfaces[1], None, read_property, None)
engine = QQmlEngine()
component = QQmlComponent(engine, QUrl.fromLocalFile(str(Path(__file__).parents[1] / "aurora-fingerprint/contents/ui/FprintMonitor.qml")))
assert component.isReady(), [e.toString() for e in component.errors()]
monitor = component.createWithInitialProperties({"busType": 0, "service": SERVICE})
assert monitor is not None

def pump_until(mode, timeout=3):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        QCoreApplication.processEvents()
        if monitor.property("mode") == mode:
            return
        time.sleep(0.01)
    raise AssertionError((mode, monitor.property("mode")))

def emit(member, signature, values):
    bus.emit_signal(None, DEVICE, IFACE, member, GLib.Variant(signature, values))
    bus.flush_sync(None)

try:
    deadline = time.monotonic() + 3
    while monitor.property("devicePath") != DEVICE and time.monotonic() < deadline:
        QCoreApplication.processEvents()
        time.sleep(0.01)
    pump_until("idle")
    assert monitor.property("devicePath") == DEVICE
    emit("VerifyFingerSelected", "(s)", ("right-index-finger",))
    pump_until("waiting")
    properties = monitor.findChild(QObject, "fingerProperties")
    assert properties is not None
    assert QMetaObject.invokeMethod(properties, "updateAll")
    # A backend without finger flags must not hide an active verify request.
    for _ in range(20):
        QCoreApplication.processEvents()
        time.sleep(0.01)
    pump_until("waiting")
    flags.update({"finger-needed": True, "finger-present": True})
    assert QMetaObject.invokeMethod(properties, "updateAll")
    pump_until("scanning")
    emit("VerifyStatus", "(sb)", ("verify-match", True))
    pump_until("success")
    emit("VerifyStatus", "(sb)", ("verify-no-match", True))
    pump_until("failure")
    emit("VerifyStatus", "(sb)", ("verify-disconnected", True))
    pump_until("idle")
    print("PASS: private D-Bus discovery, waiting, scanning, success, failure, disconnect; read-only")
finally:
    bus.unregister_object(manager)
    bus.unregister_object(device)
    Gio.bus_unown_name(owner)
