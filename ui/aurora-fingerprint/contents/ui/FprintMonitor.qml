// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import org.kde.plasma.workspace.dbus as DBus

Item {
    id: root
    property string mode: "idle"
    property string devicePath: "/net/reactivated/Fprint/Device/0"
    property bool needed: false
    property bool present: false
    property bool requestActive: false
    property var busType: DBus.BusType.System
    property string service: "net.reactivated.Fprint"
    readonly property string deviceInterface: "net.reactivated.Fprint.Device"

    function flag(value) {
        // KDE preserves some D-Bus scalar types as QML value wrappers.
        return value === true || (value !== undefined && value !== null
            && (value.value === true || String(value) === "true"));
    }

    function updateActivity() {
        if (!settle.running)
            mode = (requestActive || needed) ? (present ? "scanning" : "waiting") : "idle";
    }
    function result(status, done) {
        if (done) { requestActive = false; needed = false; present = false; }
        if (status === "verify-match") {
            mode = "success";
            settle.restart();
        } else if (status === "verify-no-match" || status.startsWith("verify-retry")
                   || status === "verify-swipe-too-short" || status === "verify-finger-not-centered"
                   || status === "verify-remove-and-retry" || status === "verify-unknown-error") {
            mode = "failure";
            settle.restart();
        } else if (status === "verify-disconnected") {
            settle.stop(); requestActive = false;
            needed = false; present = false; mode = "idle";
        }
    }
    function findDevice() {
        const bus = root.busType === DBus.BusType.System ? DBus.SystemBus : DBus.SessionBus;
        const reply = bus.asyncCall({service: root.service,
            path: "/net/reactivated/Fprint/Manager", iface: "net.reactivated.Fprint.Manager",
            member: "GetDefaultDevice"});
        reply.finished.connect(() => {
            if (!reply.isError) {
                const path = String(reply.value);
                if (path.startsWith("/net/reactivated/Fprint/Device/"))
                    root.devicePath = path;
            }
        });
    }
    Timer {
        id: settle
        interval: 1400
        onTriggered: root.updateActivity()
    }
    DBus.DBusServiceWatcher {
        id: daemon
        busType: root.busType
        watchedService: root.service
        onRegisteredChanged: {
            if (registered) root.findDevice();
            else { settle.stop(); root.requestActive = false; root.needed = false; root.present = false; root.mode = "idle"; }
        }
    }
    DBus.SignalWatcher {
        busType: root.busType
        service: root.service
        path: root.devicePath
        iface: root.deviceInterface
        enabled: daemon.registered
        function dbusVerifyStatus(status, done) { root.result(String(status), Boolean(done)); }
        function dbusVerifyFingerSelected(finger) {
            root.requestActive = true;
            root.needed = true;
            root.present = false;
            root.updateActivity();
        }
        function dbusEnrollStatus(status, done) { /* Enrollment is not login feedback. */ }
    }
    Loader {
        active: daemon.registered
        sourceComponent: Component {
            DBus.Properties {
                id: fingerProps
                objectName: "fingerProperties"
                busType: root.busType
                service: root.service
                path: root.devicePath
                iface: root.deviceInterface
                function syncFlags() {
                    root.needed = root.flag(fingerProps.properties["finger-needed"]);
                    root.present = root.flag(fingerProps.properties["finger-present"]);
                    root.updateActivity();
                }
                onRefreshed: fingerProps.syncFlags()
                onPropertiesChanged: fingerProps.syncFlags()
            }
        }
    }
    Component.onCompleted: { if (daemon.registered) findDevice(); }
}
