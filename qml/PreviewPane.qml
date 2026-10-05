import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// One item's preview, drawn by kind (QuickViewInfo decides which): image,
// text, Markdown, folder or archive listing, audio/video, PDF, or an info
// card. Shared by the quick view, the Columns view's preview column and the
// Gallery view, so all three show a file the same way.
Item {
    id: root

    property string location: ""
    property string iconSource: ""
    // Images and text: 1 = fit / normal size.
    property real zoom: 1
    // Markdown: rendered (default) or the raw source.
    property bool markdownRaw: false
    // Smaller type and no "first N shown" notes — for the preview column.
    property bool compact: false
    // Audio/video: start playing on its own, and take the keys (Space to
    // pause). On in the quick view; off where the file list keeps the keys.
    property bool autoPlay: true
    property bool interactive: true

    readonly property alias info: info
    readonly property alias sizer: sizer
    readonly property url fileUrl: info.url
    // type · size (or a folder's count and total) · dimensions · modified
    readonly property string summary: {
        const parts = [];
        if (info.typeDescription)
            parts.push(info.typeDescription);
        if (info.kind === "folder") {
            parts.push(Platform.formatItemCount(info.entryCount));
            if (sizer.size > 0)
                parts.push(Platform.formatSize(sizer.size) + (sizer.measuring ? "…" : ""));
        } else if (info.size >= 0) {
            parts.push(Platform.formatSize(info.size));
        }
        if (info.imageWidth > 0)
            parts.push(info.imageWidth + " × " + info.imageHeight);
        if (info.modified)
            parts.push(Platform.formatModified(info.modified, Settings.dateTimeFormat));
        return parts.join("  ·  ");
    }

    // Esc inside the media player.
    signal closeRequested()

    onLocationChanged: info.path = location

    QuickViewInfo {
        id: info
    }

    // Properties' own measurement (GIO, async, cancelled on the next item)
    // gives a folder its total.
    FileProperties {
        id: sizer
        paths: info.kind === "folder" && Platform.isLocal(root.location) ? [root.location] : []
    }

    Loader {
        id: body
        anchors.fill: parent
        active: root.location !== ""
        sourceComponent: {
            if (info.loading)
                return loadingView;
            switch (info.kind) {
            case "image":
            case "animated": return imageView;
            case "text": return textView;
            case "markdown": return root.markdownRaw ? textView : markdownView;
            case "folder":
            case "archive": return listingView;
            case "media": return mediaView;
            case "pdf": return pdfView;
            }
            return infoView;
        }
    }

    Component {
        id: loadingView
        Item {
            BusyIndicator {
                anchors.centerIn: parent
                running: true
            }
        }
    }

    Component {
        id: imageView

        Flickable {
            id: flick
            clip: true
            contentWidth: Math.max(width, frame.width)
            contentHeight: Math.max(height, frame.height)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}

            // Fit first, then zoom from there — never upscale a small picture
            // past its own pixels at "fit".
            readonly property real fit: info.imageWidth > 0
                ? Math.min(1, (width - 24) / info.imageWidth, (height - 24) / info.imageHeight) : 1

            Item {
                id: frame
                width: Math.max(1, info.imageWidth * flick.fit * root.zoom)
                height: Math.max(1, info.imageHeight * flick.fit * root.zoom)
                x: Math.max(0, (flick.width - width) / 2)
                y: Math.max(0, (flick.height - height) / 2)

                Loader {
                    anchors.fill: parent
                    sourceComponent: info.kind === "animated" ? animated : still
                }
            }

            Component {
                id: still
                Image {
                    source: root.fileUrl
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    // Decode at the size shown (capped), not the file's own —
                    // a 100-megapixel photo should not cost 400 MB to peek at.
                    sourceSize: Qt.size(Math.min(4096, Math.ceil(frame.width * Screen.devicePixelRatio)),
                                        Math.min(4096, Math.ceil(frame.height * Screen.devicePixelRatio)))
                }
            }

            Component {
                id: animated
                AnimatedImage {
                    source: root.fileUrl
                    asynchronous: true
                    fillMode: Image.PreserveAspectFit
                    playing: true
                }
            }

            WheelHandler {
                acceptedModifiers: Qt.ControlModifier
                onWheel: event => root.zoom = event.angleDelta.y > 0
                    ? Math.min(8, root.zoom * 1.15) : Math.max(0.25, root.zoom / 1.15)
            }
        }
    }

    Component {
        id: textView

        Flickable {
            clip: true
            contentWidth: textRow.width + 24
            contentHeight: textColumn.height + 24
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}

            Column {
                id: textColumn
                x: 12
                y: 12
                spacing: 8

                Row {
                    id: textRow
                    spacing: 14

                    Text {
                        textFormat: Text.PlainText
                        text: info.lineNumbers
                        color: Colors.textDim
                        opacity: 0.7
                        horizontalAlignment: Text.AlignRight
                        font.family: "monospace"
                        font.pixelSize: Math.round((root.compact ? 10 : 12) * root.zoom)
                    }

                    TextEdit {
                        textFormat: TextEdit.PlainText
                        text: info.text
                        readOnly: true
                        selectByMouse: true
                        color: Colors.text
                        selectionColor: Colors.selection
                        selectedTextColor: Colors.selectionText
                        font.family: "monospace"
                        font.pixelSize: Math.round((root.compact ? 10 : 12) * root.zoom)
                    }
                }

                Text {
                    textFormat: Text.PlainText
                    visible: info.truncated && !root.compact
                    text: qsTr("— preview stops here; press Enter to open the whole file —")
                    color: Colors.textDim
                    font.pixelSize: 11
                    font.italic: true
                }
            }
        }
    }

    Component {
        id: markdownView

        Flickable {
            id: mdFlick
            clip: true
            contentWidth: width
            contentHeight: md.height + 40
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Text {
                id: md
                // Rendered by QuickViewInfo::safeMarkdownHtml: images and raw
                // HTML are gone before this ever sees it, and links do not
                // activate (no onLinkActivated) — reading, not browsing.
                textFormat: Text.RichText
                x: 20
                y: 20
                width: Math.min(mdFlick.width - 40, 860)
                wrapMode: Text.Wrap
                text: info.html
                color: Colors.text
                linkColor: Colors.text
                font.pixelSize: Math.round((root.compact ? 12 : 14) * root.zoom)
            }
        }
    }

    Component {
        id: listingView

        ColumnLayout {
            spacing: 0

            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                Layout.margins: 12
                color: info.error !== "" ? Colors.error : Colors.textDim
                font.pixelSize: root.compact ? 11 : 12
                wrapMode: Text.Wrap
                text: {
                    if (info.error !== "")
                        return info.error;
                    if (info.kind === "archive") {
                        const n = info.entryCount === 1 ? qsTr("1 entry") : qsTr("%1 entries").arg(info.entryCount);
                        return n + (info.truncated && !root.compact ? qsTr(" (first %1 shown)").arg(info.entryCount) : "")
                            + "  ·  " + qsTr("%1 uncompressed").arg(Platform.formatSize(info.entriesBytes));
                    }
                    const shown = info.truncated && !root.compact ? qsTr("first %1 shown").arg(info.entries.length) : "";
                    const size = sizer.paths.length === 0 ? ""
                               : sizer.measuring ? qsTr("counting… %1").arg(Platform.formatSize(sizer.size))
                               : qsTr("%1 in %2 files, %3 folders").arg(Platform.formatSize(sizer.size))
                                                                  .arg(sizer.fileCount).arg(sizer.folderCount);
                    return [Platform.formatItemCount(info.entryCount), size, shown]
                        .filter(s => s !== "").join("  ·  ");
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: info.entries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    required property var modelData
                    width: ListView.view.width
                    height: 24

                    Text {
                        textFormat: Text.PlainText
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: sizeText.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: (modelData.isDir ? "▸ " : "   ") + modelData.name
                        color: modelData.isDir ? Colors.folder : Colors.text
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }

                    Text {
                        id: sizeText
                        textFormat: Text.PlainText
                        anchors.right: parent.right
                        anchors.rightMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.size >= 0 ? Platform.formatSize(modelData.size) : ""
                        color: Colors.textDim
                        font.pixelSize: 11
                    }
                }
            }
        }
    }

    Component {
        id: mediaView
        Loader {
            source: info.hasMedia ? "QuickViewMedia.qml" : ""
            onLoaded: {
                item.autoPlay = root.autoPlay;
                item.source = root.fileUrl;
                if (root.interactive)
                    item.forceActiveFocus();
            }
            // Esc inside the player still closes the preview.
            Connections {
                target: item
                ignoreUnknownSignals: true
                function onCloseRequested() { root.closeRequested(); }
            }
        }
    }

    Component {
        id: pdfView
        Loader {
            source: info.hasPdf ? "QuickViewPdf.qml" : ""
            onLoaded: item.source = root.fileUrl
        }
    }

    Component {
        id: infoView

        Column {
            spacing: 10
            topPadding: root.compact ? 16 : 40

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                readonly property int size: root.compact ? 64 : 128
                source: root.iconSource !== "" ? Colors.fileIcon(root.iconSource, Colors.textDim, size) : ""
                sourceSize: Qt.size(size, size)
            }

            Text {
                textFormat: Text.PlainText
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 40, implicitWidth)
                horizontalAlignment: Text.AlignHCenter
                text: info.name
                color: Colors.text
                font.pixelSize: root.compact ? 13 : 16
                font.bold: true
                elide: Text.ElideMiddle
            }

            Text {
                textFormat: Text.PlainText
                anchors.horizontalCenter: parent.horizontalCenter
                text: info.error !== "" ? info.error
                    : Platform.isLocal(root.location) ? qsTr("No preview for this type — Enter opens it")
                    : qsTr("Previews are for local files")
                color: info.error !== "" ? Colors.error : Colors.textDim
                font.pixelSize: 12
            }
        }
    }
}
