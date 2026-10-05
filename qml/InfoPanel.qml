import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// The info panel (F11, vim `i`): docked on the right, about the current item
// — or the folder itself when nothing is selected. A preview, the facts, the
// folder's real size on request, and checksums with a clipboard compare.
Rectangle {
    id: root

    property Item tab: null

    readonly property int row: tab ? tab.previewRow() : -1
    readonly property string location: !tab ? "" : row >= 0 ? tab.actionPathAt(row) : tab.path
    readonly property bool isDir: info.kind === "folder"

    color: Colors.chrome
    clip: true

    onLocationChanged: {
        info.path = location;
    }

    QuickViewInfo { id: info }
    // The Properties dialog's facts and folder measurement, reused: async,
    // and cancelled as soon as the selection moves on.
    FileProperties {
        id: props
        paths: root.location !== "" ? [root.location] : []
    }

    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: Colors.border
    }

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 1
        contentHeight: column.height + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: column
            x: 14
            y: 14
            width: parent.width - 28
            spacing: 6

            // ---- preview ----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(200, width * 0.75)
                radius: Colors.radius
                color: Colors.window
                border.color: Colors.border
                border.width: 1
                clip: true

                readonly property bool picture: info.kind === "image" || info.kind === "animated"

                Image {
                    anchors.fill: parent
                    anchors.margins: 6
                    visible: parent.picture
                    source: parent.picture ? info.url : ""
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    sourceSize: Qt.size(512, 512)
                }

                Text {
                    textFormat: Text.PlainText
                    anchors.fill: parent
                    anchors.margins: 8
                    visible: info.kind === "text" || info.kind === "markdown"
                    text: visible ? info.text.slice(0, 1500) : ""
                    color: Colors.textDim
                    font.family: "monospace"
                    font.pixelSize: 9
                    clip: true
                }

                Image {
                    anchors.centerIn: parent
                    visible: !parent.picture && info.kind !== "text" && info.kind !== "markdown"
                    readonly property string icon: root.tab && root.row >= 0
                        ? root.tab.files.valueAt(root.row, "iconSource")
                        : "image://fileicon/folder"
                    source: Colors.fileIcon(icon, root.isDir ? Colors.folder : Colors.textDim, 96)
                    sourceSize: Qt.size(96, 96)
                }
            }

            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.topMargin: 6
                text: Platform.elideMiddle(info.name || Platform.baseName(root.location),
                                           font.pixelSize, width, 3, true)
                color: Colors.text
                font.pixelSize: 14
                font.bold: true
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 3
                elide: Text.ElideRight
            }

            // ---- facts ----
            component Fact: ColumnLayout {
                property string label: ""
                property string value: ""
                Layout.fillWidth: true
                spacing: 0
                visible: value !== ""

                Text {
                    textFormat: Text.PlainText
                    text: parent.label
                    color: Colors.textDim
                    font.pixelSize: 10
                    font.capitalization: Font.AllUppercase
                }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: parent.value
                    color: Colors.text
                    font.pixelSize: 12
                    wrapMode: Text.WrapAnywhere
                }
            }

            Fact { label: qsTr("Type"); value: info.typeDescription }
            Fact {
                label: qsTr("Size")
                value: !root.isDir ? (info.size >= 0 ? Platform.formatSize(info.size) : "")
                     : props.measuring && props.size === 0 ? qsTr("Counting…")
                     : qsTr("%1%2 — %3 files, %4 folders")
                           .arg(Platform.formatSize(props.size)).arg(props.measuring ? "…" : "")
                           .arg(props.fileCount).arg(props.folderCount)
            }
            Fact {
                label: qsTr("Dimensions")
                value: info.imageWidth > 0 ? info.imageWidth + " × " + info.imageHeight + " px" : ""
            }
            Fact {
                label: qsTr("Modified")
                value: info.modified ? Platform.formatTimestamp(info.modified) : ""
            }
            Fact {
                label: qsTr("Permissions")
                value: props.modeText !== "" ? props.modeText + "  (" + props.modeOctal + ")" : ""
            }
            Fact {
                label: qsTr("Owner")
                value: props.owner !== "" ? props.owner + (props.group !== "" ? " : " + props.group : "") : ""
            }
            Fact { label: qsTr("Location"); value: Platform.parentPath(root.location) }

            // ---- checksum, on request ----
            ChecksumBox {
                Layout.fillWidth: true
                Layout.topMargin: 6
                path: root.location
                visible: !root.isDir && Platform.isLocal(root.location)
            }
        }
    }
}
