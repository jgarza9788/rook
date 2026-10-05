import QtQuick
import QtQuick.Controls
import QtQuick.Pdf
import Rook.Runtime

// PDF pages in the quick view (built only with Qt Pdf). Scrolls with the
// wheel; the quick view's +/- zoom does not apply — the page view keeps its
// own fit-to-width.
Item {
    id: root

    property url source

    PdfDocument {
        id: document
        source: root.source
    }

    PdfMultiPageView {
        id: view
        anchors.fill: parent
        document: document
    }

    Text {
        textFormat: Text.PlainText
        anchors.centerIn: parent
        visible: document.status === PdfDocument.Error
        text: qsTr("This PDF could not be opened")
        color: Colors.error
        font.pixelSize: 13
    }
}
