import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// The built-in quick view (Space): a preview drawn inside the window, so
// nothing has to start — the next file is a keypress away, not a process
// launch. Follows the owning tab's selection; j/k (arrows) step through the
// folder, +/-/0 zoom, p shrinks it to a corner (picture-in-picture) so the
// files keep the keys, Enter opens, Space or Esc closes. QuickViewInfo does
// the reading off the UI thread; this file only draws.
FocusScope {
    id: root

    // The Tab the preview belongs to; null while closed.
    property Item tab: null
    readonly property bool open: tab !== null
    // Zoom for images and text: 1 = fit (images) / normal size (text).
    property real zoom: 1
    // Markdown: rendered (default) or the raw source.
    property bool markdownRaw: false
    // Picture-in-picture: a small card in the corner while the keys go back
    // to the files — browse with j/k and the card follows the selection.
    property bool pip: false

    readonly property int row: tab ? tab.previewRow() : -1
    readonly property string location: tab && row >= 0 ? tab.actionPathAt(row) : ""
    readonly property alias info: preview.info
    readonly property string iconSource: tab && row >= 0 ? tab.files.valueAt(row, "iconSource") : ""

    signal closed()

    function show(forTab) {
        tab = forTab;
        zoom = 1;
        markdownRaw = false;
        pip = false;
        forceActiveFocus();
        openMotion.restart();
    }

    // p: shrink to the corner (keys to the files) or grow back (keys here).
    // True only while switching to/from PiP, so the card glides then — a
    // window resize just follows.
    property bool gliding: false

    function togglePip() {
        if (!tab)
            return;
        gliding = true;
        glideEnd.restart();
        pip = !pip;
        if (pip)
            tab.focusView();
        else
            forceActiveFocus();
    }

    function close() {
        if (!tab)
            return;
        const owner = tab;
        tab = null;
        pip = false;
        owner.forceActiveFocus();
        root.closed();
    }

    function step(delta) {
        if (!tab)
            return;
        tab.moveCurrent(delta, false);
        zoom = 1;
    }

    visible: open
    onLocationChanged: zoom = 1

    Keys.onPressed: event => {
        const vim = Settings.keyboardMode === "vim";
        const k = event.key;
        if (k === Qt.Key_Escape || (k === Qt.Key_Space && info.kind !== "media")) {
            root.close();
        } else if (k === Qt.Key_Down || k === Qt.Key_Right || (vim && (k === Qt.Key_J || k === Qt.Key_L))) {
            root.step(1);
        } else if (k === Qt.Key_Up || k === Qt.Key_Left || (vim && (k === Qt.Key_K || k === Qt.Key_H))) {
            root.step(-1);
        } else if (k === Qt.Key_Plus || k === Qt.Key_Equal) {
            root.zoom = Math.min(8, root.zoom * 1.25);
        } else if (k === Qt.Key_Minus) {
            root.zoom = Math.max(0.25, root.zoom / 1.25);
        } else if (k === Qt.Key_0) {
            root.zoom = 1;
        } else if (k === Qt.Key_P) {
            root.togglePip();
        } else if (k === Qt.Key_M && info.kind === "markdown") {
            root.markdownRaw = !root.markdownRaw;
        } else if (k === Qt.Key_Return || k === Qt.Key_Enter) {
            const owner = root.tab;
            const at = root.row;
            root.close();
            owner.activate(at);
        } else if (k === Qt.Key_Home || (vim && k === Qt.Key_G && !(event.modifiers & Qt.ShiftModifier))) {
            root.tab.setCurrent(0, false);
        } else if (k === Qt.Key_End || (vim && k === Qt.Key_G)) {
            root.tab.setCurrent(root.tab.files.count - 1, false);
        } else {
            return;
        }
        event.accepted = true;
    }

    Timer {
        id: glideEnd
        interval: 220
        onTriggered: root.gliding = false
    }

    // The shared open motion (Colors.popInMs): the card settles in, the dim
    // fades up behind it. Closing is instant, like every surface.
    ParallelAnimation {
        id: openMotion
        NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: Colors.fadeInMs; easing.type: Easing.OutCubic }
        NumberAnimation { target: card; property: "scale"; from: Colors.popInScale; to: 1; duration: Colors.popInMs; easing.type: Easing.OutCubic }
        NumberAnimation { target: dimmer; property: "opacity"; from: 0; to: 0.35; duration: Colors.popInMs; easing.type: Easing.OutCubic }
    }

    // The dimmed files behind; a click there closes, like any popover.
    Rectangle {
        id: dimmer
        anchors.fill: parent
        visible: !root.pip
        color: "black"
        opacity: 0.35

        MouseArea {
            anchors.fill: parent
            onClicked: root.close()
            onWheel: wheel => wheel.accepted = true
        }
    }

    Rectangle {
        id: card

        // Full: inset over the files. PiP: a 16:10-ish card bottom-right.
        readonly property real inset: Math.min(48, root.width * 0.05)
        readonly property real pipWidth: Math.max(280, Math.min(520, root.width * 0.38))
        x: root.pip ? root.width - width - 16 : inset
        y: root.pip ? root.height - height - 16 : inset
        width: root.pip ? pipWidth : root.width - 2 * inset
        height: root.pip ? Math.min(root.height - 32, pipWidth * 0.68) : root.height - 2 * inset
        Behavior on x { enabled: root.gliding; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on y { enabled: root.gliding; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on width { enabled: root.gliding; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        Behavior on height { enabled: root.gliding; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        transformOrigin: Item.Center
        radius: Colors.radius
        color: Colors.chrome
        // The accent frame says "you are in the preview now": keys go here
        // until Space or Esc hands them back to the files.
        border.color: Colors.accent
        border.width: 2
        clip: true

        // Swallow clicks so they do not reach the dimmer (or, in PiP, the
        // files under the card); a double-click grows the card back.
        MouseArea {
            anchors.fill: parent
            onDoubleClicked: if (root.pip) root.togglePip()
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 0

            // ---- header: name, facts, position in the folder ----
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 6
                    spacing: 10

                    Image {
                        source: root.iconSource !== "" ? Colors.fileIcon(root.iconSource, Colors.textDim, 20) : ""
                        sourceSize: Qt.size(20, 20)
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            textFormat: Text.PlainText
                            width: parent.width
                            text: info.name || Platform.baseName(root.location)
                            color: Colors.text
                            font.pixelSize: 13
                            font.bold: true
                            elide: Text.ElideMiddle
                        }

                        Text {
                            textFormat: Text.PlainText
                            width: parent.width
                            color: Colors.textDim
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            text: preview.summary
                                  + (root.zoom !== 1 ? "  ·  " + Math.round(root.zoom * 100) + "%" : "")
                        }
                    }

                    Text {
                        textFormat: Text.PlainText
                        visible: root.tab !== null && root.row >= 0 && !root.pip
                        text: root.tab ? (root.row + 1) + " / " + root.tab.files.count : ""
                        color: Colors.textDim
                        font.pixelSize: 11
                    }

                    ToolbarButton {
                        symbol: root.pip ? "⤢" : "⧉"
                        tip: root.pip ? qsTr("Full size (p)") : qsTr("Picture-in-picture (p)")
                        onTriggered: root.togglePip()
                    }

                    ToolbarButton {
                        visible: !root.pip
                        symbol: "↗"
                        tip: qsTr("Open (Enter)")
                        onTriggered: {
                            const owner = root.tab;
                            const at = root.row;
                            root.close();
                            owner.activate(at);
                        }
                    }

                    ToolbarButton {
                        symbol: "✕"
                        symbolSize: 13
                        tip: qsTr("Close (Space / Esc)")
                        onTriggered: root.close()
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Colors.border
                }
            }

            // ---- body: one renderer per kind ----
            PreviewPane {
                id: preview
                Layout.fillWidth: true
                Layout.fillHeight: true
                location: root.open ? root.location : ""
                iconSource: root.iconSource
                zoom: root.zoom
                markdownRaw: root.markdownRaw
                onCloseRequested: root.close()
            }
        }
    }
}
