import QtQuick
import Rook.Runtime

// The floating-surface card shared by menus, dialogs, popovers and
// drop-downs: the theme's surface, a thin accent frame (the Omarchy/Hyprland
// window-border language) and a soft two-layer shadow just outside it.
Item {
    id: root

    property real radius: Colors.radius + 2
    property real borderWidth: 1
    property real borderOpacity: 0.55

    Rectangle {
        anchors.fill: card
        anchors.margins: -4
        anchors.topMargin: -2
        anchors.bottomMargin: -6
        radius: card.radius + 4
        color: Qt.rgba(0, 0, 0, 0.12)
    }
    Rectangle {
        anchors.fill: card
        anchors.margins: -1.5
        anchors.bottomMargin: -3
        radius: card.radius + 1.5
        color: Qt.rgba(0, 0, 0, 0.16)
    }
    Rectangle {
        id: card
        anchors.fill: parent
        radius: root.radius
        color: Qt.alpha(Colors.chrome, 1)
        border.width: root.borderWidth
        border.color: Qt.alpha(Colors.border, root.borderOpacity)
    }
}
