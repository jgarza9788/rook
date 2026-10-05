import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime

// The command palette (Ctrl+Shift+P, vim `:`): one box that runs every
// command, goes anywhere and filters the view. The first character picks the
// mode, as in the design doc's prefix table; the chips under the field show
// the modes and switch between them with a click or Tab on an empty query.
Popup {
    id: palette
    objectName: "commandPalette"

    // The window (Main.qml's root) and its command registry.
    required property var win
    required property var registry

    // Every mode, in chip order. `ready: false` modes are planned in the
    // design doc and only say so.
    readonly property var modes: [
        { prefix: "",  label: qsTr("commands"), ready: true },
        { prefix: "/", label: qsTr("path"),     ready: true },
        { prefix: "~", label: qsTr("places"),   ready: true },
        { prefix: "@", label: qsTr("filter"),   ready: true },
        { prefix: "?", label: qsTr("search"),   ready: true },
        { prefix: "=", label: qsTr("calc"),     ready: true },
        { prefix: "#", label: qsTr("tags"),     ready: false },
        { prefix: ">", label: qsTr("shell"),    ready: false },
        { prefix: "!", label: qsTr("scripts"),  ready: false }
    ]

    readonly property string mode: {
        const first = field.text.charAt(0);
        for (const m of modes)
            if (m.prefix !== "" && m.prefix === first)
                return m.prefix;
        return "";
    }
    // In path mode the slash is both the mode and the root: "/etc/nginx".
    readonly property string query: mode === "" || mode === "/" ? field.text : field.text.substring(1)

    // Most recently run first; ranks the command list while the query is
    // empty. Per window and per run for now — the state DB will keep it.
    property var recent: []

    // The filter text when the palette opened in @ mode, restored on Esc.
    property string filterBefore: ""
    property bool filterTouched: false

    property var results: []

    function openWith(prefix) {
        field.text = prefix || "";
        filterTouched = false;
        open();
    }

    function setMode(prefix) {
        const rest = mode === "/" ? query.replace(/^\/+/, "") : query;
        field.text = prefix + rest;
        field.forceActiveFocus();
        field.cursorPosition = field.text.length;
    }

    function cycleMode(step) {
        const ready = modes.filter(m => m.ready);
        let i = ready.findIndex(m => m.prefix === mode);
        i = (i + step + ready.length) % ready.length;
        setMode(ready[i].prefix);
    }

    // fzf-ish: every query character in order, rewarded for runs and for
    // landing on word starts; -1 when it does not match at all.
    function fuzzy(needle, hay) {
        if (needle === "")
            return 0;
        const n = needle.toLowerCase();
        const h = hay.toLowerCase();
        let score = 0, at = 0, run = 0;
        for (let i = 0; i < n.length; ++i) {
            if (n[i] === " ")
                continue;
            const found = h.indexOf(n[i], at);
            if (found < 0)
                return -1;
            run = found === at ? run + 1 : 0;
            const wordStart = found === 0 || " ./-_".indexOf(h[found - 1]) >= 0;
            score += 1 + run * 2 + (wordStart ? 3 : 0) - Math.min(found - at, 3) * 0.2;
            at = found + 1;
        }
        return score - h.length * 0.01;
    }

    function commandRows() {
        const rows = [];
        for (const c of registry.list()) {
            if (!c.enabled)
                continue;
            const score = query === "" ? 0
                : Math.max(fuzzy(query, c.title), fuzzy(query, c.category + " " + c.title) - 1);
            if (score < 0)
                continue;
            const r = recent.indexOf(c.id);
            rows.push({ kind: "command", title: c.title, detail: c.category,
                        key: registry.keyFor(c), command: c, order: rows.length,
                        score: score + (r >= 0 ? 20 - r : 0) });
        }
        // Ties keep the registry's order, which groups by category.
        rows.sort((a, b) => b.score - a.score || a.order - b.order);
        return rows;
    }

    function pathRows() {
        const here = win.currentTab ? win.currentTab.path : Platform.homePath();
        const rows = [];
        const typed = query.trim();
        if (typed === "")
            return [{ kind: "hint", title: qsTr("Type a path — ~/, ../ and absolute paths work"),
                      detail: "", key: "" }];
        const target = Platform.resolvePath(typed, here);
        if (Platform.isNavigable(target) && Platform.exists(target))
            rows.push({ kind: "path", title: target, detail: qsTr("go"), key: "⏎", path: target });
        for (const p of Platform.completeFolder(typed, here, 30))
            if (p !== target)
                rows.push({ kind: "path", title: p, detail: qsTr("folder"), key: "", path: p });
        if (rows.length === 0)
            rows.push({ kind: "hint", title: qsTr("No such folder"), detail: "", key: "" });
        return rows;
    }

    function placeRows() {
        const rows = [];
        for (const p of win.placeList()) {
            const score = Math.max(fuzzy(query, p.name), fuzzy(query, p.location) - 2);
            if (score < 0)
                continue;
            rows.push({ kind: "path", title: p.name, detail: p.location, key: p.key,
                        path: p.location, score: score });
        }
        if (query !== "")
            rows.sort((a, b) => b.score - a.score);
        return rows;
    }

    function filterRows() {
        const tab = win.currentTab;
        if (!tab)
            return [];
        return [{ kind: "filter",
                  title: query === "" ? qsTr("Type to filter this folder — *.glob and re:regex work")
                                      : tab.statusText,
                  detail: tab.title, key: "⏎" }];
    }

    function searchRows() {
        const tab = win.currentTab;
        if (!tab)
            return [];
        if (query.trim() === "")
            return [{ kind: "hint", title: qsTr("Search names below this folder — re: for a regex"),
                      detail: "", key: "" }];
        const rows = [{ kind: "search", title: qsTr("Search for “%1”").arg(query.trim()),
                        detail: tab.title, key: "⏎", contents: false }];
        if (win.searchContentAvailable)
            rows.push({ kind: "search", title: qsTr("Search file contents for “%1”").arg(query.trim()),
                        detail: tab.title, key: "", contents: true });
        return rows;
    }

    // Size math: 4.7G / 3, 2*1.5M, (10 + 2) * 3. K/M/G/T are powers of 1024,
    // as everywhere else sizes are shown here.
    function calcRows() {
        const expr = query.trim();
        if (expr === "")
            return [{ kind: "hint", title: qsTr("Arithmetic, with K M G T for sizes — 4.7G / 3"),
                      detail: "", key: "" }];
        const value = evaluate(expr);
        if (value === null || !isFinite(value))
            return [{ kind: "hint", title: qsTr("Not an expression"), detail: "", key: "" }];
        const sized = /[kmgt]/i.test(expr);
        const plain = Number.isInteger(value) ? String(value) : value.toFixed(4).replace(/0+$/, "");
        const title = sized ? Platform.formatSize(Math.round(value)) + "  (" + Math.round(value) + " B)"
                            : plain;
        return [{ kind: "calc", title: "= " + title, detail: qsTr("copy"), key: "⏎",
                  value: sized ? String(Math.round(value)) : plain }];
    }

    function evaluate(expr) {
        const units = { k: 1024, m: 1048576, g: 1073741824, t: 1099511627776 };
        // Whitelist first: digits, operators, parentheses, unit letters.
        if (!/^[\d\s.+\-*/%()kmgtibKMGTIB]+$/.test(expr))
            return null;
        const js = expr.replace(/(\d+(?:\.\d+)?)\s*([kmgtKMGT])(?:i?[bB])?/g,
                                (m, n, u) => "(" + n + "*" + units[u.toLowerCase()] + ")");
        if (/[a-zA-Z]/.test(js))
            return null;
        try {
            return Function("return (" + js + ");")();
        } catch (e) {
            return null;
        }
    }

    function plannedRows() {
        const m = modes.find(x => x.prefix === mode);
        return [{ kind: "hint", title: qsTr("%1 mode is planned — see the design doc roadmap").arg(m.label),
                  detail: "", key: "" }];
    }

    function refresh() {
        let rows;
        switch (mode) {
        case "/": rows = pathRows(); break;
        case "~": rows = placeRows(); break;
        case "@": rows = filterRows(); break;
        case "?": rows = searchRows(); break;
        case "=": rows = calcRows(); break;
        case "": rows = commandRows(); break;
        default: rows = plannedRows(); break;
        }
        results = rows;
        list.currentIndex = rows.length > 0 && rows[0].kind !== "hint" ? 0 : -1;
    }

    function activate(index) {
        const row = index >= 0 && index < results.length ? results[index] : null;
        if (!row || row.kind === "hint")
            return;
        const tab = win.currentTab;
        filterTouched = false; // whatever @ did is now kept
        close();
        switch (row.kind) {
        case "command":
            recent = [row.command.id].concat(recent.filter(id => id !== row.command.id)).slice(0, 12);
            row.command.run();
            break;
        case "path":
            if (tab)
                tab.navigate(row.path);
            break;
        case "filter":
            if (tab)
                tab.commitFilter();
            break;
        case "search":
            win.openSearch();
            if (tab) {
                if (tab.searchContent !== row.contents)
                    win.toggleSearchContent();
                tab.searchQuery = query.trim();
            }
            break;
        case "calc":
            Clipboard.copyText(row.value);
            win.flash(qsTr("Copied %1").arg(row.value));
            break;
        }
    }

    // `/` mode: Tab puts the highlighted folder in the field, ready for the
    // next level down — shell completion.
    function complete() {
        const row = results[list.currentIndex];
        if (row && row.kind === "path") {
            field.text = row.path === "/" ? "/" : row.path + "/";
            field.cursorPosition = field.text.length;
        }
    }

    onQueryChanged: {
        if (mode === "@" && win.currentTab) {
            if (!filterTouched) {
                filterBefore = win.currentTab.filterText;
                filterTouched = true;
            }
            win.currentTab.filterText = query;
        }
        refresh();
    }
    onModeChanged: refresh()
    onAboutToShow: refresh()
    onOpened: {
        field.forceActiveFocus();
        field.cursorPosition = field.text.length;
    }
    onClosed: {
        if (filterTouched && win.currentTab)
            win.currentTab.filterText = filterBefore;
        filterTouched = false;
        win.returnFocusToView();
    }

    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.12)
    width: Math.min(640, parent.width - 32)
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    transformOrigin: Popup.Top

    Overlay.modal: Rectangle { color: Qt.alpha("black", 0.25) }

    background: OmCard {}

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Colors.fadeInMs; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: Colors.popInScale; to: 1; duration: Colors.popInMs; easing.type: Easing.OutCubic }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // The field, with the active mode's prefix as an accent badge.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            spacing: 8

            Rectangle {
                Layout.preferredHeight: 20
                Layout.preferredWidth: modeBadge.implicitWidth + 12
                radius: 3
                color: Colors.accent

                Text {
                    id: modeBadge
                    textFormat: Text.PlainText
                    anchors.centerIn: parent
                    text: palette.mode === "" ? ":" : palette.mode
                    color: Colors.accent.hslLightness > 0.6 ? "#111111" : "#ffffff"
                    font.family: Colors.mono
                    font.pixelSize: 12
                    font.bold: true
                }
            }

            TextField {
                id: field
                objectName: "paletteField"

                Layout.fillWidth: true
                color: Colors.text
                font.pixelSize: 14
                font.family: Colors.mono
                background: null
                leftPadding: 0
                placeholderText: qsTr("Run a command — or / path, ~ places, @ filter, ? search, = calc")
                placeholderTextColor: Colors.textDim
                selectByMouse: true

                Keys.onPressed: event => {
                    switch (event.key) {
                    case Qt.Key_Down:
                        list.incrementCurrentIndex();
                        event.accepted = true;
                        break;
                    case Qt.Key_Up:
                        list.decrementCurrentIndex();
                        event.accepted = true;
                        break;
                    case Qt.Key_PageDown:
                        list.currentIndex = Math.min(list.count - 1, list.currentIndex + 8);
                        event.accepted = true;
                        break;
                    case Qt.Key_PageUp:
                        list.currentIndex = Math.max(0, list.currentIndex - 8);
                        event.accepted = true;
                        break;
                    case Qt.Key_Return:
                    case Qt.Key_Enter:
                        palette.activate(list.currentIndex);
                        event.accepted = true;
                        break;
                    case Qt.Key_Tab:
                        if (palette.mode === "/" && palette.query !== "")
                            palette.complete();
                        else
                            palette.cycleMode(1);
                        event.accepted = true;
                        break;
                    case Qt.Key_Backtab:
                        palette.cycleMode(-1);
                        event.accepted = true;
                        break;
                    case Qt.Key_N:
                    case Qt.Key_J:
                        if (event.modifiers & Qt.ControlModifier) {
                            list.incrementCurrentIndex();
                            event.accepted = true;
                        }
                        break;
                    case Qt.Key_P:
                    case Qt.Key_K:
                        if (event.modifiers & Qt.ControlModifier) {
                            list.decrementCurrentIndex();
                            event.accepted = true;
                        }
                        break;
                    }
                }
            }
        }

        // Mode chips: the prefixes, the live one lit in the accent.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 8
            spacing: 4

            Repeater {
                model: palette.modes

                Rectangle {
                    required property var modelData
                    readonly property bool live: palette.mode === modelData.prefix

                    width: chipText.implicitWidth + 12
                    height: 18
                    radius: 3
                    color: live ? Colors.accent : chipMouse.containsMouse ? Colors.hover : "transparent"
                    border.color: live ? Colors.accent : Colors.border
                    border.width: 1
                    opacity: modelData.ready ? 1 : 0.45

                    Text {
                        id: chipText
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        text: (modelData.prefix === "" ? ":" : modelData.prefix) + " " + modelData.label
                        color: parent.live ? (Colors.accent.hslLightness > 0.6 ? "#111111" : "#ffffff")
                                           : Colors.textDim
                        font.family: Colors.mono
                        font.pixelSize: 10
                    }

                    MouseArea {
                        id: chipMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: palette.setMode(modelData.prefix)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Colors.border
        }

        ListView {
            id: list
            objectName: "paletteList"

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Colors.rowHeight * 14)
            Layout.topMargin: 4
            Layout.bottomMargin: 4
            clip: true
            model: palette.results
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index
                readonly property bool lit: ListView.isCurrentItem

                width: ListView.view.width
                height: Colors.rowHeight + 4
                color: lit ? Colors.selection : rowMouse.containsMouse ? Colors.hover : "transparent"

                // The focus bar: the current row, in the accent.
                Rectangle {
                    visible: row.lit
                    width: 2
                    height: parent.height
                    color: Colors.accent
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    spacing: 10

                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: row.modelData.title
                        color: row.modelData.kind === "hint" ? Colors.textDim
                             : row.lit ? Colors.selectionText : Colors.text
                        font.pixelSize: 13
                        font.family: row.modelData.kind === "path" || row.modelData.kind === "calc"
                                     ? Colors.mono : Qt.application.font.family
                        elide: row.modelData.kind === "path" ? Text.ElideMiddle : Text.ElideRight
                    }

                    Text {
                        textFormat: Text.PlainText
                        visible: text !== ""
                        Layout.maximumWidth: 220
                        text: row.modelData.detail
                        color: Colors.textDim
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }

                    // The keybinding hint, on every row that has one.
                    Rectangle {
                        visible: row.modelData.key !== ""
                        Layout.preferredHeight: 18
                        Layout.preferredWidth: keyText.implicitWidth + 10
                        radius: 3
                        color: "transparent"
                        border.color: Colors.border
                        border.width: 1

                        Text {
                            id: keyText
                            textFormat: Text.PlainText
                            anchors.centerIn: parent
                            text: row.modelData.key
                            color: Colors.textDim
                            font.family: Colors.mono
                            font.pixelSize: 10
                        }
                    }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: palette.activate(row.index)
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Colors.border
        }

        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            leftPadding: 12
            verticalAlignment: Text.AlignVCenter
            text: palette.mode === "/"
                  ? qsTr("↑↓ move   ⏎ go   tab complete   esc close")
                  : qsTr("↑↓ move   ⏎ run   tab next mode   esc close")
            color: Colors.textDim
            font.family: Colors.mono
            font.pixelSize: 10
        }
    }
}
