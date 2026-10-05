import QtQuick
import QtQuick.Controls
import Rook.Runtime

// A hairline between menu groups, inset from the card's edges.
MenuSeparator {
    topPadding: 4
    bottomPadding: 4
    leftPadding: 12
    rightPadding: 12

    contentItem: Rectangle {
        implicitWidth: 200
        implicitHeight: 1
        color: Qt.alpha(Colors.border, 0.9)
    }
}
