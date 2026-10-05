import QtQuick
import QtQuick.Controls
import Rook.Runtime

// A popover (search filters, server protocols, file operations): the shared
// card and the shared open motion, growing from the edge it hangs off —
// `transformOrigin: Popup.Bottom` for one that opens upward.
Popup {
    padding: 14
    transformOrigin: Popup.Top
    background: OmCard {}

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Colors.fadeInMs; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: Colors.popInScale; to: 1; duration: Colors.popInMs; easing.type: Easing.OutCubic }
        }
    }
}
