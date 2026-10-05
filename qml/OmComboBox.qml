import QtQuick
import QtQuick.Controls
import Rook.Runtime

// A drop-down in the theme: a field that frames in the accent when focused
// or open, and a list that opens as the shared card with the shared motion,
// its rows lit like menu rows (accent pill, accent bar).
ComboBox {
    id: control

    implicitHeight: 32
    font.pixelSize: 13
    leftPadding: 10
    rightPadding: 30

    contentItem: Text {
        textFormat: Text.PlainText
        text: control.displayText
        font: control.font
        color: control.enabled ? Colors.text : Colors.textDim
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Image {
        x: control.width - width - 10
        y: (control.height - height) / 2
        width: 12
        height: 12
        sourceSize: Qt.size(24, 24)
        rotation: control.popup.visible ? -90 : 90
        source: Colors.tint("image://fileicon/chevron",
                            control.popup.visible || control.visualFocus ? Colors.accent : Colors.textDim)
        Behavior on rotation { NumberAnimation { duration: Colors.popInMs; easing.type: Easing.OutCubic } }
    }

    background: Rectangle {
        implicitWidth: 140
        radius: Colors.radius
        color: Colors.window
        border.width: 1
        border.color: control.popup.visible || control.visualFocus ? Colors.accent
                    : control.hovered ? Qt.alpha(Colors.accent, 0.5)
                    : Colors.border
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }

    delegate: ItemDelegate {
        id: row
        required property int index
        required property var modelData
        readonly property bool lit: control.highlightedIndex === index
        readonly property bool chosen: control.currentIndex === index

        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: 30
        leftPadding: 12
        text: control.textRole ? (Array.isArray(control.model) ? modelData[control.textRole]
                                                               : modelData) : modelData

        contentItem: Text {
            textFormat: Text.PlainText
            text: row.text
            font.pixelSize: 13
            font.bold: row.chosen
            color: row.lit || row.chosen ? Colors.accent : Colors.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Item {
            Rectangle {
                id: pill
                anchors.fill: parent
                anchors.leftMargin: 2
                anchors.rightMargin: 2
                radius: Colors.radius
                color: row.lit ? Qt.alpha(Colors.accent, 0.16) : "transparent"
                Behavior on color { ColorAnimation { duration: 90 } }
            }
            Rectangle {
                anchors.left: pill.left
                anchors.verticalCenter: pill.verticalCenter
                width: 3
                radius: 1.5
                height: row.lit ? pill.height - 12 : 0
                color: Colors.accent
                Behavior on height { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
            }
        }
    }

    popup: OmPopup {
        y: control.height + 4
        width: Math.max(control.width, 160)
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 320)

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
