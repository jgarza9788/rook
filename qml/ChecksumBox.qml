import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// A file's checksum on request — SHA-256, SHA-1 or MD5 — with Copy and a
// compare against whatever is on the clipboard (the line a download page
// gives you). Shared by the info panel and Properties.
ColumnLayout {
    id: root

    property string path: ""
    // 0 not checked, 1 match, -1 differs
    property int verdict: 0

    spacing: 4

    Checksum {
        id: checksum
        path: root.path
        onStateChanged: root.verdict = 0
    }

    Text {
        textFormat: Text.PlainText
        text: qsTr("Checksum")
        color: Colors.textDim
        font.pixelSize: 10
        font.capitalization: Font.AllUppercase
    }

    RowLayout {
        Layout.fillWidth: true

        OmComboBox {
            Layout.preferredWidth: 110
            model: ["SHA-256", "SHA-1", "MD5"]
            onActivated: checksum.algorithm = ["sha256", "sha1", "md5"][currentIndex]
        }

        OmButton {
            Layout.fillWidth: true
            text: checksum.running ? qsTr("Stop") : qsTr("Compute")
            onClicked: checksum.running ? checksum.cancel() : checksum.start()
        }
    }

    ProgressBar {
        Layout.fillWidth: true
        visible: checksum.running
        value: checksum.progress
    }

    Text {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: checksum.result !== "" || checksum.error !== ""
        text: checksum.error !== "" ? checksum.error : checksum.result
        color: checksum.error !== "" ? Colors.error : Colors.text
        font.family: "monospace"
        font.pixelSize: 11
        wrapMode: Text.WrapAnywhere
    }

    RowLayout {
        Layout.fillWidth: true
        visible: checksum.result !== ""

        OmButton {
            text: qsTr("Copy")
            onClicked: Clipboard.copyText(checksum.result)
        }

        OmButton {
            text: qsTr("Compare with clipboard")
            onClicked: root.verdict = checksum.matches(Clipboard.text()) ? 1 : -1
        }

        Text {
            textFormat: Text.PlainText
            visible: root.verdict !== 0
            text: root.verdict > 0 ? "✓ " + qsTr("match") : "✗ " + qsTr("differs")
            color: root.verdict > 0 ? Colors.text : Colors.error
            font.pixelSize: 12
            font.bold: true
        }
    }
}
