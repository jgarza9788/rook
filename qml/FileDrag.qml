import QtQuick
import Rook
import Rook.Runtime

// A native file drag with a bounded preview, independent of row/cell geometry.
// The transparent parent keeps the card out of the view; grabToImage on the
// card renders its own contents without inheriting the parent's opacity.
//
// The drag itself runs in DragSource, which outlives this item: a delegate
// can be destroyed mid-drag (a spring-loaded folder opening rebuilds the
// list), and a drag owned here took its data down with it.
Item {
    id: root

    required property bool pressed
    required property bool dragging

    property var paths: []
    property var grabResult: null
    // DragSource's token for the drag this item started; 0 when none.
    property int token: 0
    property bool ready: false
    property bool capturePending: false
    property int generation: 0
    property int itemCount: 0
    property string fileName: ""
    property url previewSource: ""
    property url fallbackSource: ""
    opacity: 0

    // Start once the gesture is a drag and the preview card is captured —
    // checked on either change, not bound: the start writes `token`, and a
    // binding over `token` would loop.
    function maybeStart() {
        if (!dragging || !ready || token !== 0)
            return;
        // The window draws the card (DragState); the native picture is
        // left blank so there is only ever one label.
        DragState.cardText = label.text;
        DragState.cardIcon = preview.status === Image.Ready ? root.previewSource : root.fallbackSource;
        DragState.cardHotSpot = Qt.point(28, 28);
        DragState.ownDrag = true;
        token = DragSource.start(paths, Qt.point(28, 28));
        if (token === 0)
            DragState.ownDrag = false; // another drag is still running
    }
    onReadyChanged: maybeStart()

    Connections {
        target: DragSource
        function onFinished(finishedToken, action) {
            if (finishedToken === root.token) {
                DragState.ownDrag = false;
                root.reset();
            }
        }
    }

    function reset() {
        ++generation;
        capturePending = false;
        captureTimeout.stop();
        ready = false;
        grabResult = null;
        token = 0;
    }

    function prepare(paths, name, source, fallback) {
        reset();
        root.paths = paths;
        itemCount = paths.length;
        fileName = name;
        fallbackSource = fallback;
        previewSource = source;
        capturePending = true;
        // A ready thumbnail is preferred, but a slow remote thumbnail must
        // never hold up dragging: after a short grace period use its icon.
        captureTimeout.restart();
        captureCheck.restart();
    }

    function captureIfReady() {
        if (preview.status !== Image.Loading)
            capture();
    }

    function capture() {
        if (!capturePending || !pressed)
            return;
        capturePending = false;
        captureTimeout.stop();
        const request = generation;
        const started = card.grabToImage(result => {
            if (request !== generation || !pressed)
                return;
            // Retain the grab for the entire drag: its URL is backed by it.
            grabResult = result;
            ready = true;
        });
        // Even if a window cannot be captured, the file payload can be dragged.
        if (!started)
            ready = true;
    }

    // Destruction clears the context before the item itself is deleted;
    // neither timer may fire into that gap.
    Component.onDestruction: {
        captureCheck.stop();
        captureTimeout.stop();
    }

    // A drag in flight finishes through DragSource; only an unstarted one
    // is dropped when the press ends.
    onPressedChanged: if (!pressed && !dragging && token === 0) reset()
    onDraggingChanged: {
        if (!pressed && !dragging && token === 0)
            reset();
        else
            maybeStart();
    }

    // Deferred to the next event-loop pass, like Qt.callLater, but owned by
    // this item: a view left right after a press tears the delegate down,
    // and a queued Qt.callLater would still run against its dead context.
    Timer {
        id: captureCheck
        interval: 0
        onTriggered: root.captureIfReady()
    }

    Timer {
        id: captureTimeout
        interval: 80
        onTriggered: root.capture()
    }

    Rectangle {
        id: card

        width: Math.min(300, Math.max(120, label.implicitWidth + 70))
        height: 56
        radius: Colors.radius
        color: Qt.alpha(Colors.chrome, 1)
        border.color: Colors.border

        Image {
            id: fallback

            x: 10
            y: 10
            width: 36
            height: 36
            source: root.fallbackSource
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectFit
            visible: preview.status !== Image.Ready
        }

        Image {
            id: preview

            anchors.fill: fallback
            source: root.previewSource
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            visible: status === Image.Ready
            onStatusChanged: captureCheck.restart()
        }

        Text {
            id: label

            x: 56
            width: parent.width - x - 12
            height: parent.height
            text: root.itemCount > 1 ? qsTr("%1 items").arg(root.itemCount) : root.fileName
            textFormat: Text.PlainText
            font.pixelSize: 13
            color: Colors.text
            elide: Text.ElideMiddle
            verticalAlignment: Text.AlignVCenter
        }
    }
}
