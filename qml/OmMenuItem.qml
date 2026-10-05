import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// One menu row: an icon (a flat glyph from the icon provider, or an app's own
// icon), the label, and its shortcut in the current keyboard mode. Hover and
// keyboard focus light an accent-tinted pill with an accent bar on the left;
// destructive rows (trash, delete) light red instead.
MenuItem {
    id: item

    // A glyph key (IconImageProvider: "copy", "trash", …) tinted to the theme.
    property string glyph: ""
    // An app's own icon (Open With), shown as is.
    property url iconUrl: ""
    property bool destructive: false
    // The shortcut to show: `vimKey` while vim keys are on, else `shortcut`.
    property string shortcut: ""
    property string vimKey: ""
    // A small uppercase caption ("SORT") rather than an action.
    property bool sectionHeader: false

    readonly property string shownShortcut: Settings.keyboardMode === "vim" && vimKey !== ""
                                            ? vimKey : shortcut
    readonly property bool lit: highlighted && enabled && !sectionHeader
    readonly property color tone: destructive ? Colors.error : Colors.accent

    implicitHeight: sectionHeader ? 24 : 30
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: !sectionHeader
    focusPolicy: sectionHeader ? Qt.NoFocus : Qt.StrongFocus

    indicator: null
    arrow: Image {
        x: item.width - width - 8
        y: (item.height - height) / 2
        visible: item.subMenu !== null
        width: 12
        height: 12
        sourceSize: Qt.size(24, 24)
        source: Colors.tint("image://fileicon/chevron", item.lit ? item.tone : Colors.textDim)
    }

    contentItem: Item {
        // Read by the tests: the label's colour, dim when disabled.
        readonly property color color: label.color
        implicitWidth: row.implicitWidth + (item.subMenu ? 18 : 0)
        implicitHeight: row.implicitHeight

        RowLayout {
            id: row
            anchors.fill: parent
            anchors.rightMargin: item.subMenu ? 18 : 0
            spacing: 10

            Item {
                visible: !item.sectionHeader
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16

                Image {
                    anchors.fill: parent
                    // A checked row shows the check where its icon would be.
                    readonly property string glyphKey: item.checkable ? (item.checked ? "check" : "")
                                                     : item.glyph !== "" ? item.glyph
                                                     : item.subMenu && item.subMenu.glyph ? item.subMenu.glyph : ""
                    visible: glyphKey !== "" || item.iconUrl.toString() !== ""
                    sourceSize: Qt.size(32, 32)
                    source: item.iconUrl.toString() !== "" ? item.iconUrl
                          : glyphKey !== "" ? Colors.tint("image://fileicon/" + glyphKey,
                                                        item.lit || (item.checkable && item.checked) ? item.tone
                                                      : Colors.textDim)
                          : ""
                    opacity: item.enabled ? 1 : 0.4
                }
            }

            Text {
                id: label
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: item.text
                elide: Text.ElideMiddle
                font.pixelSize: item.sectionHeader ? 10 : 13
                font.bold: item.sectionHeader
                font.capitalization: item.sectionHeader ? Font.AllUppercase : Font.MixedCase
                font.letterSpacing: item.sectionHeader ? 0.8 : 0
                color: item.sectionHeader ? Colors.textDim
                     : !item.enabled ? Qt.alpha(Colors.textDim, 0.7)
                     : item.lit ? item.tone
                     : Colors.text
                Behavior on color { ColorAnimation { duration: 90 } }
            }

            Text {
                textFormat: Text.PlainText
                visible: item.shownShortcut !== "" && !item.sectionHeader
                text: item.shownShortcut
                color: item.lit ? Qt.alpha(item.tone, 0.85) : Colors.textDim
                opacity: item.enabled ? 1 : 0.5
                font.pixelSize: 11
                font.family: "monospace"
            }
        }
    }

    background: Item {
        implicitWidth: 220
        implicitHeight: item.implicitHeight

        Rectangle {
            id: pill
            anchors.fill: parent
            anchors.leftMargin: 2
            anchors.rightMargin: 2
            radius: Colors.radius
            color: item.lit ? Qt.alpha(item.tone, 0.16) : "transparent"
            Behavior on color { ColorAnimation { duration: 90 } }
        }

        // The accent bar: grows in from the middle as the row lights.
        Rectangle {
            anchors.left: pill.left
            anchors.verticalCenter: pill.verticalCenter
            width: 3
            radius: 1.5
            height: item.lit ? pill.height - 12 : 0
            color: item.tone
            Behavior on height { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
        }
    }
}
