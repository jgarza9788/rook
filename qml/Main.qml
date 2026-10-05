import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Rook.Runtime
import "Keymap.js" as Keymap

// A window. Several tabs, one visible at a time, plus the chrome that acts on
// whichever is current. Windows are independent — closing one never disturbs
// another, and the process exits with the last.
Window {
    id: root

    property string initialPath: Platform.homePath()
    property string initialSelection: ""
    // A saved session for this window ({tabs: [{path, split, second,
    // active}], current}) — set by App when restoring at launch.
    property var initialSession: null

    // A tab slot holds one pane, or two while split view (F3) is on. The
    // chrome acts on the slot's active pane, so `currentTab` stays the one
    // name everything below routes through.
    readonly property Item currentSlot: tabsRepeater.count > 0
                                        ? tabsRepeater.itemAt(stack.currentIndex) : null
    readonly property Item currentTab: currentSlot ? currentSlot.activePane : null
    readonly property bool splitOpen: currentSlot ? currentSlot.split : false
    readonly property int activePane: currentSlot ? currentSlot.activePaneIndex : 0

    // Read from C++ so a second launch can raise this window rather than
    // opening a duplicate of somewhere already on screen. The rest is the
    // window's observable state, which is what lets the UI be tested by
    // driving real keystrokes instead of trusting a screenshot.
    readonly property string currentPath: currentTab ? currentTab.path : ""
    readonly property int selectionCount: currentTab ? currentTab.selectionCount : 0
    readonly property string currentName: currentTab && currentTab.currentIndex >= 0
                                          ? currentTab.files.valueAt(currentTab.currentIndex, "name") : ""
    readonly property string viewMode: currentTab ? currentTab.viewMode : ""
    readonly property bool showHidden: currentTab ? currentTab.showHidden : false
    readonly property int sortKey: currentTab ? currentTab.sortKey : 0
    readonly property bool sortDescending: currentTab ? currentTab.sortDescending : false
    readonly property int zoom: currentTab ? currentTab.zoom : 0
    readonly property int visibleCount: currentTab ? currentTab.files.count : 0
    // The proxy's live value, not the Settings one: reading it end-to-end
    // proves the file → Settings → binding → proxy chain, which is what the
    // UI verification asserts on.
    readonly property bool foldersFirst: currentTab ? currentTab.files.foldersFirst : false
    // Same idea for list columns: the view's rendered set, joined for D-Bus.
    readonly property string listColumns: currentTab && currentTab.listColumns
                                          ? currentTab.listColumns.join(",") : ""
    readonly property string iconCaptions: currentTab && currentTab.iconCaptions
                                           ? currentTab.iconCaptions.join(",") : ""
    // The rendered Size cell of the current row — "12 items" for folders.
    readonly property string currentSizeCell: currentTab
                                              ? currentTab.currentSizeCell : ""
    // The current row's tree state — how verify-ui asserts expandable folders.
    readonly property int currentDepth: currentTab ? currentTab.currentDepth : 0
    readonly property bool currentExpanded: currentTab ? currentTab.currentExpanded : false
    // Live + queued operations, for the popover button and verify-ui.
    readonly property int operationsCount: FileOperations.operations.length
    readonly property int tabCount: tabModel.count

    // The sidebar (GitHub #5). Wide, it sits beside the files and F9 flips
    // the saved Settings.showSidebar. Narrow — half a laptop screen when
    // tiled — it hides itself, like Nautilus, and F9 or the header button
    // slides it over the files instead; picking a place slides it away.
    readonly property bool sidebarNarrow: width < 720
    property bool sidebarOverlayOpen: false
    readonly property bool sidebarInline: !sidebarNarrow && Settings.showSidebar
    readonly property bool sidebarVisible: sidebarNarrow ? sidebarOverlayOpen : Settings.showSidebar
    onSidebarNarrowChanged: sidebarOverlayOpen = false

    function toggleSidebar() {
        if (sidebarNarrow)
            sidebarOverlayOpen = !sidebarOverlayOpen;
        else
            Settings.showSidebar = !Settings.showSidebar;
    }
    readonly property int placesCount: sidebar.placesCount

    // Search: Ctrl+F opens the bar, Escape closes it. Published for the UI
    // verification script like everything else it drives.
    property bool searchOpen: false
    readonly property string searchQuery: currentTab ? currentTab.searchQuery : ""
    readonly property bool searchContent: currentTab ? currentTab.searchContent : false
    // The filter popover's state, for WindowState.
    readonly property string searchDateRange: currentTab ? currentTab.searchDateRange : "any"
    readonly property string searchDateKind: currentTab ? currentTab.searchDateKind : "modified"
    readonly property string searchTypeFilter: currentTab ? currentTab.searchTypeFilter : "any"
    // The index only answers for local folders — elsewhere the toggle hides.
    readonly property bool searchContentAvailable: currentTab
        ? Platform.isLocal(currentTab.path) : false

    function openSearch() {
        searchOpen = true;
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    function toggleSearchContent() {
        if (!currentTab || !searchContentAvailable)
            return;
        if (!searchOpen)
            openSearch();
        currentTab.searchContent = !currentTab.searchContent;
    }

    readonly property string searchMatchMode: currentTab ? currentTab.searchMatchMode : "auto"

    function toggleSearchRegex() {
        if (!currentTab || currentTab.searchContent)
            return;
        currentTab.searchMatchMode = currentTab.searchMatchMode === "regex" ? "auto" : "regex";
    }

    function closeSearch() {
        searchOpen = false;
        if (currentTab)
            currentTab.searchQuery = "";
        returnFocusToView();
    }

    // The properties dialog's own state, published for the same reason: it is
    // the only way to assert on a dialog without a human looking at it.
    readonly property bool preferencesOpen: preferencesDialog.opened
    readonly property bool propertiesOpen: propertiesDialog.opened
    readonly property string propertiesName: propertiesDialog.subjectName
    readonly property int propertiesCount: propertiesDialog.subjectCount
    // A string, not a number: a byte count travels over D-Bus as a double and
    // comes back out as "4321.0", which is a needlessly awkward thing to assert
    // on — and a lossy one once a file is bigger than 2^53.
    readonly property string propertiesSize: String(propertiesDialog.subjectSize)
    readonly property string propertiesMode: propertiesDialog.subjectMode
    readonly property int propertiesTab: propertiesDialog.currentTab

    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 400
    visible: true
    // Transparent, not Colors.window: each region (toolbar, tab strip,
    // sidebar, content pane, server bar) paints its surface exactly once —
    // a translucent base under translucent chrome would stack the
    // backgroundOpacity alpha twice in the chrome areas.
    color: "transparent"
    title: currentTab ? currentTab.title + " — Files" : "Files"

    // Quick Controls (menus, dialogs, fields, scrollbars) paint from the
    // palette. Without this they wear the style's stock grey and look like
    // a foreign toolkit dropped into an Omarchy window.
    //
    // The text roles are set per group, never bare: a bare role writes all
    // three groups, so every theme change (Colors re-binding) overwrote
    // disabled.* and disabled menu entries drew at full strength.
    palette {
        window: Colors.chrome
        base: Colors.window
        alternateBase: Colors.chrome
        button: Colors.chrome
        active.text: Colors.text
        active.windowText: Colors.text
        active.buttonText: Colors.text
        inactive.text: Colors.text
        inactive.windowText: Colors.text
        inactive.buttonText: Colors.text
        highlight: Colors.selection
        highlightedText: Colors.selectionText
        mid: Colors.border
        midlight: Colors.hover
        dark: Colors.border
        light: Colors.hover
        placeholderText: Colors.textDim
        toolTipBase: Colors.chrome
        toolTipText: Colors.text
        disabled {
            text: Colors.textDim
            buttonText: Colors.textDim
            windowText: Colors.textDim
        }
    }

    onClosing: App.windowClosed(root)

    // ---- tabs -------------------------------------------------------------

    // tabPath and tabSelection are where a tab opened; tabTitle follows it as
    // it navigates, for the strip.
    ListModel { id: tabModel }

    function addTab(path, selection) {
        tabModel.append({ tabPath: path, tabSelection: selection || "", tabTitle: "" });
        stack.currentIndex = tabModel.count - 1;
        focusCurrentTab();
    }

    // The keys follow the tab on screen — after the tab bindings settle.
    function focusCurrentTab() {
        Qt.callLater(() => {
            const slot = tabsRepeater.count > 0 ? tabsRepeater.itemAt(stack.currentIndex) : null;
            if (slot && slot.activePane)
                slot.activePane.focusView();
        });
    }

    function closeTab(index) {
        if (tabModel.count <= 1) {
            root.close();
            return;
        }
        tabModel.remove(index);
        stack.currentIndex = Math.min(index, tabModel.count - 1);
        // Removing the current tab may leave the index unchanged (the next
        // tab slid into its place), so no change signal: focus explicitly.
        focusCurrentTab();
    }

    function cycleTab(delta) {
        if (tabModel.count < 2)
            return;
        stack.currentIndex = (stack.currentIndex + delta + tabModel.count) % tabModel.count;
    }

    // What App saves when this is the last window to close.
    function sessionState() {
        const tabs = [];
        for (let i = 0; i < tabsRepeater.count; ++i) {
            const slot = tabsRepeater.itemAt(i);
            if (slot)
                tabs.push(slot.sessionState());
        }
        return { tabs: tabs, current: stack.currentIndex };
    }

    function restoreSession(session) {
        for (const saved of session.tabs) {
            addTab(saved.path);
            if (saved.split && saved.second)
                tabsRepeater.itemAt(tabModel.count - 1).restoreSplit(saved.second, saved.active);
        }
        stack.currentIndex = Math.max(0, Math.min(tabModel.count - 1, session.current || 0));
    }

    Component.onCompleted: {
        if (root.initialSession && root.initialSession.tabs && root.initialSession.tabs.length > 0)
            restoreSession(root.initialSession);
        else
            addTab(root.initialPath, root.initialSelection);
        if (currentTab)
            currentTab.forceActiveFocus();
        // First launch on Omarchy puts the switch in the Toggle menu, once.
        DefaultFileManager.offerToggleMenu(Settings);
    }

    // ---- chrome -----------------------------------------------------------

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // The top line: no header bar, just where you are (breadcrumbs, or
        // the search field in their place), the tabs, and the palette — the
        // way into every command that used to be a toolbar button.
        Rectangle {
            id: topLine
            Layout.fillWidth: true
            Layout.preferredHeight: Colors.barHeight
            color: Colors.chrome

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Colors.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.bottomMargin: 1
                spacing: 2

                // Only while the sidebar is out of the layout, hidden or narrow.
                ToolbarButton {
                    visible: !root.sidebarInline
                    glyph: "view-sidebar"
                    tip: qsTr("Show Sidebar (F9)")
                    active: root.sidebarOverlayOpen
                    onTriggered: root.toggleSidebar()
                }

                PathBar {
                    id: pathBar
                    objectName: "pathBar"

                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    visible: !root.searchOpen
                    path: root.currentTab ? root.currentTab.path : ""
                    onMenuRequested: pathBarMenu.popup()
                    onNavigateRequested: target => {
                        if (root.currentTab)
                            root.currentTab.navigate(target);
                        // Same rule as the dialogs: whoever took the keyboard
                        // gives it back. Without this, the first arrow key
                        // after a committed Ctrl+L lands in the path bar.
                        root.returnFocusToView();
                    }
                }

                // Search lives in the path bar's slot, as in Nautilus: the
                // field replaces the breadcrumbs in place while search is
                // open, and the query still lives on the tab so switching
                // tabs shows that tab's search.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    visible: root.searchOpen
                    radius: Colors.radius
                    color: Colors.window
                    border.color: root.currentTab && root.currentTab.searchPatternInvalid ? Colors.error
                                : searchField.activeFocus ? Colors.accent : Colors.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 6

                        Text {
                            textFormat: Text.PlainText
                            text: "⌕"
                            color: Colors.textDim
                            font.pixelSize: 15
                        }

                        TextField {
                            id: searchField

                            Layout.fillWidth: true
                            color: Colors.text
                            font.pixelSize: 13
                            background: null
                            placeholderText: qsTr("Search current folder")
                            selectByMouse: true
                            onTextEdited: {
                                if (root.currentTab)
                                    root.currentTab.searchQuery = text;
                            }
                            Keys.onEscapePressed: root.closeSearch()
                            Keys.onPressed: event => {
                                if (event.key === Qt.Key_R && (event.modifiers & Qt.AltModifier)) {
                                    root.toggleSearchRegex();
                                    event.accepted = true;
                                }
                            }
                            Keys.onDownPressed: {
                                // Hand the keyboard to the results without closing.
                                root.returnFocusToView();
                                if (root.currentTab && root.currentTab.files.count > 0)
                                    root.currentTab.setCurrent(0, false);
                            }
                        }

                        Text {
                            textFormat: Text.PlainText
                            visible: root.currentTab && root.currentTab.searching
                            text: qsTr("searching…")
                            color: Colors.accent
                            font.pixelSize: 11
                        }

                        // .* — the query as a regular expression. Name
                        // search only: the full-text index matches words.
                        Rectangle {
                            id: regexChip
                            readonly property bool on: root.searchMatchMode === "regex"
                            enabled: !root.searchContent
                            opacity: enabled ? 1 : 0.4
                            implicitWidth: regexChipLabel.implicitWidth + 14
                            implicitHeight: 22
                            radius: 4
                            color: on && enabled ? Colors.selection
                                 : regexChipMouse.containsMouse ? Colors.hover : "transparent"
                            border.color: on && enabled ? Colors.selection : Colors.border
                            border.width: 1

                            Text {
                                id: regexChipLabel
                                textFormat: Text.PlainText
                                anchors.centerIn: parent
                                text: ".*"
                                color: regexChip.on && regexChip.enabled ? Colors.selectionText : Colors.textDim
                                font.pixelSize: 12
                                font.family: "monospace"
                            }

                            MouseArea {
                                id: regexChipMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleSearchRegex()
                            }

                            ToolTip.visible: regexChipMouse.containsMouse
                            ToolTip.text: regexChip.enabled
                                ? qsTr("Regular expression (Alt+R) — *.glob and re: also work")
                                : qsTr("Full-text search matches words, not patterns")
                            ToolTip.delay: 600
                        }

                        // File-name vs full-text, Nautilus's search filter
                        // at the field's right edge. Hidden where the index
                        // can't answer (non-local).
                        Rectangle {
                            visible: root.searchContentAvailable
                            implicitWidth: contentsLabel.implicitWidth + 16
                            implicitHeight: 22
                            radius: 4
                            color: root.searchContent ? Colors.selection
                                 : contentsMouse.containsMouse ? Colors.hover : "transparent"
                            border.color: root.searchContent ? Colors.selection : Colors.border
                            border.width: 1

                            Text {
                                textFormat: Text.PlainText
                                id: contentsLabel
                                anchors.centerIn: parent
                                text: qsTr("Contents")
                                color: root.searchContent ? Colors.selectionText : Colors.textDim
                                font.pixelSize: 11
                            }

                            MouseArea {
                                id: contentsMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleSearchContent()
                            }

                            ToolTip.visible: contentsMouse.containsMouse
                            ToolTip.text: qsTr("Search file contents (Ctrl+Shift+F)")
                            ToolTip.delay: 600
                        }

                        // Nautilus's search filters: a date window and a file
                        // type, in a popover off the field's edge. The chip
                        // stays lit while any filter narrows the results.
                        Rectangle {
                            readonly property bool filtersActive: root.currentTab
                                && (root.currentTab.searchDateRange !== "any"
                                    || root.currentTab.searchTypeFilter !== "any")

                            implicitWidth: filtersLabel.implicitWidth + 16
                            implicitHeight: 22
                            radius: 4
                            color: filtersActive ? Colors.selection
                                 : filtersMouse.containsMouse ? Colors.hover : "transparent"
                            border.color: filtersActive ? Colors.selection : Colors.border
                            border.width: 1

                            Text {
                                textFormat: Text.PlainText
                                id: filtersLabel
                                anchors.centerIn: parent
                                text: qsTr("Filters")
                                color: parent.filtersActive ? Colors.selectionText : Colors.textDim
                                font.pixelSize: 11
                            }

                            MouseArea {
                                id: filtersMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: filterPopover.visible ? filterPopover.close()
                                                                 : filterPopover.open()
                            }

                            ToolTip.visible: filtersMouse.containsMouse && !filterPopover.visible
                            ToolTip.text: qsTr("Filter by date and file type")
                            ToolTip.delay: 600

                            OmPopup {
                                id: filterPopover
                                objectName: "filterPopover"

                                y: parent.height + 10
                                x: parent.width - width
                                width: 330
                                padding: 14

                                // Sampled, not bound: interacting with a
                                // ComboBox writes currentIndex, which would
                                // sever a binding on first use (sixth
                                // appearance of this pattern).
                                onAboutToShow: {
                                    const tab = root.currentTab;
                                    rangeCombo.currentIndex =
                                        Math.max(0, rangeValues.indexOf(tab.searchDateRange));
                                    kindCombo.currentIndex =
                                        Math.max(0, kindValues.indexOf(tab.searchDateKind));
                                    typeCombo.currentIndex =
                                        Math.max(0, typeValues.indexOf(tab.searchTypeFilter));
                                }

                                readonly property var rangeValues:
                                    ["any", "today", "yesterday", "week", "month", "year"]
                                readonly property var rangeLabels:
                                    [qsTr("Any time"), qsTr("Today"), qsTr("Since yesterday"),
                                     qsTr("Last 7 days"), qsTr("Last 30 days"), qsTr("Last year")]
                                readonly property var kindValues:
                                    ["modified", "created", "accessed"]
                                readonly property var kindLabels:
                                    [qsTr("Last Modified"), qsTr("Created"), qsTr("Last Used")]
                                readonly property var typeValues:
                                    ["any", "folders", "documents", "illustration", "music",
                                     "pdf", "pictures", "presentations", "spreadsheets",
                                     "text", "videos"]
                                readonly property var typeLabels:
                                    [qsTr("Anything"), qsTr("Folders"), qsTr("Documents"),
                                     qsTr("Illustration"), qsTr("Music"), qsTr("PDF / PostScript"),
                                     qsTr("Pictures"), qsTr("Presentations"), qsTr("Spreadsheets"),
                                     qsTr("Text Files"), qsTr("Videos")]

                                contentItem: Column {
                                    spacing: 10

                                    Text {
                                        textFormat: Text.PlainText
                                        text: qsTr("When")
                                        color: Colors.text
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    Row {
                                        spacing: 8

                                        OmComboBox {
                                            id: rangeCombo
                                            objectName: "rangeCombo"
                                            width: 150
                                            model: filterPopover.rangeLabels
                                            onActivated: root.currentTab.searchDateRange =
                                                filterPopover.rangeValues[currentIndex]
                                        }

                                        OmComboBox {
                                            id: kindCombo
                                            width: 132
                                            enabled: rangeCombo.currentIndex > 0
                                            model: filterPopover.kindLabels
                                            onActivated: root.currentTab.searchDateKind =
                                                filterPopover.kindValues[currentIndex]
                                        }
                                    }

                                    Text {
                                        textFormat: Text.PlainText
                                        text: qsTr("What")
                                        color: Colors.text
                                        font.pixelSize: 12
                                        font.bold: true
                                    }

                                    OmComboBox {
                                        id: typeCombo
                                        width: 290
                                        model: filterPopover.typeLabels
                                        onActivated: root.currentTab.searchTypeFilter =
                                            filterPopover.typeValues[currentIndex]
                                    }
                                }
                            }
                        }
                    }
                }

                // Tabs, inline and only when there is more than one.
                Row {
                    id: tabLine
                    Layout.fillHeight: true
                    Layout.maximumWidth: topLine.width * 0.45
                    visible: tabModel.count > 1
                    spacing: 2
                    clip: true

                    Repeater {
                        model: tabModel

                    delegate: Rectangle {
                        required property int index
                        required property string tabPath
                        required property string tabTitle

                        readonly property bool isCurrent: index === stack.currentIndex
                        width: Math.min(160, Math.max(70, tabLine.width * 0.4 / tabModel.count))
                        height: 20
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 3
                        color: isCurrent ? Colors.window
                             : tabMouse.containsMouse ? Colors.hover : "transparent"
                        border.color: Colors.border
                        border.width: isCurrent ? 1 : 0

                        // The tab's number, for Ctrl+Tab counting and the eye.
                        Text {
                            id: tabNumber
                            objectName: "tabNumber"
                            textFormat: Text.PlainText
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: index + 1
                            color: Colors.textDim
                            font.family: Colors.mono
                            font.pixelSize: 10
                        }

                        Text {
                            textFormat: Text.PlainText
                            anchors.left: tabNumber.right
                            anchors.leftMargin: 5
                            anchors.right: closeButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: tabTitle || Platform.baseName(tabPath) || "/"
                            color: index === stack.currentIndex ? Colors.text : Colors.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }

                        Text {
                            textFormat: Text.PlainText
                            id: closeButton
                            // Above tabMouse, which fills the tab and is
                            // declared later — otherwise it takes the click
                            // and the x only switches to its tab.
                            z: 1

                            anchors.right: parent.right
                            anchors.rightMargin: 5
                            anchors.verticalCenter: parent.verticalCenter
                            text: "×"
                            color: closeMouse.containsMouse ? Colors.text : Colors.textDim
                            font.pixelSize: 12

                            MouseArea {
                                id: closeMouse
                                anchors.fill: parent
                                anchors.margins: -4
                                hoverEnabled: true
                                onClicked: root.closeTab(index)
                            }
                        }

                        // A drag resting on a tab switches to it (spring-loaded);
                        // a drop lands in that tab's folder.
                        FileDropArea {
                            anchors.fill: parent
                            readonly property Item pane: tabsRepeater.itemAt(index)
                                                         ? tabsRepeater.itemAt(index).activePane : null
                            destination: pane ? pane.path : tabPath
                            springLoaded: index !== stack.currentIndex
                            onSprung: stack.currentIndex = index
                            onFilesDropped: urls => { if (pane) pane.requestDrop(urls, pane.path); }
                        }

                        MouseArea {
                            id: tabMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                            onClicked: mouse => {
                                if (mouse.button === Qt.MiddleButton)
                                    root.closeTab(index);
                                else
                                    stack.currentIndex = index;
                            }
                        }
                    }
                    }
                }

                // Issue #28: New Folder one click away, not only in the
                // palette and the right-click menu.
                ToolbarButton {
                    id: newFolderButton
                    objectName: "newFolderButton"
                    symbol: "+"
                    tip: qsTr("New Folder (Ctrl+Shift+N)")
                    enabled: root.currentTab !== null && root.viewWritable
                    onTriggered: root.newFolder()
                }

                // The palette trigger: the one way in for the mouse, and a
                // reminder of the key.
                Rectangle {
                    id: paletteTrigger
                    objectName: "paletteTrigger"
                    Layout.preferredHeight: 20
                    Layout.preferredWidth: triggerLabel.implicitWidth + 16
                    radius: 3
                    color: root.paletteOpen ? Colors.accent
                         : triggerMouse.containsMouse ? Colors.hover : "transparent"
                    border.color: root.paletteOpen ? Colors.accent : Colors.border
                    border.width: 1

                    Text {
                        id: triggerLabel
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        text: (Settings.keyboardMode === "vim" ? ":" : "⌘") + "  Ctrl+Shift+P"
                        color: root.paletteOpen ? (Colors.accent.hslLightness > 0.6 ? "#111111" : "#ffffff")
                                                : Colors.textDim
                        font.family: Colors.mono
                        font.pixelSize: 10
                    }

                    MouseArea {
                        id: triggerMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openPalette("")
                    }

                    ToolTip.visible: triggerMouse.containsMouse
                    ToolTip.text: qsTr("Command palette — every action, / paths, ~ places, @ filter, ? search")
                    ToolTip.delay: 600
                }

                ToolbarButton {
                    id: menuButton
                    symbol: "≡"
                    tip: qsTr("Main menu")
                    onTriggered: mainMenu.popup(menuButton, 0, menuButton.height)
                }
            }
        }

        // (The search field lives inline in the toolbar, in the path bar's
        // slot — see above. No separate search row, as in Nautilus.)

        // Sidebar and files. Not a RowLayout: in a narrow window the sidebar
        // slides over the files rather than squeezing them.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            // Dims the files under the slid-over sidebar; a press on it
            // dismisses the sidebar, like clicking outside a dialog.
            Rectangle {
                anchors.fill: parent
                z: 1
                color: "black"
                opacity: root.sidebarOverlayOpen ? 0.3 : 0
                visible: opacity > 0
                Behavior on opacity { NumberAnimation { duration: 150 } }

                MouseArea {
                    anchors.fill: parent
                    enabled: root.sidebarOverlayOpen
                    onPressed: root.sidebarOverlayOpen = false
                }
            }

            Sidebar {
                id: sidebar

                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: 200
                x: root.sidebarVisible ? 0 : -width
                z: 2
                visible: root.sidebarVisible || x > -width
                // Slides only as an overlay; the wide F9 toggle stays instant.
                Behavior on x {
                    enabled: root.sidebarNarrow
                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                }
                currentLocation: root.currentPath
                mounter: windowMounter
                onNavigateRequested: location => {
                    if (root.currentTab)
                        root.currentTab.navigate(location);
                    root.sidebarOverlayOpen = false;
                }
                onMountError: (name, message) => root.flash(qsTr("Could not mount “%1”: %2").arg(name).arg(message))
                onOpsPopoverClosed: root.returnFocusToView()
                onDropRequested: (urls, location) => {
                    if (location === "trash:///")
                        FileOperations.trash(Platform.locationsFromUrls(urls), root);
                    else if (location === "starred:///")
                        StarredStore.star(Platform.locationsFromUrls(urls));
                    else if (root.currentTab)
                        root.currentTab.requestDrop(urls, location);
                }
                onOpenInNewTabRequested: location => {
                    root.addTab(location);
                    root.sidebarOverlayOpen = false;
                }
                onEmptyTrashRequested: emptyTrashConfirm.open()
                onKeyboardReleased: {
                    root.sidebarOverlayOpen = false;
                    root.returnFocusToView();
                }
            }

            InfoPanel {
                id: infoPanel
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: Math.min(300, parent.width * 0.32)
                visible: Settings.showInfoPanel
                tab: visible ? root.currentTab : null
            }

            Item {
                anchors.fill: parent
                anchors.leftMargin: root.sidebarInline ? sidebar.width : 0
                anchors.rightMargin: infoPanel.visible ? infoPanel.width : 0

                // The file views' backdrop — the window tone that used to be
                // the root window colour before the root went transparent.
                Rectangle {
                    anchors.fill: parent
                    color: Colors.window
                }

                StackLayout {
                    id: stack

                    anchors.fill: parent
                    currentIndex: 0

                    // Deferred: in this handler `currentTab` can still name the
                    // tab being hidden (its binding may not have re-run yet),
                    // and focusing that leaves the keys nowhere visible.
                    onCurrentIndexChanged: root.focusCurrentTab()

                    Repeater {
                        id: tabsRepeater
                        model: tabModel

                        delegate: TabPanes {
                            required property int index
                            required property string tabPath
                            required property string tabSelection

                            // The strip names the tab after its active pane,
                            // as the window title does, not where it opened.
                            readonly property string paneTitle: activePane ? activePane.title : ""
                            onPaneTitleChanged: tabModel.setProperty(index, "tabTitle", paneTitle)
                            Component.onCompleted: tabModel.setProperty(index, "tabTitle", paneTitle)

                            initialPath: tabPath
                            initialSelection: tabSelection
                            onContextMenuRequested: contextMenu.popup()
                            onMountNeeded: location => windowMounter.mountLocation(location)
                            onTransferRequested: (sources, destination, isMove) =>
                                root.startTransfer(sources, destination, isMove, false)
                            onPreviewUnavailable: root.flash(qsTr("Space previews need Sushi — install the sushi package"))
                            onCommandRequested: (command, arg) => root.runCommand(command, arg)
                        }
                    }
                }

                // Over the files only — the sidebar and chrome stay usable.
                QuickView {
                    id: quickView
                    anchors.fill: parent
                    z: 10
                }
            }
        }

        // The server bar — Nautilus's "connect to server" surface, shown only
        // in the Network view. Connecting mounts first (credentials and all),
        // records the address as a known connection, then navigates into it.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 44 : 0
            visible: root.viewingNetwork
            color: Colors.chrome

            Rectangle {
                width: parent.width
                height: 1
                color: Colors.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                TextField {
                    id: serverField

                    Layout.fillWidth: true
                    Layout.maximumWidth: 420
                    Layout.preferredHeight: 30
                    color: Colors.text
                    font.pixelSize: 13
                    placeholderText: qsTr("Server address")
                    selectByMouse: true
                    onAccepted: root.connectToServer()
                }

                OmButton {
                    text: qsTr("Connect")
                    enabled: serverField.text.trim() !== ""
                    onClicked: root.connectToServer()
                }

                ToolbarButton {
                    id: protocolsButton

                    symbol: "ⓘ"
                    tip: "Available protocols"
                    onTriggered: protocolsPopover.opened ? protocolsPopover.close()
                                                        : protocolsPopover.open()

                    // Nautilus's Server Addresses help, honest by
                    // construction: only protocols gvfs here can actually
                    // mount are listed.
                    OmPopup {
                        id: protocolsPopover
                        // Opens upward from the button.
                        transformOrigin: Popup.Bottom

                        x: parent.width - width
                        y: -height - 6
                        padding: 16

                        contentItem: Column {
                            spacing: 10

                            Text {
                                textFormat: Text.PlainText
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Server Addresses")
                                color: Colors.text
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Text {
                                textFormat: Text.PlainText
                                width: 320
                                text: qsTr("Server addresses are made up of a protocol prefix and an address. Examples:")
                                color: Colors.textDim
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                textFormat: Text.PlainText
                                width: 320
                                text: "smb://gnome.org, ssh://192.168.0.1, ftp://[2001:db8::1]"
                                color: Colors.text
                                font.pixelSize: 12
                                wrapMode: Text.WrapAnywhere
                            }

                            GridLayout {
                                columns: 2
                                columnSpacing: 24
                                rowSpacing: 4

                                Text {
                                    textFormat: Text.PlainText
                                    text: qsTr("Available Protocols")
                                    color: Colors.text
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    text: qsTr("Prefix")
                                    color: Colors.text
                                    font.pixelSize: 12
                                    font.bold: true
                                }

                                Repeater {
                                    model: root.protocolCells
                                    delegate: Text {
                                        required property var modelData
                                        textFormat: Text.PlainText
                                        text: modelData.text
                                        color: modelData.dim ? Colors.textDim : Colors.text
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        }

        // The status line, Vim-style: the mode on the left in the accent,
        // what the view says beside it, and the numbers on the right in
        // monospace so they hold still as they change.
        Rectangle {
            id: statusLine
            Layout.fillWidth: true
            Layout.preferredHeight: Colors.barHeight - 4
            color: Colors.chrome

            Rectangle {
                width: parent.width
                height: 1
                color: Colors.border
            }

            readonly property color onAccent: Colors.accent.hslLightness > 0.6 ? "#111111" : "#ffffff"

            RowLayout {
                anchors.fill: parent
                anchors.topMargin: 1
                anchors.rightMargin: 10
                spacing: 10

                Rectangle {
                    id: modeBadge
                    objectName: "modeBadge"
                    Layout.fillHeight: true
                    Layout.preferredWidth: modeText.implicitWidth + 16
                    color: Colors.accent

                    Text {
                        id: modeText
                        textFormat: Text.PlainText
                        anchors.centerIn: parent
                        text: root.mode
                        color: statusLine.onAccent
                        font.family: Colors.mono
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                Text {
                    id: statusLabel
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    Layout.minimumWidth: 40
                    // A long selected name gives up its middle, keeping the start
                    // and the extension (and the "(copy)" before it).
                    elide: Text.ElideMiddle
                    // While something is running, the status line belongs to it.
                    text: FileOperations.busy ? FileOperations.statusText
                        : FileOperations.lastError !== "" ? FileOperations.lastError
                        : root.flashText !== "" ? root.flashText
                        : root.currentTab ? root.currentTab.statusText : ""
                    color: !FileOperations.busy
                           && (FileOperations.lastError !== "" || root.flashText !== "")
                           ? Colors.error : Colors.textDim
                    font.pixelSize: 11
                }

                Row {
                    spacing: 8
                    visible: FileOperations.busy

                    ProgressBar {
                        width: 120
                        anchors.verticalCenter: parent.verticalCenter
                        from: 0
                        to: 1
                        value: FileOperations.progress
                        indeterminate: FileOperations.progress <= 0
                    }

                    Text {
                        textFormat: Text.PlainText
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("cancel")
                        color: cancelMouse.containsMouse ? Colors.text : Colors.textDim
                        font.family: Colors.mono
                        font.pixelSize: 10
                        font.underline: true

                        MouseArea {
                            id: cancelMouse
                            anchors.fill: parent
                            anchors.margins: -6
                            hoverEnabled: true
                            onClicked: FileOperations.cancel()
                        }
                    }
                }

                // The view, and what shapes it; a click opens the view
                // options (sort, columns, zoom) that the header used to hold.
                Text {
                    id: viewOptionsButton
                    readonly property string tip: qsTr("View options")
                    textFormat: Text.PlainText
                    visible: root.currentTab !== null
                    text: root.currentTab ? root.viewMode + (root.currentTab.showHidden ? " +hidden" : "") + " ▾"
                                          : ""
                    color: viewOptionsMouse.containsMouse ? Colors.text : Colors.textDim
                    font.family: Colors.mono
                    font.pixelSize: 10

                    MouseArea {
                        id: viewOptionsMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: viewOptionsMenu.popup(viewOptionsButton, 0, -viewOptionsMenu.height)
                    }

                    ToolTip.visible: viewOptionsMouse.containsMouse
                    ToolTip.text: tip
                    ToolTip.delay: 600
                }

                // selected/total, the way a pager shows a position.
                Text {
                    id: countLabel
                    textFormat: Text.PlainText
                    visible: root.currentTab !== null
                    text: root.currentTab
                          ? (root.currentTab.selectionCount > 0 ? root.currentTab.selectionCount + "/" : "")
                            + root.currentTab.files.count
                          : ""
                    color: root.currentTab && root.currentTab.selectionCount > 0 ? Colors.text : Colors.textDim
                    font.family: Colors.mono
                    font.pixelSize: 10
                }

                // Room left where this folder lives — re-read whenever the folder
                // changes or an operation finishes, which is when it moves.
                Text {
                    id: freeSpaceLabel
                    textFormat: Text.PlainText
                    readonly property real bytes: FileOperations.busy, root.currentPath
                                                  ? Platform.freeSpace(root.currentPath) : -1
                    visible: !FileOperations.busy && bytes >= 0
                    text: qsTr("%1 free").arg(Platform.formatSize(bytes))
                    color: Colors.textDim
                    font.family: Colors.mono
                    font.pixelSize: 10
                }

                Text {
                    id: keysHint
                    textFormat: Text.PlainText
                    // `?` is a vim key; classic mode types it into type-ahead.
                    text: Settings.keyboardMode === "vim" ? "? keys" : "Ctrl+? keys"
                    color: keysMouse.containsMouse ? Colors.text : Colors.textDim
                    font.family: Colors.mono
                    font.pixelSize: 10

                    MouseArea {
                        id: keysMouse
                        anchors.fill: parent
                        anchors.margins: -4
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: shortcutsDialog.open()
                    }
                }
            }

            // An error that nobody dismissed should not sit there forever.
            Timer {
                running: FileOperations.lastError !== "" && !FileOperations.busy
                interval: 8000
                onTriggered: FileOperations.clearError()
            }
        }
    }

    // The tab is the owner of the query; the field follows it — when tabs
    // switch, and when navigation clears the search.
    onSearchQueryChanged: {
        if (searchField.text !== searchQuery)
            searchField.text = searchQuery;
        if (searchQuery === "" && searchOpen && !searchField.activeFocus)
            searchOpen = false;
    }

    // A short-lived message on the status line, for events that have no
    // other surface — a failed mount, mainly.
    property string flashText: ""

    function flash(message) {
        flashText = message;
        flashTimer.restart();
    }

    Timer {
        id: flashTimer
        interval: 8000
        onTriggered: root.flashText = ""
    }

    // ---- keyboard ---------------------------------------------------------

    readonly property bool quickViewOpen: quickView.open
    readonly property bool quickViewPip: quickView.pip
    // The quick view's subject, for WindowState and the tests.
    readonly property string quickViewPath: quickView.location

    // Vim keys that reach past the pane: Keymap.js lists them all.
    function runCommand(command, arg) {
        switch (command) {
        case "copy": Clipboard.copyFiles(selection()); break;
        case "cut": Clipboard.cutFiles(selection()); break;
        case "paste": paste(); break;
        case "duplicate": duplicateSelected(); break;
        case "copyToOther": transferToOtherPane(false); break;
        case "moveToOther": transferToOtherPane(true); break;
        case "rename": renameSelected(); break;
        case "newFolder": newFolder(); break;
        case "trash": trashSelected(); break;
        case "undo": FileOperations.undo(); break;
        case "redo": FileOperations.redo(); break;
        case "newTab": addTab(currentTab ? currentTab.path : Platform.homePath()); break;
        case "closeTab": closeTab(stack.currentIndex); break;
        case "otherPane": if (currentSlot) currentSlot.cyclePane(); break;
        case "search": openSearch(); break;
        case "selectPattern": askSelectPattern(); break;
        case "help": shortcutsDialog.open(); break;
        case "palette": openPalette(arg || ""); break;
        case "info": toggleInfoPanel(); break;
        case "pip":
            if (quickView.open)
                quickView.togglePip();
            break;
        case "quickview":
            if (quickView.open)
                quickView.close();
            else if (currentTab)
                quickView.show(currentTab);
            break;
        case "place": sidebar.activatePlace(arg); break;
        case "sidebar":
            if (sidebarNarrow)
                sidebarOverlayOpen = true;
            else if (!Settings.showSidebar)
                Settings.showSidebar = true;
            sidebar.focusPlaces();
            break;
        case "escape":
            if (searchOpen)
                closeSearch();
            break;
        }
    }

    function toggleInfoPanel() { Settings.showInfoPanel = !Settings.showInfoPanel; }

    // ---- palette ----------------------------------------------------------

    Commands {
        id: commands
        win: root
    }

    CommandPalette {
        id: commandPalette
        win: root
        registry: commands
    }

    readonly property bool paletteOpen: commandPalette.opened
    readonly property int currentTabIndex: stack.currentIndex

    // The status line's mode, Vim-style: what the keyboard is talking to.
    readonly property string mode: {
        if (paletteOpen)
            return "COMMAND";
        if (pathBar.editing)
            return "PATH";
        if (searchOpen)
            return "SEARCH";
        if (quickView.open)
            return "PREVIEW";
        if (currentTab && currentTab.filterEditing)
            return "FILTER";
        if (currentTab && currentTab.selectionCount > 1)
            return "VISUAL";
        return "NORMAL";
    }

    function openPalette(prefix) {
        if (commandPalette.opened)
            commandPalette.setMode(prefix || "");
        else
            commandPalette.openWith(prefix || "");
    }

    function editLocation() { pathBar.beginEditing(); }
    function openPreferences() { preferencesDialog.open(); }
    function openAbout() { aboutDialog.open(); }
    function openVisibleColumns() { visibleColumnsDialog.open(); }

    function sortBy(key, descending) {
        if (!currentTab)
            return;
        currentTab.sortKey = key;
        currentTab.sortDescending = descending;
    }

    // The sidebar's places for the palette's ~ mode, with the 1–9 jump keys.
    function placeList() {
        const vim = Settings.keyboardMode === "vim";
        return sidebar.placeList().map((p, i) => ({
            name: p.name, location: p.location,
            key: vim && i < 9 ? String(i + 1) : ""
        }));
    }

    // Ctrl+Shift+D / vim Y: a copy beside each selected item, "name (copy)".
    // The copy's own rename-on-conflict naming does the work; grouped by
    // folder because search results span several.
    function duplicateSelected() {
        if (!currentTab || viewingRecent || viewingTrash)
            return;
        const byFolder = {};
        for (const path of selection()) {
            const parent = Platform.parentPath(path);
            (byFolder[parent] = byFolder[parent] || []).push(path);
        }
        for (const folder in byFolder)
            FileOperations.copy(byFolder[folder], folder, FileOperations.RenameNew);
    }

    // Split view's F5 / F6 (vim c / m): the selection into the other pane's
    // folder, through the same conflict dialog as paste.
    function transferToOtherPane(isMove) {
        const other = currentSlot ? currentSlot.otherPane : null;
        if (!other) {
            flash(qsTr("Open split view (F3) to copy or move between panes"));
            return;
        }
        const paths = selection();
        if (paths.length === 0 || other.path === currentTab.path)
            return;
        const target = other.path;
        if (target === "trash:///" || target === "recent:///" || target === "network:///"
            || target === "starred:///") {
            flash(qsTr("The other pane is not a folder files can go into"));
            return;
        }
        startTransfer(paths, target, isMove, false);
    }

    function askSelectPattern() {
        if (!currentTab)
            return;
        selectPatternPrompt.initialText = "*";
        selectPatternPrompt.ask();
    }

    // ---- file operations --------------------------------------------------

    function selection() { return currentTab ? currentTab.selectedPaths() : []; }

    // Switching views also persists the choice as the default for new tabs
    // and windows — Nautilus writes default-folder-viewer the same way.
    readonly property var viewModes: ["list", "icon", "columns", "gallery"]

    function nextViewMode() {
        const at = currentTab ? viewModes.indexOf(currentTab.viewMode) : -1;
        return viewModes[(at + 1) % viewModes.length];
    }

    function setViewMode(mode) {
        if (!currentTab)
            return;
        currentTab.viewMode = mode;
        Settings.defaultViewMode = mode;
    }

    // A modal dialog takes the keyboard; nothing gives it back automatically.
    // Without this, the first New Folder leaves the window unnavigable.
    function returnFocusToView() {
        if (currentTab)
            currentTab.forceActiveFocus();
    }

    function newFolder() {
        if (!viewWritable)
            return;
        newFolderPrompt.initialText = qsTr("New Folder");
        newFolderPrompt.ask();
    }

    function renameSelected() {
        // Recent rows are pointers at files elsewhere; renaming the pointer
        // is meaningless and renaming the target would strand it. Nautilus
        // disables rename here too.
        if (viewingRecent)
            return;
        const paths = selection();
        if (paths.length === 0)
            return;
        if (paths.length === 1) {
            renamePrompt.target = paths[0];
            renamePrompt.initialText = Platform.baseName(paths[0]);
            renamePrompt.ask();
        } else if (currentTab.batchRenamable) {
            batchRenameDialog.askAbout(currentTab.selectedItems(), currentTab.allNames());
        }
    }

    // The virtual roots make no sense as bookmarks; anything with a real
    // location — local or a mounted share — is fair game, as in Nautilus.
    readonly property bool folderBookmarkable: {
        if (!currentTab || currentTab.path === "")
            return false;
        const path = currentTab.path;
        return path !== "trash:///" && path !== "recent:///"
            && path !== "network:///" && path !== "starred:///";
    }

    function toggleBookmark() {
        if (folderBookmarkable)
            sidebar.toggleBookmark(currentTab.path);
    }

    // Rows in the trash cannot be trashed again — there, the delete key means
    // what Nautilus makes it mean: permanent delete, behind the confirm.
    readonly property bool viewingTrash: currentTab ? currentTab.path === "trash:///" : false

    // Recent rows resolve to their target files for every operation (the tab
    // does that in selectedPaths/activate), but the view itself takes no new
    // files and its rows keep their real names — so rename, paste and New
    // Folder are off here, as in Nautilus.
    readonly property bool viewingRecent: currentTab ? currentTab.viewingRecent : false

    // Whether the view is somewhere files can be created: any real directory,
    // not one of the four virtual roots. Gates New Folder and Paste.
    readonly property bool viewWritable: {
        if (!currentTab || currentTab.path === "")
            return false;
        const path = currentTab.path;
        return path !== "trash:///" && path !== "recent:///"
            && path !== "network:///" && path !== "starred:///";
    }

    // The Network view carries its own chrome: the server bar below the view
    // and Forget Connection in the context menu.
    readonly property bool viewingNetwork: currentTab ? currentTab.viewingNetwork : false
    readonly property bool serverBarVisible: viewingNetwork

    // The protocol help's table cells (label, prefix, label, prefix…),
    // filtered to what gvfs on this machine can actually mount.
    readonly property var protocolCells: {
        const schemes = Platform.supportedSchemes();
        const known = [
            { scheme: "afp", label: qsTr("AppleTalk"), prefix: "afp://" },
            { scheme: "ftp", label: qsTr("File Transfer Protocol"),
              prefix: schemes.indexOf("ftps") >= 0 ? "ftp:// or ftps://" : "ftp://" },
            { scheme: "nfs", label: qsTr("Network File System"), prefix: "nfs://" },
            { scheme: "smb", label: qsTr("Samba"), prefix: "smb://" },
            { scheme: "sftp", label: qsTr("SSH File Transfer Protocol"),
              prefix: schemes.indexOf("ssh") >= 0 ? "sftp:// or ssh://" : "sftp://" },
            { scheme: "dav", label: qsTr("WebDAV"),
              prefix: schemes.indexOf("davs") >= 0 ? "dav:// or davs://" : "dav://" },
        ];
        const cells = [];
        for (const p of known) {
            if (schemes.indexOf(p.scheme) < 0)
                continue;
            cells.push({ text: p.label, dim: false });
            cells.push({ text: p.prefix, dim: true });
        }
        return cells;
    }

    // The address being connected to through the server bar. Only a mount
    // this window asked for lands in the known-connections store — a sidebar
    // click on an unmounted volume must not.
    property string pendingServer: ""

    function connectToServer() {
        if (!currentTab)
            return;
        const address = serverField.text.trim();
        if (address === "")
            return;
        if (address.indexOf("://") < 0) {
            flash(qsTr("Addresses need a protocol prefix — smb://, sftp://, ftp://…"));
            return;
        }
        pendingServer = Platform.resolvePath(address, "");
        windowMounter.mountLocation(address);
    }

    function compressSelected() {
        const paths = selection();
        if (paths.length === 0 || !currentTab)
            return;
        // A single item suggests its own name, as Nautilus does.
        const suggested = paths.length === 1
            ? Platform.archiveStem(Platform.baseName(paths[0])) : "";
        compressDialog.askAbout(paths, currentTab.path, suggested, currentTab.allNames());
    }

    function extractSelected() {
        const paths = selection();
        if (paths.length > 0 && currentTab)
            FileOperations.extractHere(paths, currentTab.path);
    }

    // "Extract to…" — same operation, the destination picked first.
    function extractSelectedTo() {
        const paths = selection();
        if (paths.length === 0 || !currentTab)
            return;
        extractPicker.pendingArchives = paths;
        const what = paths.length === 1
            ? "\u201c" + Platform.baseName(paths[0]) + "\u201d"
            : qsTr("%1 archives").arg(paths.length);
        extractPicker.askFor(currentTab.path, qsTr("Extract %1 to:").arg(what));
    }

    function trashSelected() {
        if (viewingTrash) {
            deleteSelected();
            return;
        }
        const paths = selection();
        if (paths.length > 0)
            FileOperations.trash(paths, root);
    }

    function restoreSelected() {
        if (!currentTab)
            return;
        const originals = currentTab.selectedOrigPaths();
        if (originals.length > 0)
            FileOperations.restoreFromTrash(originals);
    }

    function deleteSelected() {
        const paths = selection();
        if (paths.length === 0)
            return;
        deleteConfirm.message = paths.length === 1
            ? qsTr("Permanently delete “%1”?").arg(Platform.baseName(paths[0]))
            : qsTr("Permanently delete %1 items?").arg(paths.length);
        deleteConfirm.detail = qsTr("This cannot be undone.");
        deleteConfirm.pending = paths;
        deleteConfirm.open();
    }

    function paste() {
        if (!currentTab || !viewWritable)
            return;
        // Only a cut is used up by its paste; copied files stay on the
        // clipboard to be pasted again, as in Nautilus.
        const cut = Clipboard.isCut();
        startTransfer(Clipboard.paths(), currentTab.path, cut, cut);
    }

    // One flow for everything that lands files somewhere: paste and drops
    // both check for name clashes up front and share the conflict dialog.
    property var pendingTransfer: null

    function startTransfer(paths, destination, isMove, fromCut) {
        if (paths.length === 0 || !destination)
            return;
        pendingTransfer = { paths: paths, destination: destination,
                            isMove: isMove, fromCut: fromCut === true };
        const clashes = Platform.collisions(paths, destination);
        if (clashes.length > 0)
            conflictDialog.askAbout(clashes);
        else
            performTransfer(FileOperations.RenameNew);
    }

    function performTransfer(policy) {
        const transfer = pendingTransfer;
        pendingTransfer = null;
        if (!transfer)
            return;
        if (transfer.isMove)
            FileOperations.move(transfer.paths, transfer.destination, policy);
        else
            FileOperations.copy(transfer.paths, transfer.destination, policy);
        if (transfer.fromCut)
            Clipboard.clear();
    }

    PromptDialog {
        id: newFolderPrompt
        onClosed: root.returnFocusToView()
        prompt: qsTr("Name for the new folder")
        onAccepted_: name => FileOperations.createFolder(root.currentTab.path, name)
    }

    // Select by pattern (Ctrl+S, vim *): a glob by default, re: for a regex.
    PromptDialog {
        id: selectPatternPrompt
        onClosed: root.returnFocusToView()
        prompt: qsTr("Select items matching (*.jpg, IMG_????.*, re:^draft)")
        onAccepted_: pattern => {
            const error = FileSortFilterModel.patternError(pattern, "auto");
            if (error !== "") {
                root.flash(error);
                return;
            }
            const count = root.currentTab.selectMatching(pattern, "auto");
            root.flash(count === 0 ? qsTr("Nothing matches “%1”").arg(pattern)
                                   : count === 1 ? qsTr("1 item selected")
                                   : qsTr("%1 items selected").arg(count));
        }
    }

    PromptDialog {
        id: renamePrompt
        onClosed: root.returnFocusToView()
        property string target: ""
        prompt: qsTr("New name")
        selectStem: true
        onAccepted_: name => FileOperations.rename(target, name)
    }

    ConfirmDialog {
        id: deleteConfirm
        onClosed: root.returnFocusToView()
        property var pending: []
        confirmText: qsTr("Delete")
        onConfirmed: FileOperations.deletePermanently(pending)
    }

    ConfirmDialog {
        id: emptyTrashConfirm
        onClosed: root.returnFocusToView()
        message: qsTr("Empty the trash?")
        detail: qsTr("Everything in the trash will be permanently deleted. This cannot be undone.")
        confirmText: qsTr("Empty Trash")
        onConfirmed: FileOperations.emptyTrash()
    }

    ConflictDialog {
        id: conflictDialog
        onClosed: root.returnFocusToView()
        onChosen: policy => root.performTransfer(policy)
    }

    PropertiesDialog {
        id: propertiesDialog
        objectName: "propertiesDialog"
        onClosed: root.returnFocusToView()
    }

    BatchRenameDialog {
        id: batchRenameDialog
        onClosed: root.returnFocusToView()
    }

    CompressDialog {
        id: compressDialog
        onClosed: root.returnFocusToView()
    }

    FolderPickerDialog {
        id: extractPicker

        property var pendingArchives: []

        acceptLabel: qsTr("Extract")
        onPicked: path => FileOperations.extractHere(pendingArchives, path)
        onClosed: root.returnFocusToView()
    }

    // An extract hit an encrypted archive: ask, then replay with the answer.
    PromptDialog {
        id: passphraseDialog

        secret: true
        onAccepted_: text => FileOperations.providePassphrase(text)
        onRejected: FileOperations.declinePassphrase()
        onClosed: root.returnFocusToView()
    }

    // An extract expanded far past its archive's size: a likely zip bomb.
    // Closing without confirming (Cancel, Escape) drops it.
    ConfirmDialog {
        id: largeExtractionConfirm

        property bool answered: false

        confirmText: qsTr("Extract Anyway")
        detail: qsTr("It expands to far more than its own size — archives built to fill the disk look like this. Nothing from it has been extracted yet.")
        onAboutToShow: answered = false
        onConfirmed: {
            answered = true;
            FileOperations.confirmLargeExtraction();
        }
        onClosed: {
            if (!answered)
                FileOperations.declineLargeExtraction();
            root.returnFocusToView();
        }
    }

    Connections {
        target: FileOperations
        function onLargeExtractionNeedsConfirmation(archiveName) {
            largeExtractionConfirm.message =
                qsTr("Extract \u201c%1\u201d?").arg(archiveName);
            largeExtractionConfirm.open();
        }
        function onPassphraseNeeded(archiveName) {
            passphraseDialog.prompt =
                qsTr("\u201c%1\u201d is password-protected. Enter the password:")
                    .arg(archiveName);
            passphraseDialog.initialText = "";
            passphraseDialog.ask();
        }
    }

    // ---- mounting ---------------------------------------------------------

    Mounter {
        id: windowMounter

        // The tab that asked is reloaded once its filesystem is there. Only
        // the matching tab: the user may have moved on meanwhile.
        onMounted: location => {
            if (location === root.pendingServer) {
                // A server-bar connect: remember the address and go there.
                ServerStore.add(location);
                root.pendingServer = "";
                serverField.clear();
                if (root.currentTab)
                    root.currentTab.navigate(location);
                return;
            }
            if (root.currentTab && root.currentTab.path === location)
                root.currentTab.reload();
        }
        onMountFailed: (location, message) => {
            if (location === root.pendingServer)
                root.pendingServer = "";
            if (message !== "") {
                root.flash(qsTr("Could not mount: %1").arg(message));
            } else if (root.currentTab && root.currentTab.path === location) {
                // Cancelled from the dialog — leave the place, don't re-ask.
                root.currentTab.goBack();
            }
        }
        onPromptAborted: {
            credentialDialog.abort();
            mountQuestion.close();
        }
        onAskQuestion: (message, choices) => {
            mountQuestion.message = message;
            mountQuestion.choices = choices;
            mountQuestion.open();
        }
        onAskPassword: (message, defaultUser, defaultDomain, needsUsername, needsDomain, needsPassword, canAnonymous) => {
            credentialDialog.ask(message, defaultUser, defaultDomain,
                                 needsUsername, needsDomain, needsPassword, canAnonymous);
        }
    }

    OmDialog {
        id: mountQuestion
        property string message: ""
        property var choices: []
        title: qsTr("Server verification")
        anchors.centerIn: Overlay.overlay
        width: Math.min(500, root.width - 32)
        modal: true
        closePolicy: Popup.CloseOnEscape
        standardButtons: Dialog.Cancel
        onRejected: windowMounter.cancelPassword()
        onClosed: root.returnFocusToView()
        Column {
            width: parent.width
            spacing: 12
            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: mountQuestion.message
                wrapMode: Text.Wrap
                color: Colors.text
            }
            Repeater {
                model: mountQuestion.choices
                OmButton {
                    required property int index
                    required property string modelData
                    width: parent.width
                    text: modelData
                    onClicked: {
                        windowMounter.answerQuestion(index);
                        mountQuestion.close();
                    }
                }
            }
        }
    }

    CredentialDialog {
        id: credentialDialog
        onClosed: root.returnFocusToView()
        onAnswered: (username, domain, password, anonymous, remember) =>
            windowMounter.providePassword(username, domain, password, anonymous, remember)
        onDismissed: windowMounter.cancelPassword()
    }

    // Properties of the selection, or of the folder being viewed when nothing
    // is selected — which is how you ask "how big is this folder?".
    function showProperties() {
        if (!currentTab)
            return;
        const paths = selection();
        propertiesDialog.show(paths.length > 0 ? paths : [currentTab.path]);
    }

    // Called from C++ for FileManager1's ShowItemProperties, so another
    // application's "Properties" button lands on the real dialog.
    function showPropertiesFor(paths) {
        if (paths.length > 0)
            propertiesDialog.show(paths);
    }

    // Trash does not exist on every filesystem — tmpfs has none. Rather than
    // leaving the user with a bare error, offer the thing they actually meant.
    ConfirmDialog {
        id: trashUnavailable
        onClosed: root.returnFocusToView()
        property var pending: []
        message: qsTr("These files can't be moved to the trash.")
        detail: qsTr("This location has no trash. Delete them permanently instead?")
        confirmText: qsTr("Delete permanently")
        onConfirmed: FileOperations.deletePermanently(pending)
    }

    Connections {
        target: FileOperations
        function onTrashUnavailable(paths, requester) {
            if (requester !== root || paths.length === 0)
                return;
            trashUnavailable.pending = paths;
            trashUnavailable.open();
        }
    }

    // ---- main menu (the hamburger) ----------------------------------------

    OmMenu {
        id: mainMenu

        OmMenuItem {
            text: qsTr("New Window")
            glyph: "window-new"
            shortcut: "Ctrl+N"
            onTriggered: App.openWindow(root.currentTab ? root.currentTab.path : Platform.homePath())
        }

        OmMenuItem {
            text: qsTr("New Tab")
            glyph: "tab-new"
            shortcut: "Ctrl+T"
            vimKey: "t"
            onTriggered: root.addTab(root.currentTab ? root.currentTab.path : Platform.homePath())
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("Undo")
            glyph: "undo"
            shortcut: "Ctrl+Z"
            vimKey: "u"
            enabled: FileOperations.canUndo
            onTriggered: FileOperations.undo()
        }

        OmMenuItem {
            text: qsTr("Redo")
            glyph: "redo"
            shortcut: "Ctrl+Shift+Z"
            vimKey: "U"
            enabled: FileOperations.canRedo
            onTriggered: FileOperations.redo()
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("Preferences")
            glyph: "gear"
            shortcut: "Ctrl+,"
            onTriggered: preferencesDialog.open()
        }

        OmMenuItem {
            text: qsTr("Keyboard Shortcuts")
            glyph: "keyboard"
            shortcut: "Ctrl+?"
            vimKey: "?"
            onTriggered: shortcutsDialog.open()
        }

        OmMenuItem {
            text: qsTr("About Rook")
            glyph: "info"
            onTriggered: aboutDialog.open()
        }
    }

    PreferencesDialog {
        id: preferencesDialog
        objectName: "preferencesDialog"
        onClosed: root.returnFocusToView()
    }

    ShortcutsDialog {
        id: shortcutsDialog
        onClosed: root.returnFocusToView()
    }

    AboutDialog {
        id: aboutDialog
        onClosed: root.returnFocusToView()
    }

    VisibleColumnsDialog {
        id: visibleColumnsDialog
        onClosed: root.returnFocusToView()
    }

    // ---- view options menu ------------------------------------------------

    // Nautilus's view-options popover (the ▾ next to the view toggle): icon
    // size, the sort orders, hidden files. Sort state lives on the tab, so
    // this menu and the list view's column headers can never disagree.
    OmMenu {
        id: viewOptionsMenu

        // Sampled when the menu opens rather than bound: triggering a
        // checkable MenuItem writes `checked`, which would sever a binding
        // the first time any item was clicked.
        onAboutToShow: {
            const key = root.currentTab ? root.currentTab.sortKey : FileSortFilterModel.ByName;
            const desc = root.currentTab ? root.currentTab.sortDescending : false;
            sortAZ.checked = key === FileSortFilterModel.ByName && !desc;
            sortZA.checked = key === FileSortFilterModel.ByName && desc;
            sortNewest.checked = key === FileSortFilterModel.ByModified && desc;
            sortOldest.checked = key === FileSortFilterModel.ByModified && !desc;
            sortLargest.checked = key === FileSortFilterModel.BySize && desc;
            sortByType.checked = key === FileSortFilterModel.ByType && !desc;
            hiddenToggle.checked = root.showHidden;
            viewList.checked = root.viewMode === "list";
            viewGrid.checked = root.viewMode === "icon";
            viewColumns.checked = root.viewMode === "columns";
            viewGallery.checked = root.viewMode === "gallery";
            sidebarToggle.checked = root.sidebarVisible;
        }

        function setSort(key, descending) {
            if (!root.currentTab)
                return;
            root.currentTab.sortKey = key;
            root.currentTab.sortDescending = descending;
        }

        // Both views resize their icons, with separate sizes per tab.
        Item {
            implicitWidth: 220
            implicitHeight: 36

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 6
                spacing: 0

                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: qsTr("Icon Size")
                    color: Colors.text
                    font.pixelSize: 13
                }

                ToolbarButton {
                    symbol: "−"
                    tip: "Zoom out (Ctrl+-)"
                    enabled: root.currentTab && root.currentTab.zoom > root.currentTab.minimumZoom
                    onTriggered: root.currentTab.zoomOut()
                }

                ToolbarButton {
                    symbol: "+"
                    tip: "Zoom in (Ctrl++)"
                    enabled: root.currentTab && root.currentTab.zoom < root.currentTab.maximumZoom
                    onTriggered: root.currentTab.zoomIn()
                }
            }
        }

        OmMenuSeparator {}

        OmMenuItem { text: qsTr("View"); enabled: false; sectionHeader: true }

        OmMenuItem { id: viewList; text: qsTr("List"); shortcut: "Ctrl+1"; checkable: true; onTriggered: root.setViewMode("list") }
        OmMenuItem { id: viewGrid; text: qsTr("Grid"); shortcut: "Ctrl+2"; checkable: true; onTriggered: root.setViewMode("icon") }
        OmMenuItem { id: viewColumns; text: qsTr("Columns"); shortcut: "Ctrl+3"; checkable: true; onTriggered: root.setViewMode("columns") }
        OmMenuItem { id: viewGallery; text: qsTr("Gallery"); shortcut: "Ctrl+4"; checkable: true; onTriggered: root.setViewMode("gallery") }

        OmMenuSeparator {}

        OmMenuItem { text: qsTr("Sort"); enabled: false; sectionHeader: true }

        OmMenuItem {
            id: sortAZ
            text: qsTr("A-Z")
            glyph: "sort"
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByName, false)
        }

        OmMenuItem {
            id: sortZA
            text: qsTr("Z-A")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByName, true)
        }

        OmMenuItem {
            id: sortNewest
            text: qsTr("Last Modified")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByModified, true)
        }

        OmMenuItem {
            id: sortOldest
            text: qsTr("First Modified")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByModified, false)
        }

        OmMenuItem {
            id: sortLargest
            text: qsTr("Size")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.BySize, true)
        }

        OmMenuItem {
            id: sortByType
            text: qsTr("Type")
            checkable: true
            onTriggered: viewOptionsMenu.setSort(FileSortFilterModel.ByType, false)
        }

        OmMenuSeparator {
            visible: root.viewMode === "list"
            height: visible ? implicitHeight : 0
        }

        OmMenuItem {
            text: qsTr("Visible Columns…")
            glyph: "columns"
            visible: root.viewMode === "list"
            height: visible ? implicitHeight : 0
            onTriggered: visibleColumnsDialog.open()
        }

        OmMenuSeparator {}

        OmMenuItem {
            id: hiddenToggle
            text: qsTr("Show Hidden Files")
            shortcut: "Ctrl+H"
            vimKey: "."
            checkable: true
            onTriggered: {
                if (root.currentTab)
                    root.currentTab.showHidden = !root.currentTab.showHidden;
            }
        }

        OmMenuItem {
            id: sidebarToggle
            text: qsTr("Show Sidebar")
            shortcut: "F9"
            checkable: true
            onTriggered: root.toggleSidebar()
        }
    }

    // ---- path-bar menu ----------------------------------------------------

    // The pill's kebab: Nautilus 50's current-folder menu. Folder-scoped —
    // never about the selection, whatever is selected.
    OmMenu {
        id: pathBarMenu

        property var templateFiles: []
        property bool newDocumentShown: false

        onAboutToShow: {
            // Same trick as contextMenu: a submenu can't play the height:0
            // trick, so New Document is inserted/removed on each open.
            templateFiles = Platform.templates();
            const wantNewDocument = templateFiles.length > 0 && root.currentTab
                && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path);
            if (wantNewDocument !== newDocumentShown) {
                if (wantNewDocument)
                    pathBarMenu.insertMenu(1, pathBarNewDocumentMenu);
                else
                    pathBarMenu.removeMenu(pathBarNewDocumentMenu);
                newDocumentShown = wantNewDocument;
            }
        }

        OmMenuItem {
            text: qsTr("New Folder…")
            glyph: "folder-new"
            shortcut: "Ctrl+Shift+N"
            vimKey: "a"
            enabled: root.currentTab !== null && root.viewWritable
            onTriggered: root.newFolder()
        }

        OmMenuItem {
            text: qsTr("Open With…")
            glyph: "open"
            enabled: root.currentTab !== null
            onTriggered: {
                propertiesDialog.show([root.currentTab.path]);
                propertiesDialog.selectTab(2);
            }
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("Reload")
            glyph: "reload"
            shortcut: "Ctrl+R"
            enabled: root.currentTab !== null
            onTriggered: root.currentTab.reload()
        }

        OmMenuItem {
            text: qsTr("Copy Location")
            glyph: "location"
            enabled: root.currentTab !== null
            onTriggered: Clipboard.copyText(root.currentTab.path)
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("Paste")
            glyph: "paste"
            shortcut: "Ctrl+V"
            vimKey: "p"
            enabled: Clipboard.hasFiles && root.currentTab !== null && root.viewWritable
            onTriggered: root.paste()
        }

        OmMenuItem {
            text: qsTr("Paste as Link")
            glyph: "link"
            enabled: Clipboard.hasFiles && root.currentTab !== null
                     && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path)
            onTriggered: FileOperations.createLink(Clipboard.paths(), root.currentTab.path)
        }

        OmMenuItem {
            text: qsTr("Select All")
            glyph: "select-all"
            shortcut: "Ctrl+A"
            vimKey: "V"
            enabled: root.currentTab !== null
            onTriggered: root.currentTab.selectAll()
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("Properties")
            glyph: "info"
            shortcut: "Ctrl+I"
            enabled: root.currentTab !== null
            onTriggered: propertiesDialog.show([root.currentTab.path])
        }
    }

    // Its New Document submenu — same template list and transfer flow as the
    // context menu's, but owned here: a Menu has one parent at a time.
    OmMenu {
        id: pathBarNewDocumentMenu
        title: qsTr("New Document")
        glyph: "file-new"

        Instantiator {
            model: pathBarMenu.templateFiles
            OmMenuItem {
                required property var modelData
                text: modelData.name
                glyph: "file-new"
                onTriggered: root.startTransfer([modelData.path],
                                                root.currentTab.path, false, false)
            }
            onObjectAdded: (index, object) => pathBarNewDocumentMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => pathBarNewDocumentMenu.removeItem(object)
        }
    }

    // ---- context menu -----------------------------------------------------

    OmMenu {
        id: contextMenu
        objectName: "contextMenu"

        // Sampled when the menu opens: the bookmarks file has no notify
        // signal a binding could follow — and the user actions depend on
        // what is selected right now.
        property bool folderBookmarked: false
        property string terminalDir: ""
        property bool selectionStarred: false
        property bool selectionForgettable: false
        property bool selectionExtractable: false
        property var selectionActions: []
        property var actionPaths: []
        property var openWithApps: []
        property string openWithPath: ""
        property var templateFiles: []
        property bool newDocumentShown: false
        onAboutToShow: {
            folderBookmarked = root.currentTab
                ? sidebar.isBookmarked(root.currentTab.path) : false;
            actionPaths = root.selection();
            // The Open With submenu is a single local file's answer: folders
            // open in the file manager itself, several files have no shared
            // handler list, and a remote URI would stall the menu on a
            // synchronous network stat.
            if (actionPaths.length === 1 && Platform.isLocal(actionPaths[0])
                && !Platform.isDir(actionPaths[0])) {
                openWithPath = actionPaths[0];
                openWithApps = Platform.applicationsFor(actionPaths[0]);
            } else {
                openWithPath = "";
                openWithApps = [];
            }
            // Where Open in Terminal lands: one selected local folder is
            // itself the place; one selected local file means its folder —
            // which in Recent is the target file's real location, since the
            // selection resolves to targets there; otherwise the folder
            // being viewed, when a shell can cd to it.
            if (actionPaths.length === 1 && Platform.isLocal(actionPaths[0]))
                terminalDir = Platform.isDir(actionPaths[0])
                            ? actionPaths[0] : Platform.parentPath(actionPaths[0]);
            else
                terminalDir = root.currentTab && Platform.isLocal(root.currentTab.path)
                            ? root.currentTab.path : "";
            selectionStarred = StarredStore.allStarred(actionPaths);
            selectionForgettable = root.viewingNetwork && ServerStore.allKnown(actionPaths);
            selectionExtractable = root.currentTab && root.currentTab.batchRenamable
                && Platform.isLocal(root.currentTab.path)
                && root.currentTab.selectionAllArchives();
            selectionActions = UserActions.actionsFor(actionPaths);

            // New Document exists only while ~/Templates has files and the
            // view is a real local directory — hidden otherwise, as
            // Nautilus. A submenu can't play the height:0 trick, so it is
            // inserted after New Folder / removed on each open.
            templateFiles = Platform.templates();
            const wantNewDocument = templateFiles.length > 0 && root.currentTab
                && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path);
            if (wantNewDocument !== newDocumentShown) {
                if (wantNewDocument) {
                    let at = contextMenu.count;
                    for (let i = 0; i < contextMenu.count; ++i) {
                        const item = contextMenu.itemAt(i);
                        if (item && item.text === qsTr("New Folder")) { at = i + 1; break; }
                    }
                    contextMenu.insertMenu(at, newDocumentMenu);
                } else {
                    contextMenu.removeMenu(newDocumentMenu);
                }
                newDocumentShown = wantNewDocument;
            }
        }

        OmMenuItem {
            text: "Open"
            glyph: "open"
            shortcut: "Enter"
            vimKey: "l"
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.currentTab.activate(root.currentTab.currentIndex)
        }

        // Nautilus's Open With submenu: the apps registered for this file's
        // type, then the full chooser. A static submenu rather than an
        // inserted/removed one: it stays put, greyed out when the selection
        // is not one local file.
        OmMenu {
            id: openWithMenu
            title: qsTr("Open With")
            glyph: "open"
            enabled: contextMenu.openWithPath !== ""

            Instantiator {
                model: contextMenu.openWithApps
                delegate: OmMenuItem {
                    required property var modelData
                    text: modelData.name
                    iconUrl: modelData.iconSource
                    onTriggered: Platform.openWith(modelData.id, [contextMenu.openWithPath])
                }
                onObjectAdded: (index, object) => openWithMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => openWithMenu.removeItem(object)
            }

            OmMenuSeparator {
                visible: contextMenu.openWithApps.length > 0
                height: visible ? implicitHeight : 0
            }

            OmMenuItem {
                text: qsTr("Other Application…")
                glyph: "grid"
                onTriggered: {
                    propertiesDialog.show([contextMenu.openWithPath]);
                    propertiesDialog.selectTab(2);
                }
            }
        }

        OmMenuItem {
            text: qsTr("Preview")
            glyph: "eye"
            shortcut: "Space"
            enabled: root.currentTab && root.currentTab.selectionCount === 1
            onTriggered: root.currentTab.preview(false)
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: "Open in Terminal"
            glyph: "terminal"
            // A terminal needs a working directory; the sampled terminalDir
            // is empty when there is nowhere local to land (trash://, an
            // unresolved remote view).
            enabled: contextMenu.terminalDir !== ""
            onTriggered: Platform.openTerminal(contextMenu.terminalDir)
        }

        OmMenuItem {
            text: "Open in New Tab"
            glyph: "tab-new"
            enabled: root.currentTab && root.currentTab.selectionCount === 1
            onTriggered: {
                const selected = root.currentTab.selectedPaths();
                if (selected.length === 1 && Platform.isDir(selected[0]))
                    root.addTab(selected[0]);
            }
        }

        OmMenuItem {
            text: contextMenu.folderBookmarked ? qsTr("Remove Bookmark") : qsTr("Bookmark This Folder")
            glyph: "bookmark"
            shortcut: "Ctrl+D"
            enabled: root.folderBookmarkable
            onTriggered: root.toggleBookmark()
        }

        OmMenuItem {
            // Nautilus's star toggle: Star when anything in the selection is
            // unstarred, Unstar only when the whole selection is starred.
            text: contextMenu.selectionStarred ? qsTr("Unstar") : qsTr("Star")
            glyph: "star"
            // Local folders, the Starred view itself and Recent (paths in
            // both resolve to the real files); trash rows have nothing to pin.
            enabled: root.currentTab && root.currentTab.selectionCount > 0
                     && (Platform.isLocal(root.currentTab.path)
                         || root.currentTab.path === "starred:///"
                         || root.viewingRecent)
            onTriggered: {
                if (contextMenu.selectionStarred)
                    StarredStore.unstar(contextMenu.actionPaths);
                else
                    StarredStore.star(contextMenu.actionPaths);
            }
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("Cut")
            glyph: "cut"
            shortcut: "Ctrl+X"
            vimKey: "x"
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: Clipboard.cutFiles(root.selection())
        }

        OmMenuItem {
            text: qsTr("Copy")
            glyph: "copy"
            shortcut: "Ctrl+C"
            vimKey: "y"
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: Clipboard.copyFiles(root.selection())
        }

        OmMenuItem {
            text: qsTr("Duplicate")
            glyph: "copy"
            shortcut: "Ctrl+Shift+D"
            vimKey: "Y"
            enabled: root.currentTab && root.currentTab.selectionCount > 0
                     && !root.viewingRecent && !root.viewingTrash
            onTriggered: root.duplicateSelected()
        }

        OmMenuItem {
            text: qsTr("Copy to Other Pane")
            glyph: "columns"
            shortcut: "F5"
            vimKey: "c"
            visible: root.splitOpen
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.transferToOtherPane(false)
        }

        OmMenuItem {
            text: qsTr("Move to Other Pane")
            glyph: "columns"
            shortcut: "F6"
            vimKey: "m"
            visible: root.splitOpen
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.transferToOtherPane(true)
        }

        OmMenuItem {
            text: qsTr("Paste")
            glyph: "paste"
            shortcut: "Ctrl+V"
            vimKey: "p"
            enabled: Clipboard.hasFiles && root.viewWritable
            onTriggered: root.paste()
        }

        OmMenuItem {
            // Nautilus's optional Create Link action — hidden until the
            // preference turns it on.
            text: qsTr("Create Link")
            glyph: "link"
            visible: Settings.showCreateLink
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
                     && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path)
            onTriggered: FileOperations.createLink(root.selection(), root.currentTab.path)
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("New Folder")
            glyph: "folder-new"
            shortcut: "Ctrl+Shift+N"
            vimKey: "a"
            enabled: root.viewWritable
            onTriggered: root.newFolder()
        }

        OmMenuItem {
            text: qsTr("Rename…")
            glyph: "rename"
            shortcut: "F2"
            vimKey: "r"
            // One item renames inline; several open batch rename, which
            // needs a real directory under it. Recent rows are pointers —
            // nothing there is renamable.
            enabled: root.currentTab && !root.viewingRecent
                     && (root.currentTab.selectionCount === 1
                     || (root.currentTab.selectionCount > 1 && root.currentTab.batchRenamable))
            onTriggered: root.renameSelected()
        }

        OmMenuItem {
            text: qsTr("Compress…")
            glyph: "archive"
            enabled: root.currentTab && root.currentTab.selectionCount > 0
                     && root.currentTab.batchRenamable && Platform.isLocal(root.currentTab.path)
            onTriggered: root.compressSelected()
        }

        OmMenuItem {
            text: qsTr("Extract Here")
            glyph: "extract"
            visible: contextMenu.selectionExtractable
            height: visible ? implicitHeight : 0
            onTriggered: root.extractSelected()
        }

        OmMenuItem {
            text: qsTr("Extract to…")
            glyph: "extract"
            visible: contextMenu.selectionExtractable
            height: visible ? implicitHeight : 0
            onTriggered: root.extractSelectedTo()
        }

        OmMenuItem {
            // Nautilus's label for dropping a server from the known list.
            // Only for remembered servers — a discovered or mounted row has
            // nothing here to forget.
            text: qsTr("Forget Connection")
            glyph: "delete"
            visible: root.viewingNetwork
            height: visible ? implicitHeight : 0
            enabled: contextMenu.selectionForgettable
            onTriggered: ServerStore.remove(contextMenu.actionPaths)
        }

        OmMenuItem {
            text: qsTr("Restore from Trash")
            glyph: "undo"
            visible: root.viewingTrash
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.restoreSelected()
        }

        OmMenuItem {
            text: qsTr("Empty Trash…")
            glyph: "trash"
            destructive: true
            visible: root.viewingTrash
            height: visible ? implicitHeight : 0
            enabled: root.visibleCount > 0
            onTriggered: emptyTrashConfirm.open()
        }

        OmMenuItem {
            text: qsTr("Move to Trash")
            glyph: "trash"
            destructive: true
            shortcut: "Delete"
            vimKey: "D"
            visible: !root.viewingTrash
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.trashSelected()
        }

        OmMenuItem {
            text: qsTr("Delete Permanently…")
            glyph: "delete"
            destructive: true
            shortcut: "Shift+Del"
            // Nautilus hides this behind a preference — except in the trash,
            // where permanent delete is the only kind there is. Shift+Delete
            // works regardless, as the preference dialog says.
            visible: root.viewingTrash || Settings.showDeletePermanently
            height: visible ? implicitHeight : 0
            enabled: root.currentTab && root.currentTab.selectionCount > 0
            onTriggered: root.deleteSelected()
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: qsTr("Properties")
            glyph: "info"
            shortcut: "Ctrl+I"
            onTriggered: root.showProperties()
        }

        OmMenuSeparator {}

        OmMenuItem {
            text: root.currentTab && root.currentTab.showHidden ? "Hide Hidden Files" : "Show Hidden Files"
            glyph: root.currentTab && root.currentTab.showHidden ? "eye-off" : "eye"
            shortcut: "Ctrl+H"
            vimKey: "."
            onTriggered: root.currentTab.showHidden = !root.currentTab.showHidden
        }

        OmMenuItem {
            text: "Reload"
            glyph: "reload"
            shortcut: "Ctrl+R"
            onTriggered: root.currentTab.reload()
        }

        OmMenuSeparator {
            visible: contextMenu.selectionActions.length > 0
            height: visible ? implicitHeight : 0
        }

        // The user actions (transcode, omarchy-send, …) — declarative TOML
        // files, the replacement for the nautilus-python extensions.
        // Instantiated fresh from whatever aboutToShow computed; addItem
        // appends after the static entries above.
        Instantiator {
            model: contextMenu.selectionActions
            delegate: OmMenuItem {
                required property var modelData
                text: modelData.label
                glyph: "bolt"
                onTriggered: UserActions.run(modelData.id, contextMenu.actionPaths)
            }
            onObjectAdded: (index, object) => contextMenu.addItem(object)
            onObjectRemoved: (index, object) => contextMenu.removeItem(object)
        }
    }

    // The New Document submenu — one item per file in ~/Templates, copied
    // into the folder being viewed through the shared transfer flow (so a
    // name clash gets the conflict dialog and the copy is one Ctrl+Z).
    // Declared outside contextMenu: the parent menu inserts and removes it
    // in onAboutToShow, because Nautilus hides the entry entirely when
    // there are no templates.
    OmMenu {
        id: newDocumentMenu
        title: qsTr("New Document")
        glyph: "file-new"

        Instantiator {
            model: contextMenu.templateFiles
            OmMenuItem {
                required property var modelData
                text: modelData.name
                glyph: "file-new"
                onTriggered: root.startTransfer([modelData.path],
                                                root.currentTab.path, false, false)
            }
            onObjectAdded: (index, object) => newDocumentMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => newDocumentMenu.removeItem(object)
        }
    }

    // ---- drag label --------------------------------------------------------

    // Beneath every real drop target: keeps the drag card following the
    // pointer over the toolbar, status line and other places that take no
    // drop (it shows the card without an action there, and ignores drops).
    FileDropArea {
        parent: root.contentItem
        anchors.fill: parent
        z: -1000
        destination: ""
    }

    // rook's own drag: the one card, drawn here and live —
    //   [▣ 5 items | Move]
    // the right-hand section following Ctrl / Shift / Alt and the target
    // under the pointer (no section where a drop would do nothing).
    Rectangle {
        id: dragCard
        objectName: "dragCard"
        parent: root.contentItem
        z: 1000
        visible: DragState.ownDrag && DragState.area !== null && DragState.window === root
        x: DragState.x - DragState.cardHotSpot.x
        y: DragState.y - DragState.cardHotSpot.y
        width: cardRow.implicitWidth + 24
        height: 56
        radius: Colors.radius
        color: Qt.alpha(Colors.chrome, 1)
        border.color: DragState.destructive ? Colors.error
                    : DragState.word !== "" ? Colors.accent : Colors.border

        Row {
            id: cardRow
            x: 10
            height: parent.height
            spacing: 10

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 36
                height: 36
                source: DragState.cardIcon
                sourceSize: Qt.size(36, 36)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }

            Text {
                objectName: "dragCardText"
                textFormat: Text.PlainText
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 220)
                text: DragState.cardText
                color: Colors.text
                font.pixelSize: 13
                elide: Text.ElideMiddle
            }

            Rectangle {
                visible: DragState.word !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: 1
                height: 24
                color: Colors.border
            }

            Text {
                id: dragCardAction
                objectName: "dragCardAction"
                textFormat: Text.PlainText
                visible: DragState.word !== ""
                anchors.verticalCenter: parent.verticalCenter
                text: DragState.word
                color: DragState.destructive ? Colors.error : Colors.accent
                font.pixelSize: 13
                font.bold: true
            }
        }
    }

    // A drag from another app has no rook card: a badge beside the
    // pointer says what a drop here would do.
    Rectangle {
        id: dragLabel
        objectName: "dragLabel"
        parent: root.contentItem
        z: 1000
        visible: !DragState.ownDrag && DragState.active && DragState.window === root
        x: Math.min(DragState.x + 18, root.width - width - 4)
        y: Math.min(DragState.y + 22, root.height - height - 4)
        width: dragLabelText.implicitWidth + 16
        height: dragLabelText.implicitHeight + 10
        radius: Colors.radius
        color: DragState.destructive ? Colors.error : Colors.accent

        Text {
            id: dragLabelText
            objectName: "dragLabelText"
            textFormat: Text.PlainText
            anchors.centerIn: parent
            text: DragState.label
            color: Colors.window.hslLightness > 0.5 ? "#ffffff" : Qt.alpha(Colors.window, 1)
            font.pixelSize: 13
            font.bold: true
        }
    }

    // ---- mouse back/forward ----------------------------------------------

    // Buttons 8 and 9 on a mouse are back and forward everywhere else on the
    // desktop; a file manager that ignores them feels broken.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        z: 100
        onPressed: mouse => {
            if (!root.currentTab) {
                mouse.accepted = false;
                return;
            }
            if (mouse.button === Qt.BackButton)
                root.currentTab.goBack();
            else if (mouse.button === Qt.ForwardButton)
                root.currentTab.goForward();
        }
    }

    // ---- shortcuts --------------------------------------------------------

    Shortcut { sequence: "Ctrl+T"; onActivated: root.addTab(root.currentTab ? root.currentTab.path : Platform.homePath()) }
    Shortcut { sequence: "Ctrl+W"; onActivated: root.closeTab(stack.currentIndex) }
    Shortcut { sequence: "Ctrl+N"; onActivated: App.openWindow(root.currentTab ? root.currentTab.path : Platform.homePath()) }
    Shortcut { sequence: "Ctrl+Shift+W"; onActivated: root.close() }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: root.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: root.cycleTab(-1) }
    Shortcut { sequence: "Ctrl+PgDown"; onActivated: root.cycleTab(1) }
    Shortcut { sequence: "Ctrl+PgUp"; onActivated: root.cycleTab(-1) }

    Shortcut { sequence: "Alt+Left"; onActivated: if (root.currentTab) root.currentTab.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: if (root.currentTab) root.currentTab.goForward() }
    Shortcut { sequence: "Alt+Up"; onActivated: if (root.currentTab) root.currentTab.goUp() }
    Shortcut { sequence: "Alt+Home"; onActivated: if (root.currentTab) root.currentTab.navigate(Platform.homePath()) }

    // Split view: F3 toggles the second pane, F6 moves the keyboard (and the
    // chrome) between panes — the classic dual-pane bindings.
    Shortcut { sequence: "F3"; onActivated: if (root.currentSlot) root.currentSlot.toggleSplit() }
    // With the split open, F5 / F6 copy / move the selection across — the
    // dual-pane convention — and Ctrl+F6 (or Tab in vim keys) switches panes.
    Shortcut { sequence: "F6"; onActivated: if (root.splitOpen) root.transferToOtherPane(true) }
    Shortcut { sequence: "Ctrl+F6"; onActivated: if (root.currentSlot) root.currentSlot.cyclePane() }

    Shortcut { sequence: "F9"; onActivated: root.toggleSidebar() }
    // In picture-in-picture the files have the keys; Esc still closes the
    // card first (one layer at a time), unless a field is being typed in.
    Shortcut {
        sequence: "Escape"
        enabled: quickView.pip && root.currentTab !== null && !root.currentTab.filterEditing
                 && !searchField.activeFocus
        onActivated: quickView.close()
    }
    // Only while the sidebar is slid over; otherwise Escape stays the views'.
    Shortcut {
        sequence: "Escape"
        enabled: root.sidebarOverlayOpen
        onActivated: root.sidebarOverlayOpen = false
    }
    Shortcut { sequence: "Ctrl+D"; onActivated: root.toggleBookmark() }
    Shortcut { sequence: "Ctrl+F"; onActivated: root.openSearch() }
    Shortcut { sequence: "Ctrl+Shift+F"; onActivated: root.toggleSearchContent() }
    Shortcut { sequence: "Ctrl+L"; onActivated: pathBar.beginEditing() }
    Shortcut { sequence: "Ctrl+H"; onActivated: if (root.currentTab) root.currentTab.showHidden = !root.currentTab.showHidden }
    Shortcut { sequence: "Ctrl+A"; onActivated: if (root.currentTab) root.currentTab.selectAll() }
    Shortcut { sequence: "Ctrl+R"; onActivated: if (root.currentTab) root.currentTab.reload() }
    Shortcut {
        sequence: "F5"
        onActivated: {
            if (root.splitOpen)
                root.transferToOtherPane(false);
            else if (root.currentTab)
                root.currentTab.reload();
        }
    }
    Shortcut { sequence: "F11"; onActivated: root.toggleInfoPanel() }
    Shortcut { sequence: "Ctrl+S"; onActivated: root.askSelectPattern() }
    Shortcut { sequence: "Ctrl+Shift+S"; onActivated: if (root.currentTab) root.currentTab.openFilter() }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: root.duplicateSelected() }

    Shortcut { sequence: "Delete"; onActivated: root.trashSelected() }
    Shortcut { sequence: "Shift+Delete"; onActivated: root.deleteSelected() }
    Shortcut { sequence: "F2"; onActivated: root.renameSelected() }
    Shortcut { sequence: "Ctrl+C"; onActivated: Clipboard.copyFiles(root.selection()) }
    Shortcut { sequence: "Ctrl+X"; onActivated: Clipboard.cutFiles(root.selection()) }
    Shortcut { sequence: "Ctrl+V"; onActivated: root.paste() }
    Shortcut { sequence: "Ctrl+Z"; onActivated: FileOperations.undo() }
    Shortcut { sequence: "Ctrl+Shift+Z"; onActivated: FileOperations.redo() }
    Shortcut { sequence: "Ctrl+Shift+N"; onActivated: root.newFolder() }

    // Ctrl+I is Nautilus's; Alt+Return is what the rest of the desktop uses.
    Shortcut { sequence: "Ctrl+I"; onActivated: root.showProperties() }
    Shortcut { sequence: "Alt+Return"; onActivated: root.showProperties() }

    Shortcut { sequence: "Ctrl+Shift+P"; onActivated: root.openPalette("") }
    Shortcut { sequence: "F1"; onActivated: root.openPalette("") }
    Shortcut { sequence: "Ctrl+,"; onActivated: preferencesDialog.open() }
    Shortcut { sequence: "Ctrl+?"; onActivated: shortcutsDialog.open() }

    Shortcut { sequence: "Ctrl+1"; onActivated: root.setViewMode("list") }
    Shortcut { sequence: "Ctrl+2"; onActivated: root.setViewMode("icon") }
    Shortcut { sequence: "Ctrl+3"; onActivated: root.setViewMode("columns") }
    Shortcut { sequence: "Ctrl+4"; onActivated: root.setViewMode("gallery") }
    Shortcut { sequence: "Ctrl++"; onActivated: if (root.currentTab) root.currentTab.zoomIn() }
    Shortcut { sequence: "Ctrl+="; onActivated: if (root.currentTab) root.currentTab.zoomIn() }
    Shortcut { sequence: "Ctrl+-"; onActivated: if (root.currentTab) root.currentTab.zoomOut() }
    Shortcut { sequence: "Ctrl+0"; onActivated: if (root.currentTab) root.currentTab.resetZoom() }
}
