.pragma library

// The one key table, read by the `?` overlay (ShortcutsDialog). Tab.qml (vim keys) and Main.qml (Shortcuts) do the
// binding; when a binding moves, move its row here too.
//
// Vim keys follow the Omarchy plugins (flank, notification, loadout): hjkl
// to move, g/G for the ends, / to filter (Enter keeps it, Esc clears it),
// ? for every key, Esc backs out one layer at a time.

var vimGroups = [
    { name: "Move", rows: [
        ["j / k", "Down / up"],
        ["h / l", "Parent / open (list) — left / right (grid)"],
        ["- / Backspace", "Parent folder"],
        ["~", "Home folder"],
        ["g / G", "First / last item"],
        ["J / K", "Extend the selection down / up"],
        ["1 – 9", "Jump to sidebar place N"],
        ["b", "Focus the sidebar (j/k, Enter, Esc)"],
        ["Tab", "Other pane (split view)"]] },
    { name: "Select and find", rows: [
        ["v", "Toggle the current item"],
        ["V", "Select all"],
        ["*", "Select by pattern"],
        ["/", "Filter this folder (text, *.glob, re:regex)"],
        ["f", "Search below this folder"],
        ["Alt+R", "Regex on/off (filter and search fields)"],
        ["Esc", "Close preview › clear filter › clear selection"]] },
    { name: "Act", rows: [
        ["Enter", "Open"],
        ["Space", "Quick view (j/k step, Space/Esc close)"],
        ["p (in quick view) / P", "Picture-in-picture: preview in a corner, keys to the files"],
        ["i", "Info panel"],
        ["y / x / p", "Copy / cut / paste"],
        ["Y", "Duplicate"],
        ["c / m", "Copy / move to the other pane"],
        ["r", "Rename"],
        ["a", "New folder"],
        ["D", "Move to trash"],
        ["u / U", "Undo / redo"],
        ["o", "Actions menu"]] },
    { name: "Columns and gallery", rows: [
        ["h / l (columns)", "Back up a column / into the folder (a new column opens)"],
        ["h / l (gallery)", "Previous / next in the strip"],
        ["Ctrl+ + / -", "Row size (columns) / strip thumbnails (gallery)"]] },
    { name: "View", rows: [
        [".", "Show hidden files"],
        ["s", "Cycle sort: name › modified › size › type"],
        ["t / q", "New tab / close tab"],
        ["?", "This list"],
        [":", "Command palette"]] }
];

var classicGroups = [
    { name: "Windows and tabs", rows: [
        ["Ctrl+N", "New window"], ["Ctrl+Shift+W", "Close window"],
        ["Ctrl+T", "New tab"], ["Ctrl+W", "Close tab"],
        ["Ctrl+Tab / Ctrl+PgDn", "Next tab"], ["Ctrl+Shift+Tab / Ctrl+PgUp", "Previous tab"],
        ["F3", "Split view"], ["Ctrl+F6", "Switch pane"],
        ["F9", "Toggle sidebar"], ["F11", "Info panel"]] },
    { name: "Navigation", rows: [
        ["Alt+Left / Alt+Right", "Back / forward"], ["Alt+Up", "Parent folder"],
        ["Alt+Home", "Home folder"], ["Ctrl+L", "Edit the location"],
        ["Enter", "Open the selection"], ["Space", "Quick view"],
        ["Backspace", "Parent folder"]] },
    { name: "View", rows: [
        ["Ctrl+1 / Ctrl+2", "List / grid view"],
        ["Ctrl+3 / Ctrl+4", "Columns / gallery view"], ["Ctrl+H", "Show hidden files"],
        ["Ctrl++ / Ctrl+-", "Zoom in / out"], ["Ctrl+0", "Reset zoom"],
        ["Ctrl+R", "Reload"]] },
    { name: "Search and select", rows: [
        ["Ctrl+F", "Search the current folder"],
        ["Ctrl+Shift+F", "Search file contents"],
        ["Ctrl+Shift+S", "Filter this folder"],
        ["Ctrl+S", "Select by pattern"],
        ["Alt+R", "Regex on/off (in the field)"]] },
    { name: "Files", rows: [
        ["Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste"],
        ["Ctrl+Shift+D", "Duplicate"],
        ["F5 / F6", "Copy / move to the other pane (split view)"],
        ["Ctrl+Z", "Undo"], ["Ctrl+Shift+Z", "Redo"],
        ["Ctrl+A", "Select all"],
        ["F2", "Rename (batch rename on a multi-selection)"],
        ["Delete", "Move to trash"], ["Shift+Delete", "Delete permanently"],
        ["Ctrl+Shift+N", "New folder"], ["Ctrl+D", "Bookmark this folder"],
        ["Ctrl+I / Alt+Return", "Properties"]] },
    { name: "While dragging files", rows: [
        ["Ctrl", "Copy"],
        ["Shift", "Move"],
        ["Ctrl+Shift / Alt", "Create a link"],
        ["Rest on a folder", "It opens (spring-loaded; Preferences sets the delay)"]] },
    { name: "Command palette", rows: [
        ["Ctrl+Shift+P / F1", "Open the palette"],
        ["/ ~ @ ? =", "Path, places, filter, search, calc (type first, or Tab)"],
        ["Tab (in / mode)", "Complete the highlighted folder"],
        ["↑ ↓ / Ctrl+J Ctrl+K", "Move"], ["Enter / Esc", "Run / close"]] },
    { name: "Application", rows: [
        ["Ctrl+,", "Preferences"], ["Ctrl+?", "Keyboard shortcuts"]] }
];

// The overlay: vim keys first when they are on, then everything that works
// in both modes.
function groups(mode) {
    return mode === "vim" ? vimGroups.concat(classicGroups) : classicGroups;
}
