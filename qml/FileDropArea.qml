import QtQuick
import Rook.Runtime

// A place files can be dropped: a folder row or cell, a folder's background,
// a sidebar place, a tab. Shows what the drop will do beside the pointer
// (DragState) and, for folders, springs open after a rest
// (Settings.springLoadDelay) — keep holding and dig further down.
DropArea {
    id: root

    // Where dropped files go.
    property string destination: ""
    // Open the destination after the pointer rests here mid-drag.
    property bool springLoaded: false
    // A drop: the owner decides what it means (a tab transfers, the sidebar
    // trashes or stars).
    signal filesDropped(var urls)
    // The spring fired: the owner opens the place.
    signal sprung()

    readonly property int springDelay: Settings.springLoadDelay === "off" ? 0
                                       : Math.round(parseFloat(Settings.springLoadDelay) * 1000)

    function track(drag) {
        const scene = root.mapToItem(null, drag.x, drag.y);
        DragState.hover(root, drag.urls, root.destination, scene.x, scene.y, root.Window.window);
    }

    onEntered: drag => {
        track(drag);
        if (springLoaded && springDelay > 0)
            spring.restart();
    }
    onPositionChanged: drag => track(drag)
    onExited: {
        spring.stop();
        DragState.leave(root);
    }
    onDropped: drop => {
        spring.stop();
        DragState.leave(root);
        // No destination (the window's catch-all): refuse, so a source app
        // never thinks its files were taken — a "moved" answer could make it
        // delete them.
        if (root.destination === "") {
            drop.accepted = false;
            return;
        }
        root.filesDropped(drop.urls);
        drop.accept();
    }
    Component.onDestruction: DragState.leave(root)

    Timer {
        id: spring
        interval: Math.max(1, root.springDelay)
        onTriggered: if (root.containsDrag) root.sprung()
    }
}
