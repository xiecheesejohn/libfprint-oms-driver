// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import "../aurora-fingerprint/contents/ui" as Aurora

ApplicationWindow {
    id: window
    width: 1120; height: 700
    minimumWidth: 640; minimumHeight: 480
    visible: true
    title: "极光指纹 · 独立插件预览"
    property string previewMode: "waiting"
    Aurora.AuroraBackground { anchors.fill: parent }
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height * 0.18
        spacing: 12
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatTime(new Date(), "hh:mm")
            font.pixelSize: 68; font.weight: Font.Light
            color: "#e5f1f5"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "独立插件预览 · 不进行身份验证"
            font.pixelSize: 15; color: "#95b0bd"
        }
    }
    Aurora.FingerprintFeedback {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 58
        mode: window.previewMode
    }
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 36
        spacing: 10
        Repeater {
            model: ["waiting", "scanning", "success", "failure"]
            Button {
                required property string modelData
                text: ({waiting: "等待", scanning: "扫描", success: "成功", failure: "失败"})[modelData]
                onClicked: window.previewMode = modelData
            }
        }
    }
}
