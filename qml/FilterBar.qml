import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// The in-folder filter, `/` in vim keys (Ctrl+Shift+S in classic). Narrows the
// listing as you type: plain text, a glob when the text has * ? [, a regex
// after `re:` or with .* lit (Alt+R). Enter keeps the filter and hands the
// keys back to the files; Esc clears it. While kept, the bar stays as a
// reminder of why the folder looks short — `/` reopens it.
Rectangle {
    id: bar

    required property Item tab

    function focusField() {
        // Re-sync first: typing can detach the text binding, and a cleared
        // filter must not reopen showing the old pattern.
        field.text = tab.filterText;
        field.forceActiveFocus();
        field.selectAll();
    }

    implicitHeight: 32
    color: Colors.chrome
    visible: tab.filterActive

    Rectangle {
        width: parent.width
        height: 1
        color: Colors.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 8
        spacing: 8

        Text {
            textFormat: Text.PlainText
            text: "/"
            color: Colors.accent
            font.pixelSize: 15
            font.bold: true
        }

        TextField {
            id: field
            objectName: "filterField"

            Layout.fillWidth: true
            Layout.preferredHeight: 26
            color: bar.tab.filterError ? Colors.error : Colors.text
            font.pixelSize: 13
            selectByMouse: true
            placeholderText: qsTr("Filter this folder — text, *.glob or re:regex")
            background: Rectangle {
                radius: 4
                color: Colors.window
                border.width: 1
                border.color: bar.tab.filterError ? Colors.error
                            : field.activeFocus ? Colors.accent : Colors.border
            }
            onTextEdited: bar.tab.filterText = text
            onActiveFocusChanged: if (activeFocus) bar.tab.filterEditing = true

            Keys.onPressed: event => {
                if (event.key === Qt.Key_Escape) {
                    bar.tab.clearFilter();
                    event.accepted = true;
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter
                           || event.key === Qt.Key_Down) {
                    bar.tab.commitFilter();
                    event.accepted = true;
                } else if (event.key === Qt.Key_R && (event.modifiers & Qt.AltModifier)) {
                    bar.tab.toggleFilterMode();
                    event.accepted = true;
                }
            }
        }

        Text {
            textFormat: Text.PlainText
            visible: bar.tab.filterError !== ""
            text: bar.tab.filterError
            color: Colors.error
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.maximumWidth: 220
        }

        // .* — regex mode, the same chip the search bar carries.
        Rectangle {
            implicitWidth: regexLabel.implicitWidth + 14
            implicitHeight: 22
            radius: 4
            readonly property bool on: bar.tab.filterMode === "regex"
            color: on ? Colors.selection : regexMouse.containsMouse ? Colors.hover : "transparent"
            border.color: on ? Colors.selection : Colors.border
            border.width: 1

            Text {
                id: regexLabel
                textFormat: Text.PlainText
                anchors.centerIn: parent
                text: ".*"
                color: parent.on ? Colors.selectionText : Colors.textDim
                font.pixelSize: 12
                font.family: "monospace"
            }

            MouseArea {
                id: regexMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.tab.toggleFilterMode()
            }

            ToolTip.visible: regexMouse.containsMouse
            ToolTip.text: qsTr("Regular expression (Alt+R)")
            ToolTip.delay: 600
        }

        ToolbarButton {
            symbol: "✕"
            symbolSize: 12
            implicitWidth: 26
            implicitHeight: 26
            tip: qsTr("Clear filter (Esc)")
            onTriggered: bar.tab.clearFilter()
        }
    }
}
