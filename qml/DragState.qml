pragma Singleton

import QtQuick
import Rook.Runtime

// What a file drag over rook would do right now, shared by every drop
// target and the label beside the pointer ("+ Copy to “Photos”").
//
// The action follows the modifiers, Nautilus's and Windows's convention:
//   Ctrl              copy
//   Shift             move
//   Ctrl+Shift / Alt  link
//   none              move within one filesystem, copy across
// The keys are re-read on a short timer while a target is hovered, so the
// label changes the moment a key goes down — no mouse movement needed.
QtObject {
    id: state

    // The drop target being hovered, or null.
    property Item area: null
    property var paths: []
    property string destination: ""
    // "copy" | "move" | "link" | "trash" | "star" — what a drop does.
    property string action: ""
    // Scene position of the pointer, and the window it is in.
    property real x: 0
    property real y: 0
    property var window: null
    // rook's own drag: the card ([▣ 5 items | Move]) is drawn by the
    // window under the pointer, live, from these — the native drag picture
    // is blank, since Qt cannot change it mid-drag. False for drags from
    // other apps, which get a badge beside the pointer instead.
    property bool ownDrag: false
    property string cardText: ""
    property url cardIcon: ""
    property point cardHotSpot: Qt.point(28, 28)

    readonly property bool active: area !== null && action !== ""
    // Trash is the one destructive drop: drawn in the error colour.
    readonly property bool destructive: action === "trash"
    // The one word that goes beside the drag card's "5 items".
    readonly property string word: {
        switch (action) {
        case "copy": return qsTr("Copy");
        case "move": return qsTr("Move");
        case "link": return qsTr("Link");
        case "trash": return qsTr("Trash");
        case "star": return qsTr("Star");
        }
        return "";
    }
    readonly property string label: {
        const where = destination === "" ? "" : "“" + Platform.baseName(destination) + "”";
        switch (action) {
        case "copy": return "+  " + qsTr("Copy to %1").arg(where);
        case "move": return "→  " + qsTr("Move to %1").arg(where);
        case "link": return "↗  " + qsTr("Link in %1").arg(where);
        case "trash": return "→  " + qsTr("Move to Trash");
        case "star": return "★  " + qsTr("Star");
        }
        return "";
    }

    // The action for these sources landing in `target`, from the keys held
    // now. Trash and Starred have one meaning each.
    function actionFor(sources, target) {
        if (target === "trash:///")
            return "trash";
        if (target === "starred:///")
            return "star";
        if (sources.length === 0 || !target)
            return "";
        const mods = Platform.keyboardModifiers();
        const ctrl = (mods & Qt.ControlModifier) !== 0;
        const shift = (mods & Qt.ShiftModifier) !== 0;
        if ((ctrl && shift) || (mods & Qt.AltModifier))
            return "link";
        if (ctrl)
            return "copy";
        if (shift)
            return "move";
        return Platform.sameFilesystem(sources[0], target) ? "move" : "copy";
    }

    function hover(dropArea, urls, target, sceneX, sceneY, win) {
        if (area !== dropArea) {
            area = dropArea;
            paths = Platform.locationsFromUrls(urls);
            destination = target;
        }
        x = sceneX;
        y = sceneY;
        window = win;
        refresh();
    }

    function leave(dropArea) {
        if (area === dropArea) {
            area = null;
            action = "";
            paths = [];
        }
    }

    function refresh() {
        if (!area)
            return;
        // A drop into the folder something already lives in is no move at
        // all; say nothing rather than promise one.
        const next = actionFor(paths, destination);
        const noop = (next === "move" || next === "") && paths.length > 0
                     && paths.every(p => Platform.parentPath(p) === destination);
        action = noop || paths.indexOf(destination) >= 0 ? "" : next;
    }

    // Keys can change while the pointer rests; a dead area (its delegate
    // destroyed, the drag cancelled) is let go here too.
    property Timer poll: Timer {
        interval: 100
        repeat: true
        running: state.area !== null
        onTriggered: {
            if (!state.area || !state.area.containsDrag)
                state.leave(state.area);
            else
                state.refresh();
        }
    }
}
