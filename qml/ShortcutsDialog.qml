import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime
import "Keymap.js" as Keymap

// Nautilus's Keyboard Shortcuts window (Ctrl+?, `?` in vim keys), as one
// scrollable themed list. The rows come from Keymap.js, which mirrors what
// Main.qml and Tab.qml bind — when a binding changes, move its row there.
OmDialog {
    id: root
    objectName: "shortcutsDialog"

    anchors.centerIn: Overlay.overlay
    width: 560
    height: Math.min(640, Overlay.overlay ? Overlay.overlay.height - 80 : 640)
    modal: true
    // Nothing to lose here, so a click on the dimmed window closes it too.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Held by a property, not the default one: that would hand it to the
    // contentItem (a ScrollView here and there) before it lifts itself out.
    readonly property Item closeButton: DialogCloseButton { dialog: root }
    title: qsTr("Keyboard Shortcuts")

    // Keymap.js is the one table; vim keys lead while they are on.
    readonly property var allGroups: Keymap.groups(Settings.keyboardMode)

    // The search: every word must appear in the key or its description
    // ("tab new", "ctrl z", "pane"). Groups with nothing left disappear.
    property string query: ""
    readonly property var groups: {
        const words = query.toLowerCase().split(/\s+/).filter(w => w !== "");
        if (words.length === 0)
            return allGroups;
        const out = [];
        for (const group of allGroups) {
            const rows = group.rows.filter(row => {
                const haystack = (row[0] + " " + row[1] + " " + group.name).toLowerCase();
                return words.every(w => haystack.indexOf(w) >= 0);
            });
            if (rows.length > 0)
                out.push({ name: group.name, rows: rows });
        }
        return out;
    }

    onAboutToShow: {
        query = "";
        searchField.text = "";
    }
    onOpened: searchField.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 8

        TextField {
            id: searchField
            objectName: "shortcutSearch"

            Layout.fillWidth: true
            Layout.rightMargin: 28 // clear of the ✕
            placeholderText: qsTr("Search keys — \"tab\", \"ctrl z\", \"preview\"")
            color: Colors.text
            font.pixelSize: 13
            selectByMouse: true
            onTextEdited: root.query = text
            // Esc clears a search first; a second Esc (empty field) closes.
            Keys.onEscapePressed: event => {
                if (text !== "") {
                    text = "";
                    root.query = "";
                    event.accepted = true;
                } else {
                    event.accepted = false;
                }
            }
            Keys.onDownPressed: scroller.list.flick(0, -800)
            Keys.onUpPressed: scroller.list.flick(0, 800)
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: root.groups.length === 0
            text: qsTr("No keys match “%1”").arg(root.query)
            color: Colors.textDim
            font.pixelSize: 12
            topPadding: 12
        }

        ScrollView {
            id: scroller
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            readonly property Flickable list: contentItem

            Column {
                width: scroller.availableWidth
                spacing: 4

                Repeater {
                    model: root.groups

                    Column {
                        required property var modelData
                        width: parent.width
                        spacing: 2

                        Text {
                            textFormat: Text.PlainText
                            text: modelData.name
                            color: Colors.text
                            font.pixelSize: 14
                            font.bold: true
                            topPadding: 10
                            bottomPadding: 4
                        }

                        Repeater {
                            model: modelData.rows

                            Row {
                                required property var modelData
                                width: parent.width
                                spacing: 10

                                Text {
                                    textFormat: Text.PlainText
                                    width: 200
                                    text: modelData[0]
                                    color: Colors.accent
                                    font.pixelSize: 12
                                    font.family: "monospace"
                                }

                                Text {
                                    textFormat: Text.PlainText
                                    text: modelData[1]
                                    color: Colors.textDim
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }

                Item { width: 1; height: 8 }
            }
        }
    }
}
