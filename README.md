# omanta

A native file manager for [Omarchy](https://omarchy.org), built with Qt Quick
and GIO as a drop-in replacement for GNOME Files (Nautilus) — same
keybindings, same launch semantics, same D-Bus integration, themed by your
Omarchy theme.

![omanta](docs/screenshot.png)

> **Testing preview.** omanta installs *alongside* your existing file manager
> and stays out of the way until you choose it. The only thing it adds by
> itself is a switch in the Omarchy Toggle menu. The switch flips between the
> two and restores the stock setup byte-identically. Please file issues for
> anything that doesn't behave exactly as you'd expect.

## What you get

- Keyboard-first, Omarchy-plugin style: `h` `j` `k` `l` to move, `g`/`G`
  for the ends, `/` to filter, `?` for every key (also linked from the
  status line), and Esc backing out one layer at a time (see [Keys](#keys)).
  Preferences → Keys switches back to Nautilus-style type-ahead.
- Four views (Ctrl+1–4, or the toolbar button to cycle):
  - **List** with configurable columns and optional tree expansion
  - **Grid**
  - **Columns**, Finder-style: one column per folder along the path,
    scrolling sideways as you go deeper, with a preview column at the end;
    `h`/`l` (←/→) go back up / into a folder
  - **Gallery**: the current item large on top, a thumbnail strip below that
    `h`/`l` step through
- Tabs, split view (F3) with F5/F6 to copy/move across, breadcrumbs + Ctrl+L.
  Going up a folder selects the one you came from
- Reopens your last windows, tabs and split panes on a plain launch
  (Preferences → Reopen Windows and Tabs at Launch)
- Adjustable icon sizes in both views: Ctrl++ / Ctrl+- to resize, Ctrl+0
  to reset, or use View Options → Icon Size. List and grid each keep their
  own size, shared by every window and remembered after a restart.
- Theme-coloured folders with two-tone panels and special-location symbols;
  small icons simplify their detail for clarity.
- All write operations — copy/cut/paste (system clipboard, interops with
  other file managers), move, rename, batch rename, trash, delete — with
  undo/redo and a progress popover
- Thumbnails (images, video, PDF) via the freedesktop spec, sharing the
  system-wide cache
- Places sidebar: devices with mount/unmount/eject, Network (`smb://`,
  `sftp://`) with credential prompts, Trash, Recent, Starred, bookmarks.
  F9 shows or hides it, and the choice is remembered. In a narrow window
  (tiled side by side on a laptop, say) it tucks itself away; F9 or the
  sidebar button slides it back over the files.
- Hidden files: Ctrl+H or Preferences → Show Hidden Files, remembered
  after a restart
- Quick view: Space (or right-click → Preview) previews the selected item
  inside the window — nothing to launch, so the next file is one keypress
  away. Images (zoom with +/-/0), text with line numbers, rendered
  Markdown (`m` for the source; images and HTML in it are never loaded),
  folder contents and size, archive listings, PDF pages, and audio/video
  with a seek bar. `j`/`k` (or the arrows) step through the folder,
  Enter opens, Space or Esc closes. `p` shrinks it to a picture-in-picture
  card in the corner: the files keep the keys and the card follows the
  selection (`P` or a double-click brings it back full size). Sushi is still available under
  Preferences → Space Previews With
- Info panel (F11, or `i`): preview, type, size (a folder's real total),
  dimensions, permissions, owner, and on-demand SHA-256/SHA-1/MD5 with a
  "compare with clipboard" check — the same checksum is in Properties
- Free space: a usage bar under Home and each drive in the sidebar
  (red past 90%), and "N GB free" on the status line
- Open With: right-click a file to open it in any app registered for its
  type, or "Other Application…" to choose one and set the default
- New Folder from the + button beside search, Ctrl+Shift+N, or right-click
- Search: recursive filename plus full-text (via `localsearch`), date and
  type filters. Names match as plain text, as a glob when the query has
  `*` `?` `[` (`*.jpg`, `IMG_????.*`), or as a regular expression after
  `re:` or with the `.*` toggle (Alt+R) — smart case: an upper-case letter
  in the pattern makes it case-sensitive
- Filter (`/`, or Ctrl+Shift+S): narrows the current folder as you type,
  same text/glob/regex rules; select by pattern with `*` or Ctrl+S
- Duplicate (Ctrl+Shift+D, or `Y`): "name (copy)" beside the original
- Drag and drop that tells you what it will do: a label beside the pointer
  reads "Move to …", "Copy to …" or "Link in …" and changes as you press
  Ctrl (copy), Shift (move) or Ctrl+Shift / Alt (link); with no key it moves
  within a drive and copies across drives
- Spring-loaded folders: rest a drag on a folder, a sidebar place or a tab
  and it opens, so you can keep digging (Preferences → Open Folders While
  Dragging: 3 s by default, 1.5 s, ¾ s or never)
- Compress/extract (zip, tar.xz, 7z, encrypted zip), "Extract to…"
- Omarchy theming end to end — follows your active theme live, not just
  light/dark
- Custom context-menu actions from simple TOML files — no extension API
  needed (Omarchy's transcode/LocalSend/Omarchy-Send menu items ship
  included, plus Dropbox share links if you use Dropbox)
- Multi-window single instance, `org.freedesktop.FileManager1` — "open
  containing folder" from browsers and other apps just works

Completed work from a failed or cancelled copy/move remains undoable when it
has not replaced existing files. These partial batches cannot be redone.
Extraction Undo preserves later additions, edits, and replacement files by
refusing to remove an output that has changed.

## Keys

Vim keys are on by default; `?` lists them all in the app. The short
version:

| Key | |
|---|---|
| `j` `k` / arrows | move |
| `h` `l` | parent / open (list view); left / right (grid) |
| `-` / Backspace, `~` | parent folder, home |
| `g` / `G` | first / last item |
| `J` `K` | extend the selection |
| `v` / `V` | toggle selection / select all |
| `/` | filter this folder — Enter keeps it, Esc clears it |
| `*` | select by pattern |
| `f` | search below this folder |
| Space | quick view (`p` inside it: picture-in-picture) |
| `i` | info panel |
| `y` `x` `p` | copy, cut, paste |
| `Y` | duplicate |
| `c` / `m` | copy / move to the other pane (split view) |
| `r` | rename |
| `a` | new folder |
| `D` | move to trash |
| `u` / `U` | undo / redo |
| `o` | actions menu |
| Ctrl+1 – 4 | list / grid / columns / gallery |
| `.` | hidden files |
| `s` | cycle sort: name › modified › size › type |
| `1`–`9` | jump to sidebar place N |
| `b` | browse the sidebar (`j`/`k`, Enter, Esc) |
| `t` / `q` | new tab / close tab |
| Tab | other pane |
| `?` | every key |

Every Ctrl/Alt/F-key shortcut works in both modes. Split view: F5 copies
and F6 moves the selection into the other pane; Ctrl+F6 switches panes.

## Install

Grab the package and its checksum file from the
[latest release](https://github.com/28allday/omanta/releases), check it, and
install it:

```bash
curl -LO https://github.com/28allday/omanta/releases/download/v0.1.23/omanta-0.1.23-1-x86_64.pkg.tar.zst
curl -LO https://github.com/28allday/omanta/releases/download/v0.1.23/SHA256SUMS
sha256sum -c --ignore-missing SHA256SUMS   # must print "OK"
sudo pacman -U omanta-0.1.23-1-x86_64.pkg.tar.zst
```

(The package is unsigned, so pacman won't install it straight from a URL —
download it first, check it against `SHA256SUMS`, and install the local
file. Don't install it if the check fails.)

Or build it yourself:

```bash
git clone https://github.com/28allday/omanta.git
cd omanta/packaging
makepkg -si
```

On ARM (aarch64 — Arch Linux ARM, Asahi, ARM laptops and VMs) there is no
prebuilt package yet, so build it this way. The code has nothing
architecture-specific, and `makepkg` needs only `base-devel` plus the
build tools it installs for you.

Installing changes none of your defaults — Nautilus (or whatever you use)
remains the file manager until you switch.

## Switching

The first time you open omanta, it adds an **Omanta File Manager** row to
the Omarchy Toggle menu (`SUPER+CTRL+O`). Select it to switch either way.
It shows a ✓ while omanta is the default, so the menu doubles as a status
check. If you remove the row, omanta won't add it back.

omanta's **Preferences → Default File Manager** has the same controls:
one switch makes omanta the default, the other shows or hides the
Toggle-menu row.

From a terminal:

```bash
omanta-switch omanta     # make omanta the default
omanta-switch nautilus   # back to stock
omanta-switch toggle     # flip
omanta-switch status     # what's active right now
```

`omanta-switch install-menu` and `omanta-switch remove-menu` add and remove
the Toggle-menu row. The row appears or disappears straight away, with no
shell restart.

Switching makes omanta (or Nautilus) the default everywhere at once:
`SUPER+SHIFT+F`, folders opened from other apps, and double-clicked
archives. It works by flipping the xdg-mime defaults and writing a
managed, clearly-marked block to `~/.config/hypr/bindings.lua` —
Omarchy's own files are never modified, no logout needed, and switching
back restores your configuration byte-for-byte. It also drops a user-level
D-Bus `.service` file for `org.freedesktop.FileManager1`, so "show in
folder" from a browser starts omanta (with `--service`, no extra window)
instead of activating Nautilus when neither is running. One note:
whichever file manager has windows open keeps that name until its last
window closes, so close the other one's windows after switching; omanta
takes the name over on its own once Nautilus exits.

## Uninstall

```bash
omanta-switch nautilus && omanta-switch remove-menu   # stock default, no menu row
sudo pacman -R omanta
```

## Requirements

Arch with Omarchy. Dependencies (`qt6-base`, `qt6-declarative`, `glib2`,
`gvfs`, `libarchive`, `tinysparql`, `qt6-multimedia`, `qt6-webengine`) are
all in Omarchy's default install or pulled automatically. The last two give
the quick view audio/video and PDF pages; build with
`-DOMANTA_WITH_MEDIA=OFF` / `-DOMANTA_WITH_PDF=OFF` to leave either out, and
those types fall back to an info card. Optional: `gvfs-smb`/`gvfs-mtp`/`gvfs-gphoto2` for
network shares, phones and cameras, `ffmpegthumbnailer` for video
thumbnails, `localsearch` for full-text search.

## Hacking on it

These scripts work from any directory:

```bash
./bin/build      # cmake + ninja into build/, then run ./build/omanta
./bin/test       # the headless suites (ctest, ~25s)
./bin/test-sanitizers # Clang ASan, UBSan and leak checks
./bin/install    # user-local install: ~/.local/bin symlink, desktop entry, icon
```

`./bin/install` never touches `/usr` or pacman, and installs alongside your
existing file manager — later rebuilds are picked up without reinstalling. For
a real package instead, use the `makepkg -si` route above.

Requires `cmake`, `ninja` and the Qt 6 development packages in addition to the
runtime dependencies above.

The Empty Trash integration test also needs `bubblewrap` and GVfs. It runs
with a separate filesystem, home directory and D-Bus session; it is skipped
when those dependencies are unavailable. The full-disk and permission tests
also require `bubblewrap`. See the [validation report](docs/validation-v0.1.13.md)
for the latest integration checks and their limits.

## License

MIT
