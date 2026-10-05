import QtQuick
import QtQuick.Controls
import Rook.Runtime

// A dialog or panel button in the theme. In a button box its role decides
// the look: the accepting action filled with the accent, a destructive one
// outlined in red, the rest outlined. `primary` overrides outside a box.
Button {
    id: control

    readonly property int role: DialogButtonBox.buttonRole
    property bool primary: role === DialogButtonBox.AcceptRole || role === DialogButtonBox.YesRole
                           || role === DialogButtonBox.ApplyRole
    readonly property bool destructive: role === DialogButtonBox.DestructiveRole

    implicitHeight: 32
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 13

    contentItem: Text {
        textFormat: Text.PlainText
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        opacity: control.enabled ? 1 : 0.45
        // On the accent fill: whichever of white/near-black reads.
        color: control.primary ? (Colors.accent.hslLightness > 0.6 ? "#111111" : "#ffffff")
             : control.destructive ? Colors.error
             : Colors.text
    }

    background: Rectangle {
        implicitWidth: 84
        radius: Colors.radius
        color: control.primary
               ? (control.down ? Qt.darker(Colors.accent, 1.15)
                  : control.hovered ? Qt.lighter(Colors.accent, 1.08) : Colors.accent)
               : control.down ? Qt.alpha(Colors.accent, 0.22)
               : control.hovered ? Qt.alpha(control.destructive ? Colors.error : Colors.accent, 0.12)
               : "transparent"
        border.width: 1
        border.color: control.primary ? Colors.accent
                    : control.destructive ? Qt.alpha(Colors.error, 0.7)
                    : control.visualFocus || control.hovered ? Qt.alpha(Colors.accent, 0.7)
                    : Colors.border
        opacity: control.enabled ? 1 : 0.6
        Behavior on color { ColorAnimation { duration: 90 } }
    }
}
