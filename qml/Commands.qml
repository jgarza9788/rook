import QtQuick
import Rook.Runtime

// The command registry: every action the palette can run, with the key that
// also runs it. Menus and shortcuts predate it and still call the window's
// functions directly; new features register here first (design doc,
// "Command palette"), so the palette is never the place that lags behind.
//
// A command is { id, title, category, keys: [classic, vim], enabled, run }.
// `list()` is evaluated each time the palette opens, so `enabled` reads the
// window as it is right then.
QtObject {
    id: registry

    // The window (Main.qml's root): commands run through its functions.
    required property var win

    function make(id, category, title, classicKey, vimKey, enabled, run) {
        return { id: id, category: category, title: title,
                 keys: [classicKey || "", vimKey || ""],
                 enabled: enabled, run: run };
    }

    // The key to show for a command in the current keyboard mode: the vim
    // key when vim keys are on and one exists, else the Ctrl/F-key one.
    function keyFor(command) {
        if (Settings.keyboardMode === "vim" && command.keys[1] !== "")
            return command.keys[1];
        return command.keys[0];
    }

    function list() {
        const w = win;
        const tab = w.currentTab;
        const hasTab = tab !== null && tab !== undefined;
        const hasSelection = hasTab && tab.selectionCount > 0;
        const here = hasTab ? tab.path : Platform.homePath();

        return [
            // ---- navigation
            make("go.back", "Go", qsTr("Back"), "Alt+Left", "",
                 hasTab && tab.history.canGoBack, () => tab.goBack()),
            make("go.forward", "Go", qsTr("Forward"), "Alt+Right", "",
                 hasTab && tab.history.canGoForward, () => tab.goForward()),
            make("go.up", "Go", qsTr("Parent Folder"), "Alt+Up", "-",
                 hasTab, () => tab.goUp()),
            make("go.home", "Go", qsTr("Home Folder"), "Alt+Home", "~",
                 hasTab, () => tab.navigate(Platform.homePath())),
            make("go.location", "Go", qsTr("Edit Location…"), "Ctrl+L", "",
                 hasTab, () => w.editLocation()),
            make("go.trash", "Go", qsTr("Trash"), "", "",
                 hasTab, () => tab.navigate("trash:///")),
            make("go.recent", "Go", qsTr("Recent"), "", "",
                 hasTab, () => tab.navigate("recent:///")),
            make("go.starred", "Go", qsTr("Starred"), "", "",
                 hasTab, () => tab.navigate("starred:///")),
            make("go.network", "Go", qsTr("Network"), "", "",
                 hasTab, () => tab.navigate("network:///")),

            // ---- files
            make("file.newFolder", "File", qsTr("New Folder…"), "Ctrl+Shift+N", "a",
                 hasTab && w.viewWritable, () => w.newFolder()),
            make("file.rename", "File", qsTr("Rename…"), "F2", "r",
                 hasSelection, () => w.renameSelected()),
            make("file.batchRename", "File", qsTr("Bulk Rename…"), "F2", "r",
                 hasTab && tab.selectionCount > 1, () => w.renameSelected()),
            make("file.copy", "File", qsTr("Copy"), "Ctrl+C", "y",
                 hasSelection, () => Clipboard.copyFiles(w.selection())),
            make("file.cut", "File", qsTr("Cut"), "Ctrl+X", "x",
                 hasSelection, () => Clipboard.cutFiles(w.selection())),
            make("file.paste", "File", qsTr("Paste"), "Ctrl+V", "p",
                 Clipboard.hasFiles && hasTab && w.viewWritable, () => w.paste()),
            make("file.pasteLink", "File", qsTr("Paste as Link"), "", "",
                 Clipboard.hasFiles && hasTab,
                 () => FileOperations.createLink(Clipboard.paths(), tab.path)),
            make("file.duplicate", "File", qsTr("Duplicate"), "Ctrl+Shift+D", "Y",
                 hasSelection, () => w.duplicateSelected()),
            make("file.copyToOther", "File", qsTr("Copy to Other Pane"), "F5", "c",
                 hasSelection && w.splitOpen, () => w.transferToOtherPane(false)),
            make("file.moveToOther", "File", qsTr("Move to Other Pane"), "F6", "m",
                 hasSelection && w.splitOpen, () => w.transferToOtherPane(true)),
            make("file.trash", "File", qsTr("Move to Trash"), "Delete", "D",
                 hasSelection, () => w.trashSelected()),
            make("file.delete", "File", qsTr("Delete Permanently…"), "Shift+Delete", "",
                 hasSelection, () => w.deleteSelected()),
            make("file.properties", "File", qsTr("Properties"), "Ctrl+I", "",
                 hasTab, () => w.showProperties()),
            make("file.copyPath", "File", qsTr("Copy Location"), "", "",
                 hasTab, () => Clipboard.copyText(here)),
            make("file.bookmark", "File", qsTr("Bookmark This Folder"), "Ctrl+D", "",
                 hasTab, () => w.toggleBookmark()),
            make("file.terminal", "File", qsTr("Open Terminal Here"), "", "",
                 hasTab && Platform.isLocal(here), () => Platform.openTerminal(here)),
            make("edit.undo", "Edit", qsTr("Undo"), "Ctrl+Z", "u",
                 FileOperations.canUndo, () => FileOperations.undo()),
            make("edit.redo", "Edit", qsTr("Redo"), "Ctrl+Shift+Z", "U",
                 FileOperations.canRedo, () => FileOperations.redo()),

            // ---- select and find
            make("select.all", "Select", qsTr("Select All"), "Ctrl+A", "V",
                 hasTab, () => tab.selectAll()),
            make("select.pattern", "Select", qsTr("Select by Pattern…"), "Ctrl+S", "*",
                 hasTab, () => w.askSelectPattern()),
            make("find.filter", "Find", qsTr("Filter This Folder"), "Ctrl+Shift+S", "/",
                 hasTab, () => tab.openFilter()),
            make("find.search", "Find", qsTr("Search Below This Folder"), "Ctrl+F", "f",
                 hasTab, () => w.openSearch()),
            make("find.contents", "Find", qsTr("Search File Contents"), "Ctrl+Shift+F", "",
                 w.searchContentAvailable, () => w.toggleSearchContent()),

            // ---- view
            make("view.list", "View", qsTr("List View"), "Ctrl+1", "",
                 hasTab, () => w.setViewMode("list")),
            make("view.grid", "View", qsTr("Grid View"), "Ctrl+2", "",
                 hasTab, () => w.setViewMode("icon")),
            make("view.columns", "View", qsTr("Columns View"), "Ctrl+3", "",
                 hasTab, () => w.setViewMode("columns")),
            make("view.gallery", "View", qsTr("Gallery View"), "Ctrl+4", "",
                 hasTab, () => w.setViewMode("gallery")),
            make("view.hidden", "View", qsTr("Toggle Hidden Files"), "Ctrl+H", ".",
                 hasTab, () => tab.showHidden = !tab.showHidden),
            make("view.sortName", "View", qsTr("Sort by Name"), "", "s",
                 hasTab, () => w.sortBy(FileSortFilterModel.ByName, false)),
            make("view.sortModified", "View", qsTr("Sort by Last Modified"), "", "s",
                 hasTab, () => w.sortBy(FileSortFilterModel.ByModified, true)),
            make("view.sortSize", "View", qsTr("Sort by Size"), "", "s",
                 hasTab, () => w.sortBy(FileSortFilterModel.BySize, true)),
            make("view.sortType", "View", qsTr("Sort by Type"), "", "s",
                 hasTab, () => w.sortBy(FileSortFilterModel.ByType, false)),
            make("view.zoomIn", "View", qsTr("Zoom In"), "Ctrl++", "",
                 hasTab && tab.zoom < tab.maximumZoom, () => tab.zoomIn()),
            make("view.zoomOut", "View", qsTr("Zoom Out"), "Ctrl+-", "",
                 hasTab && tab.zoom > tab.minimumZoom, () => tab.zoomOut()),
            make("view.zoomReset", "View", qsTr("Reset Zoom"), "Ctrl+0", "",
                 hasTab, () => tab.resetZoom()),
            make("view.columnsDialog", "View", qsTr("Visible Columns…"), "", "",
                 hasTab && w.viewMode === "list", () => w.openVisibleColumns()),
            make("view.reload", "View", qsTr("Reload"), "Ctrl+R", "",
                 hasTab, () => tab.reload()),
            make("view.quickView", "View", qsTr("Quick View"), "Space", "Space",
                 hasTab, () => w.runCommand("quickview")),
            make("view.info", "View", qsTr("Toggle Info Panel"), "F11", "i",
                 true, () => w.toggleInfoPanel()),
            make("view.sidebar", "View", qsTr("Toggle Sidebar"), "F9", "",
                 true, () => w.toggleSidebar()),

            // ---- panes, tabs and windows
            make("pane.split", "Pane", qsTr("Toggle Split View"), "F3", "",
                 w.currentSlot !== null, () => w.currentSlot.toggleSplit()),
            make("pane.other", "Pane", qsTr("Switch Pane"), "Ctrl+F6", "Tab",
                 w.splitOpen, () => w.currentSlot.cyclePane()),
            make("tab.new", "Tab", qsTr("New Tab"), "Ctrl+T", "t",
                 true, () => w.addTab(here)),
            make("tab.close", "Tab", qsTr("Close Tab"), "Ctrl+W", "q",
                 true, () => w.closeTab(w.currentTabIndex)),
            make("tab.next", "Tab", qsTr("Next Tab"), "Ctrl+Tab", "",
                 w.tabCount > 1, () => w.cycleTab(1)),
            make("tab.previous", "Tab", qsTr("Previous Tab"), "Ctrl+Shift+Tab", "",
                 w.tabCount > 1, () => w.cycleTab(-1)),
            make("window.new", "Window", qsTr("New Window"), "Ctrl+N", "",
                 true, () => App.openWindow(here)),
            make("window.close", "Window", qsTr("Close Window"), "Ctrl+Shift+W", "",
                 true, () => w.close()),

            // ---- application
            make("app.preferences", "Rook", qsTr("Preferences"), "Ctrl+,", "",
                 true, () => w.openPreferences()),
            make("app.keys", "Rook", qsTr("Keyboard Shortcuts"), "Ctrl+?", "?",
                 true, () => w.runCommand("help")),
            make("app.about", "Rook", qsTr("About Rook"), "", "",
                 true, () => w.openAbout())
        ];
    }
}
