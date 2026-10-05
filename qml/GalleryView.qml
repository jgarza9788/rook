import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// Gallery (Ctrl+4): Finder's gallery view. The current item large on top —
// the same preview as Space — with a caption, and a strip of thumbnails
// below that h / l (or the arrows) step through. Ctrl +/- sizes the strip;
// +/-/0 in the quick view still zooms a picture.
Item {
    id: root

    required property var tab
    property alias currentIndex: strip.currentIndex
    readonly property int columns: 1
    // The strip's thumbnails follow the grid's icon size (Ctrl +/-).
    readonly property int thumbSize: Math.max(48, Math.min(128, root.tab.zoom))
    readonly property string previewLocation: preview.location

    function positionAt(row) { strip.positionViewAtIndex(row, ListView.Center); }

    readonly property int row: root.tab.previewRow() >= 0 ? root.tab.previewRow() : root.tab.currentIndex

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PreviewPane {
            id: preview
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            location: root.row >= 0 && root.row < root.tab.files.count ? root.tab.actionPathAt(root.row) : ""
            iconSource: root.row >= 0 ? root.tab.files.valueAt(root.row, "iconSource") : ""
            autoPlay: false
            interactive: false
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            horizontalAlignment: Text.AlignHCenter
            visible: preview.location !== ""
            text: preview.info.name + (preview.summary !== "" ? "  ·  " + preview.summary : "")
            color: Colors.textDim
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.tab.files.count === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: qsTr("This folder is empty")
            color: Colors.textDim
            font.pixelSize: 13
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 6
            Layout.preferredHeight: 1
            color: Colors.border
        }

        ListView {
            id: strip
            objectName: "galleryStrip"
            Layout.fillWidth: true
            Layout.preferredHeight: root.thumbSize + 40
            orientation: ListView.Horizontal
            spacing: 6
            leftMargin: 8
            rightMargin: 8
            clip: true
            model: root.tab.files
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false
            // Keep the current thumbnail in the middle as it moves.
            highlightRangeMode: ListView.ApplyRange
            preferredHighlightBegin: width / 2 - (root.thumbSize + 16) / 2
            preferredHighlightEnd: width / 2 + (root.thumbSize + 16) / 2
            highlightMoveDuration: 120
            ScrollBar.horizontal: ScrollBar {}

            delegate: Rectangle {
                id: cell
                required property int index
                required property string name
                required property string displayName
                required property string iconSource
                required property string filePath
                required property string targetPath
                required property string contentType
                required property real size
                required property var modified
                required property bool isDir

                readonly property bool selected: root.tab.isSelected(name)
                readonly property bool isCurrent: index === root.row

                width: root.thumbSize + 16
                height: ListView.view.height - 8
                y: 4
                radius: Colors.radius
                color: selected ? Colors.selection : "transparent"
                border.color: isCurrent ? Colors.accent : "transparent"
                border.width: 2

                Image {
                    id: thumb
                    // Same fallback chain as the grid: a thumbnail when one
                    // can be made here, else the type icon; a failure is
                    // remembered per URL so it is not retried every frame.
                    property string failedSource: ""
                    readonly property string previewPath: cell.targetPath !== "" ? cell.targetPath : cell.filePath
                    readonly property string thumbnailSource: Thumbnails.source(previewPath, cell.modified, cell.size)
                    readonly property bool wantThumbnail:
                        thumbnailSource !== failedSource
                        && Settings.showThumbnails !== "never"
                        && (Settings.showThumbnails === "always" || Platform.isLocal(previewPath))
                        && Thumbnails.canThumbnail(cell.contentType, cell.size)

                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 6
                    width: root.thumbSize
                    height: root.thumbSize
                    fillMode: Image.PreserveAspectFit
                    source: wantThumbnail ? thumbnailSource
                          : Colors.fileIcon(cell.iconSource,
                                            cell.selected ? Colors.selectionText
                                          : cell.isDir ? Colors.folder : Colors.textDim, root.thumbSize)
                    sourceSize: Qt.size(root.thumbSize, root.thumbSize)
                    asynchronous: true
                    onStatusChanged: if (status === Image.Error && wantThumbnail) failedSource = thumbnailSource
                }

                Text {
                    textFormat: Text.PlainText
                    anchors.top: thumb.bottom
                    anchors.topMargin: 4
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 6
                    horizontalAlignment: Text.AlignHCenter
                    text: Platform.elideMiddle(cell.displayName, font.pixelSize, width, 1)
                    color: cell.selected ? Colors.selectionText : Colors.text
                    font.pixelSize: 11
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        root.tab.currentIndex = cell.index;
                        root.tab.focusView();
                        if (mouse.button === Qt.RightButton) {
                            if (!cell.selected)
                                root.tab.selectOnly(cell.name);
                            root.tab.requestContextMenu();
                        } else if (mouse.modifiers & Qt.ControlModifier) {
                            root.tab.toggleSelection(cell.name);
                        } else if (mouse.modifiers & Qt.ShiftModifier) {
                            root.tab.extendSelectionTo(cell.index);
                        } else {
                            root.tab.selectOnly(cell.name);
                        }
                    }
                    onDoubleClicked: mouse => {
                        if (mouse.button === Qt.LeftButton)
                            root.tab.activate(cell.index);
                    }
                }
            }
        }
    }
}
