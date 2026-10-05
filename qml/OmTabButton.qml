import QtQuick
import QtQuick.Controls
import Rook.Runtime

// A tab in a dialog's tab bar: plain text, the chosen one in the accent with
// an accent underline that slides in — no stock grey blocks.
TabButton {
    id: control

    implicitHeight: 34
    font.pixelSize: 13

    contentItem: Text {
        textFormat: Text.PlainText
        text: control.text
        font.pixelSize: control.font.pixelSize
        font.bold: control.checked
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: control.enabled ? 1 : 0.4
        color: control.checked ? Colors.accent : control.hovered ? Colors.text : Colors.textDim
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    background: Item {
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            height: 2
            radius: 1
            width: control.checked ? parent.width - 16 : 0
            color: Colors.accent
            Behavior on width { NumberAnimation { duration: Colors.popInMs; easing.type: Easing.OutCubic } }
        }
    }
}
