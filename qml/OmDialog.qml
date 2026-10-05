import QtQuick
import QtQuick.Controls
import Rook.Runtime

// Every dialog: the shared card with a slightly stronger accent frame, a
// themed title, themed buttons, a soft dim behind, and the shared open
// motion from the centre. Closing is instant (see Colors.popInMs).
Dialog {
    id: dialog

    padding: 20
    transformOrigin: Popup.Center
    background: OmCard {
        borderWidth: 1.5
        borderOpacity: 0.6
    }

    header: Label {
        visible: dialog.title !== ""
        text: dialog.title
        textFormat: Text.PlainText
        color: Colors.text
        font.pixelSize: 15
        font.bold: true
        elide: Text.ElideRight
        leftPadding: dialog.leftPadding
        rightPadding: dialog.rightPadding + 28 // clear of a close button
        topPadding: 16
        bottomPadding: 2
    }

    footer: OmButtonBox {
        visible: count > 0
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.35)
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Colors.fadeInMs; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: Colors.popInScale; to: 1; duration: Colors.popInMs; easing.type: Easing.OutCubic }
        }
    }
}
