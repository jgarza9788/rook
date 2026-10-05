import QtQuick
import QtQuick.Controls
import Rook.Runtime

// About Files — the hamburger menu's last entry, omacalc-flat.
OmDialog {
    id: root

    anchors.centerIn: Overlay.overlay
    width: 360
    modal: true
    // Nothing to lose here, so a click on the dimmed window closes it too.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Held by a property, not the default one: that would hand it to the
    // contentItem (a ScrollView here and there) before it lifts itself out.
    readonly property Item closeButton: DialogCloseButton { dialog: root }

    Column {
        width: parent.width
        spacing: 6
        topPadding: 10
        bottomPadding: 10

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/qt/qml/Rook/packaging/rook.svg"
            sourceSize: Qt.size(64, 64)
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Rook")
            color: Colors.text
            font.pixelSize: 20
            font.bold: true
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: "rook " + Qt.application.version
            color: Colors.textDim
            font.pixelSize: 12
        }

        Text {
            textFormat: Text.PlainText
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("A palette-first file manager. Work in progress.")
            color: Colors.textDim
            font.pixelSize: 12
        }
    }
}
