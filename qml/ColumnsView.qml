import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// Columns (Ctrl+3): Finder's column view. One column per folder along the
// path — from Home, or / outside it, the same walk as the path bar — then
// the folder being browsed, then a preview of the current item. Going deeper
// adds a column on the right and scrolls to it; h / ← goes back up.
//
// Only the current column is the tab's own model, so selection, keys, the
// context menu and every file operation work exactly as in the list view.
// The columns to its left are small read-only listings of each ancestor.
Item {
    id: root

    required property var tab
    property alias currentIndex: current.currentIndex
    readonly property int columns: 1
    readonly property int iconSize: Math.min(24, root.tab.zoom)

    readonly property var crumbs: Platform.pathCrumbs(root.tab.path)
    // Ancestors: every crumb but the last (the current folder itself).
    readonly property var ancestors: crumbs.slice(0, Math.max(0, crumbs.length - 1))
    readonly property int ancestorWidth: 220
    readonly property int currentWidth: 260
    // What the preview column shows — published for the tests.
    readonly property string previewLocation: preview.location

    function positionAt(row) { current.positionViewAtIndex(row, ListView.Contain); }

    // Keep the current column and the preview on screen as the path grows.
    function scrollToEnd() {
        Qt.callLater(() => flick.contentX = Math.max(0, flick.contentWidth - flick.width));
    }
    onCrumbsChanged: scrollToEnd()
    onWidthChanged: scrollToEnd()

    // One row, shared look for ancestor and current columns.
    component ColumnRow: Rectangle {
        id: rowItem
        property string label: ""
        property string icon: ""
        property bool isFolder: false
        property bool selected: false
        property bool onPath: false   // ancestor column: the folder leading onward
        property bool dimmed: false

        height: Math.max(Colors.rowHeight - 4, root.iconSize + 8)
        radius: Colors.radius
        color: selected ? Colors.selection
             : onPath ? Qt.alpha(Colors.accent, 0.22)
             : "transparent"

        Image {
            id: rowIcon
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            source: rowItem.icon !== ""
                    ? Colors.fileIcon(rowItem.icon, rowItem.selected ? Colors.selectionText
                                                 : rowItem.isFolder ? Colors.folder : Colors.textDim,
                                      root.iconSize)
                    : ""
            sourceSize: Qt.size(root.iconSize, root.iconSize)
            opacity: rowItem.dimmed ? 0.5 : 1
        }

        Text {
            textFormat: Text.PlainText
            anchors.left: rowIcon.right
            anchors.leftMargin: 8
            anchors.right: chevron.left
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            text: rowItem.label
            color: rowItem.selected ? Colors.selectionText : Colors.text
            opacity: rowItem.dimmed ? 0.5 : 1
            font.pixelSize: 13
            elide: Text.ElideMiddle
        }

        Text {
            id: chevron
            textFormat: Text.PlainText
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: rowItem.isFolder
            text: "›"
            color: rowItem.selected ? Colors.selectionText : Colors.textDim
            font.pixelSize: 14
        }
    }

    // A read-only listing of one ancestor folder.
    component AncestorColumn: Rectangle {
        id: column
        required property string folder
        required property string nextPath // the child folder on the way down

        width: root.ancestorWidth
        height: flick.height
        color: "transparent"

        DirectoryModel {
            id: dirModel
            path: column.folder
            countItems: false
        }

        FileSortFilterModel {
            id: proxy
            sourceModel: dirModel
            showHidden: root.tab.showHidden
            sortKey: root.tab.sortKey
            sortDescending: root.tab.sortDescending
            foldersFirst: Settings.sortFoldersFirst
        }

        ListView {
            id: list
            objectName: "ancestorColumn"
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            model: proxy
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            // Bring the highlighted folder into view once the listing arrives.
            onCountChanged: {
                const row = proxy.proxyRowForName(Platform.baseName(column.nextPath));
                if (row >= 0)
                    positionViewAtIndex(row, ListView.Center);
            }

            delegate: ColumnRow {
                required property int index
                required property string name
                required property string displayName
                required property string iconSource
                required property string filePath
                required property bool isDir

                width: ListView.view.width
                label: displayName
                icon: iconSource
                isFolder: isDir
                onPath: filePath === column.nextPath

                FileDropArea {
                    anchors.fill: parent
                    enabled: isDir
                    destination: filePath
                    springLoaded: true
                    onFilesDropped: urls => root.tab.requestDrop(urls, filePath)
                    onSprung: root.tab.navigate(filePath)
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        // A folder becomes the one being browsed; a file is
                        // shown selected in its own folder.
                        if (isDir) {
                            root.tab.navigate(filePath);
                        } else {
                            root.tab.pendingSelection = name;
                            root.tab.navigate(column.folder);
                        }
                        root.tab.focusView();
                    }
                }
            }
        }

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Colors.border
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: strip.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.horizontal: ScrollBar {}
        Behavior on contentX { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

        Row {
            id: strip
            height: flick.height

            Repeater {
                model: root.ancestors

                AncestorColumn {
                    required property var modelData
                    required property int index
                    folder: modelData.path
                    nextPath: index + 1 < root.crumbs.length ? root.crumbs[index + 1].path : ""
                }
            }

            // ---- the current folder: the tab's own model ----
            Rectangle {
                width: root.currentWidth
                height: flick.height
                color: "transparent"

                ListView {
                    id: current
                    objectName: "currentColumn"
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: root.tab.files
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: false
                    highlightFollowsCurrentItem: false
                    ScrollBar.vertical: ScrollBar {}

                    delegate: ColumnRow {
                        id: row
                        required property int index
                        required property string name
                        required property string displayName
                        required property string iconSource
                        required property string filePath
                        required property string targetPath
                        required property bool isDir

                        width: ListView.view.width
                        label: displayName
                        icon: iconSource
                        isFolder: row.isDir
                        selected: root.tab.isSelected(name)
                        dimmed: Clipboard.revision >= 0 && (Clipboard.isCutPath(filePath)
                                || (targetPath !== "" && Clipboard.isCutPath(targetPath)))
                        border.color: index === root.tab.currentIndex && !selected
                                      ? Colors.accent : "transparent"
                        border.width: 1

                        FileDrag {
                            id: dragProxy
                            pressed: rowMouse.pressed
                            dragging: rowMouse.drag.active
                        }

                        FileDropArea {
                            anchors.fill: parent
                            enabled: row.isDir
                            destination: row.filePath
                            springLoaded: true
                            onFilesDropped: urls => root.tab.requestDrop(urls, row.filePath)
                            onSprung: root.tab.navigate(row.filePath)
                        }

                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            drag.target: dragProxy

                            // Same rules as the list view: press selects
                            // before a drag, modifiers keep their meaning,
                            // the click policy decides what opens.
                            onPressed: mouse => {
                                if (mouse.button !== Qt.LeftButton)
                                    return;
                                if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))
                                    && !root.tab.isSelected(row.name))
                                    root.tab.selectOnly(row.name);
                                const paths = root.tab.isSelected(row.name)
                                    ? root.tab.selectedPaths()
                                    : [root.tab.viewingRecent && row.targetPath !== ""
                                       ? row.targetPath : row.filePath];
                                const icon = Colors.fileIcon(row.iconSource,
                                    row.isDir ? Colors.folder : Colors.textDim, 36);
                                dragProxy.prepare(paths, row.displayName, icon, icon);
                            }
                            onClicked: mouse => {
                                root.tab.currentIndex = row.index;
                                root.tab.focusView();
                                if (mouse.button === Qt.RightButton) {
                                    if (!root.tab.isSelected(row.name))
                                        root.tab.selectOnly(row.name);
                                    root.tab.requestContextMenu();
                                } else if (mouse.modifiers & Qt.ControlModifier) {
                                    root.tab.toggleSelection(row.name);
                                } else if (mouse.modifiers & Qt.ShiftModifier) {
                                    root.tab.extendSelectionTo(row.index);
                                } else {
                                    root.tab.selectOnly(row.name);
                                    if (Settings.clickPolicy === "single")
                                        root.tab.activate(row.index);
                                }
                            }
                            onDoubleClicked: mouse => {
                                if (mouse.button === Qt.LeftButton
                                    && Settings.clickPolicy !== "single")
                                    root.tab.activate(row.index);
                            }
                        }
                    }

                    // Empty space: drops land in the folder being browsed.
                    FileDropArea {
                        anchors.fill: parent
                        z: -2
                        destination: root.tab.path
                        onFilesDropped: urls => root.tab.requestDrop(urls, root.tab.path)
                    }

                    // Empty space: the folder's own menu, no selection.
                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            root.tab.clearSelection();
                            root.tab.focusView();
                            if (mouse.button === Qt.RightButton)
                                root.tab.requestContextMenu();
                        }
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Colors.border
                }
            }

            // ---- preview of the current item (a folder: its contents) ----
            Item {
                id: previewColumn
                readonly property real used: root.ancestors.length * root.ancestorWidth + root.currentWidth
                width: Math.max(320, flick.width - used)
                height: flick.height

                readonly property int row: root.tab.previewRow()

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6
                    visible: preview.location !== ""

                    PreviewPane {
                        id: preview
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        location: previewColumn.row >= 0 ? root.tab.actionPathAt(previewColumn.row) : ""
                        iconSource: previewColumn.row >= 0 ? root.tab.files.valueAt(previewColumn.row, "iconSource") : ""
                        compact: true
                        autoPlay: false
                        interactive: false
                    }

                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: preview.info.name
                        color: Colors.text
                        font.pixelSize: 13
                        font.bold: true
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: preview.summary
                        color: Colors.textDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
}
