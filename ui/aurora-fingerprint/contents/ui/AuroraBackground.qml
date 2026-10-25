// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick

Item {
    id: root
    property bool reducedMotion: false
    property real phase: 0
    clip: true

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#07111e" }
            GradientStop { position: 0.58; color: "#0c2232" }
            GradientStop { position: 1; color: "#050b14" }
        }
    }

    Canvas {
        id: aurora
        anchors.centerIn: parent
        width: 960
        height: 540
        scale: Math.max(root.width / width, root.height / height)
        canvasSize: Qt.size(960, 540)
        smooth: true
        renderStrategy: Canvas.Threaded
        onPaint: {
            const c = getContext("2d");
            c.clearRect(0, 0, 960, 540);
            const t = root.phase;
            for (let i = 0; i < 5; ++i) {
                const y = 125 + i * 29 + Math.sin(t + i * 0.8) * 26;
                const color = i < 3 ? "55, 220, 195" : "93, 137, 231";
                const g = c.createLinearGradient(0, y - 75, 0, y + 145);
                g.addColorStop(0, "rgba(" + color + ",0)");
                g.addColorStop(0.42, "rgba(" + color + ",0.075)");
                g.addColorStop(0.57, "rgba(" + color + ",0.22)");
                g.addColorStop(1, "rgba(" + color + ",0)");
                c.fillStyle = g;
                c.beginPath();
                c.moveTo(-100, y + 100);
                c.bezierCurveTo(190, y - 140 + Math.cos(t) * 40,
                                440, y + 110, 1060, y - 65);
                c.lineTo(1060, y + 110);
                c.bezierCurveTo(590, y + 175, 190, y - 20, -100, y + 210);
                c.closePath();
                c.fill();
            }
            for (let j = 0; j < 52; ++j) {
                const x = ((j * 137.508 + 41) % 960);
                const y = ((j * 59.317 + 21) % 400);
                const alpha = 0.12 + (Math.sin(t * 0.5 + j) + 1) * 0.08;
                c.fillStyle = "rgba(214,239,255," + alpha + ")";
                c.fillRect(x, y, j % 5 === 0 ? 1.3 : 0.7, 0.8);
            }
        }
        Component.onCompleted: requestPaint()
    }
    NumberAnimation on phase {
        from: 0; to: Math.PI * 2; duration: 90000
        loops: Animation.Infinite
        running: root.visible && !root.reducedMotion
    }
    Timer {
        interval: 125; repeat: true
        running: root.visible && !root.reducedMotion
        onTriggered: aurora.requestPaint()
    }
    onReducedMotionChanged: aurora.requestPaint()
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: "#08040a13" }
            GradientStop { position: 0.45; color: "#10040a13" }
            GradientStop { position: 1; color: "#c4050b14" }
        }
    }
}
