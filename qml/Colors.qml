pragma Singleton

import QtQuick
import Rook.Runtime

// One place for every colour. When the active Omarchy theme's colors.toml is
// present, all roles come from it — rook then matches the terminal, the
// bar and every other themed surface, and follows `omarchy theme set` live.
// Without it (non-Omarchy system), the built-in palette below tracks the
// portal's light/dark setting instead.
QtObject {
    readonly property bool dark: Theme.darkMode
    readonly property bool themed: Theme.hasThemeColors

    // The two big surfaces carry the background-opacity preference, the way a
    // terminal's background_opacity works: the backdrop goes translucent while
    // text, icons and controls stay fully opaque.
    readonly property real surfaceAlpha: Settings.backgroundOpacity
    readonly property color window: Qt.alpha(themed ? Theme.windowColor : (dark ? "#101010" : "#fbfbfb"), surfaceAlpha)
    readonly property color chrome: Qt.alpha(themed ? Theme.chromeColor : (dark ? "#161616" : "#f0f0f0"), surfaceAlpha)
    readonly property color border: themed ? Theme.borderColor : (dark ? "#2a2a2a" : "#dcdcdc")

    readonly property color text: themed ? Theme.textColor : (dark ? "#eeeeee" : "#1c1c1c")
    readonly property color textDim: themed ? Theme.textDimColor : (dark ? "#8a8a8a" : "#6b6b6b")

    // The one accent, and it means something: focus, selection and the
    // active mode. Nothing decorative wears it — folders, links and frames
    // use the neutral roles below.
    readonly property color accent: themed ? Theme.accentColor : (dark ? "#7aa2f7" : "#3457d5")
    // Selection is the accent laid over the window, so the two never drift
    // apart; the normal text reads fine on a tint this light.
    readonly property color selection: Qt.tint(themed ? Theme.windowColor : (dark ? "#101010" : "#fbfbfb"),
                                               Qt.alpha(accent, dark ? 0.32 : 0.22))
    readonly property color selectionText: text
    // Folder glyphs: a step brighter than file glyphs, never the accent.
    readonly property color folder: Qt.tint(textDim, Qt.alpha(text, 0.45))
    readonly property color hover: themed ? Theme.hoverColor : (dark ? "#1e1e1e" : "#eaeaea")

    readonly property color error: themed ? Theme.errorColor : "#f7768e"

    readonly property int radius: 6
    // One motion for every floating surface (menus, dialogs, popovers,
    // drop-downs, the quick view): a quick fade with a slight settle in.
    // Closing is always instant — a lingering close swallows the next key.
    readonly property int fadeInMs: 110
    readonly property int popInMs: 140
    readonly property real popInScale: 0.96
    // Compact by default: rows, bars and hit targets sized for a dense
    // tiling desktop rather than a touch-friendly GNOME window.
    readonly property int rowHeight: 22
    readonly property int barHeight: 26
    // Sizes, counts, dates and keys — anything that lines up in columns.
    readonly property string mono: "monospace"

    // Ask the icon provider for the flat theme-coloured glyph instead of the
    // GTK theme icon. The colour rides the URL (minus its '#', which a URL
    // would read as a fragment), so theme switches and selection changes
    // re-render through ordinary bindings.
    function tint(iconSource, color) {
        return iconSource + "?c=" + String(color).substring(1);
    }

    // File-view folders have a front panel and optional location emblem;
    // navigation/chrome continue to use the compact monochrome symbols.
    function fileIcon(iconSource, color, iconSize) {
        // The provider receives physical pixels. Choose detail from the
        // logical size so a 16px icon stays simple on a high-DPI screen too.
        return tint(iconSource, color) + "&style=content&detail="
            + (iconSize >= 24 ? "full" : "simple");
    }
}
