// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    property alias cfg_ReducedMotion: reduced.checked
    property alias cfg_ShowFingerprint: feedback.checked
    readonly property bool chinese: Qt.locale().name.startsWith("zh")
    CheckBox {
        id: reduced
        text: parent.chinese ? "减少动态效果" : "Reduce motion"
    }
    CheckBox {
        id: feedback
        text: parent.chinese ? "显示真实指纹验证状态" : "Show live fingerprint feedback"
        checked: true
    }
}
