import QtQuick
import QtQuick.Controls
import Rook.Runtime

// rook's menus: a theme-coloured card with a thin accent frame — the same
// language as an Omarchy/Hyprland window border — a soft shadow, and a quick
// fade-and-settle on open (closing is instant). Entries are OmMenuItem; submenu rows get one too.
Menu {
    id: menu

    // The icon this menu's row shows in its parent menu (a submenu).
    property string glyph: ""

    padding: 6
    // Wide enough for an icon, a label and its shortcut; grows for long names.
    implicitWidth: Math.max(248, contentItem ? contentItem.implicitWidth + leftPadding + rightPadding : 0)
    transformOrigin: Popup.TopLeft
    delegate: OmMenuItem {}

    background: OmCard {
        implicitWidth: 248
    }

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Colors.fadeInMs; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: Colors.popInScale; to: 1; duration: Colors.popInMs; easing.type: Easing.OutCubic }
        }
    }
    // No exit animation: a closing menu that lingers even 80 ms swallows
    // the next keystroke, and in a keyboard-first app that is felt.
}
