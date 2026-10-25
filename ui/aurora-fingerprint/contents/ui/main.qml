// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Window
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root
    readonly property bool reducedMotion: configuration ? configuration.ReducedMotion : false
    readonly property bool showFingerprint: configuration ? configuration.ShowFingerprint : true
    readonly property var statusMonitor: monitor.item

    AuroraBackground {
        anchors.fill: parent
        reducedMotion: root.reducedMotion
    }
    // If the optional KDE DBus QML module changes, keep the background working.
    Loader {
        id: monitor
        source: "FprintMonitor.qml"
    }
    FingerprintFeedback {
        objectName: "auroraFingerprintFeedback"
        // Keep feedback above the host's wallpaper blur without replacing its UI.
        // This is a non-interactive item in the same trusted window, not a new window.
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(48, parent.height * 0.06)
        width: Math.min(300, parent.width - 24)
        height: 140
        mode: root.showFingerprint && root.statusMonitor ? root.statusMonitor.mode : "idle"
        reducedMotion: root.reducedMotion
    }
}
