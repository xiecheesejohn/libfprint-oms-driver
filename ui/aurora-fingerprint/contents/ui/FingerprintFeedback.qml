// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick

Item {
    id: root
    property string mode: "idle"
    property bool reducedMotion: false
    readonly property bool chinese: Qt.locale().name.startsWith("zh")
    readonly property color accent: mode === "success" ? "#79edb5"
        : mode === "failure" ? "#ff929c" : "#88e1e1"
    readonly property string label: mode === "success" ? (chinese ? "指纹验证通过" : "Fingerprint verified")
        : mode === "failure" ? (chinese ? "请重试或使用密码" : "Try again or use your password")
        : mode === "scanning" ? (chinese ? "正在识别指纹" : "Reading fingerprint")
        : (chinese ? "请触摸指纹传感器" : "Touch the fingerprint sensor")
    property real breath: 0.65
    property real sweep: 0
    implicitWidth: 300
    implicitHeight: 150
    opacity: mode === "idle" ? 0 : 1
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: root.reducedMotion ? 0 : 250 } }

    Rectangle {
        width: 80; height: 80; radius: 40
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0
        color: "#152d434b"
        border.width: 1
        border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b, root.breath * 0.6)
        scale: root.mode === "success" ? 1.06 : 1
        Behavior on scale { NumberAnimation { duration: 240; easing.type: Easing.OutCubic } }

        Canvas {
            id: fingerprint
            anchors.fill: parent
            anchors.margins: 15
            onPaint: {
                const c = getContext("2d");
                c.clearRect(0, 0, width, height);
                c.strokeStyle = root.accent;
                c.lineWidth = 1.65;
                c.lineCap = "round";
                if (root.mode === "success") {
                    c.beginPath(); c.moveTo(12, 26); c.lineTo(22, 35); c.lineTo(39, 16); c.stroke();
                    return;
                }
                for (let i = 0; i < 6; ++i) {
                    const inset = 3 + i * 4;
                    const left = inset;
                    const right = width - inset;
                    if (right <= left) break;
                    c.beginPath();
                    c.moveTo(left, height - 8 - i * 1.4);
                    c.bezierCurveTo(left - 2, 5 + i * 3, right + 2, 5 + i * 3,
                                    right, height - 12 - i * 1.9);
                    c.stroke();
                }
                c.beginPath(); c.moveTo(25, 27); c.bezierCurveTo(25, 36, 23, 42, 20, 48); c.stroke();
            }
            Component.onCompleted: requestPaint()
            Connections { target: root; function onModeChanged() { fingerprint.requestPaint(); } }
        }
        Rectangle {
            width: 52; height: 1
            anchors.horizontalCenter: parent.horizontalCenter
            y: 15 + root.sweep * 50
            color: root.accent
            opacity: root.mode === "scanning" ? 0.8 : 0
        }
    }
    Text {
        y: 98
        width: parent.width
        text: root.label
        color: root.accent
        font.pixelSize: 15
        font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter
        textFormat: Text.PlainText
    }
    SequentialAnimation on breath {
        running: root.visible && !root.reducedMotion && root.mode !== "success"
        loops: Animation.Infinite
        NumberAnimation { from: 0.3; to: 1; duration: 1300; easing.type: Easing.InOutSine }
        NumberAnimation { from: 1; to: 0.3; duration: 1300; easing.type: Easing.InOutSine }
    }
    NumberAnimation on sweep {
        from: 0; to: 1; duration: 1000; loops: Animation.Infinite
        running: root.visible && !root.reducedMotion && root.mode === "scanning"
    }
}
