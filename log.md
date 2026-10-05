# Change log — fork of omanta v0.1.23

Everything changed in this fork since upstream `master` at `20fbae8` (v0.1.23,
"Open With submenu, New Folder button, disabled menu items greyed out").

| Branch | Commit | Summary |
|---|---|---|
| `features/keyboard-quickview-search` | `25e0911` | Features, keyboard, search, quick view, views, drag and drop |
| `polish/ui` | `11ce9de` | Themed menus, dialogs, popovers, drop-downs and animation |

The total is 80 files changed, with about 6,900 lines added and 370 removed.

---

## 1. Security review (fixed)

A review of the code produced `todo.md`, which lists seven findings in order of severity. All
seven are fixed; `todo.md` records what was done for each and which tests cover it:

1. **Thumbnailers run inside bubblewrap:** no network, no session environment, the system
   read-only, only the input readable and only a private output directory writable. If bwrap is
   missing or can't create a sandbox, there's a one-time warning and they run unsandboxed.
2. **Only PNG/JPEG/GIF/BMP are decoded in-process.** Other image formats go to a sandboxed
   thumbnailer whenever one exists. Absurd header sizes are refused, and thumbnailer output is
   read strictly as size-capped PNG.
3. **The thumbnail cache is private and written atomically:** 0700 directories, 0600 files, written
   with `QSaveFile`. Looser directories left by earlier runs are tightened.
4. **Encrypted ZIP is AES-256.** ZipCrypto stays available as "Encrypted ZIP, legacy". libarchive
   can't write encrypted 7z, so that isn't offered.
5. **Extraction honours the umask and never keeps setuid/setgid/sticky or group/other write.** An
   archive that expands past max(50× its size, 1 GiB) stops and asks "Extract Anyway?" for that one
   archive. Running out of free space fails without asking.
6. **The PKGBUILD pins the release commit with a real checksum.** The README checks the package
   against `SHA256SUMS` before installing it.
7. **`omanta-switch` writes through symlinked `bindings.lua` / `omarchy-menu.jsonc`**, keeping the
   link and the file's mode.

The review also checked parts of the code that turned out not to be problems: D-Bus callers,
action TOML files, the SPARQL search, and how copies and moves are published.

## 2. Keyboard-first mode

- **Vim keys are on by default.** Preferences → Keys switches back to classic Nautilus-style
  type-ahead.
- The keys follow the Omarchy plugins (flank, notification, loadout):

  | Key | Action |
  |---|---|
  | `h` `j` `k` `l` / arrows | move |
  | `g` / `G` | first / last item |
  | `-` / Backspace | parent folder |
  | `~` | home |
  | `J` / `K` | extend the selection |
  | `v` / `V` | toggle the current item / select all |
  | `/` | filter |
  | `*` | select by pattern |
  | `f` | search |
  | Space | quick view |
  | `i` | info panel |
  | `y` / `x` / `p` | copy / cut / paste |
  | `Y` | duplicate |
  | `c` / `m` | copy / move to the other pane |
  | `r` | rename |
  | `a` | new folder |
  | `D` | move to trash |
  | `u` / `U` | undo / redo |
  | `o` | actions menu |
  | `.` | hidden files |
  | `s` | cycle the sort |
  | `1`–`9` | jump to sidebar place N |
  | `b` | browse the sidebar |
  | `t` / `q` | new tab / close tab |
  | Tab | other pane |
  | `?` | every key |

- **Esc backs out one layer at a time:** it closes the preview, then clears the filter, then
  clears the selection, then closes the search.
- **Key table and all-keys panel:** every key is listed in one table, `qml/Keymap.js`. The all-keys
  panel (`?` or Ctrl+?) reads that table, can be searched, and has an accent border. The status line
  ends with "[?] all keys". (A hint bar along the bottom was tried and then removed.)
- **Focus fix:** opening, switching or closing a tab now gives the keyboard to the tab on screen.
  Before, focus was left on a hidden tab.

## 3. Search and filter

- **Pattern search:** the existing search now matches names as plain text, as a glob (`*.jpg`), or
  as a regular expression. A regex is chosen with a `re:` prefix or the `.*` toggle (Alt+R). Regexes
  are smart-case: an upper-case letter in the pattern makes the match case-sensitive. An invalid
  pattern is shown in red.
- **One matching rule:** the rule lives in `FileSortFilterModel::namePattern`, and both search and
  filter use it. A separate helper class was written first and then folded in, so the logic isn't
  duplicated.
- **`/` filter:** filters the current folder as you type, using the same text, glob and regex
  rules. Enter keeps the filter and Esc clears it.
- **Select by pattern:** `*` or Ctrl+S.

## 4. Built-in quick view (replaces Sushi)

- **Built-in:** Space previews inside the window, so nothing has to launch. `j`/`k` step through
  the folder, +/-/0 zoom, Enter opens, and Space or Esc closes.
- **Supported kinds:** images (including animated ones), text with line numbers, Markdown (with
  images and raw HTML stripped, so nothing is fetched from the web), folder listings with totals,
  archive listings (headers only, nothing extracted), PDF (Qt Pdf) and audio/video (Qt Multimedia).
- **Picture-in-picture:** `p` shrinks the preview to a card in the corner. The file list keeps the
  keys and the card follows the selection.
- **Optional at build time:** PDF and media support are optional, controlled by
  `-DOMANTA_WITH_PDF` / `-DOMANTA_WITH_MEDIA`. Sushi is still available as a preference.
- **Code layout:** `QuickViewInfo` (C++) does the reading on a worker thread. `PreviewPane.qml` holds
  the renderers and is shared by the quick view, the Columns preview column and the Gallery.

## 5. Features

1. **Session restore:** windows, tabs and split panes come back on a plain launch. Folders that no
   longer exist are skipped.
2. **Free space:** a usage bar under Home and each device in the sidebar, red above 90%, and "X free"
   on the status line.
3. **Split view:** F5 / F6 copy / move the selection into the other pane. Pane switching moved to
   Ctrl+F6.
4. **Checksums:** SHA-256, SHA-1 and MD5 with progress and cancel, plus "compare with clipboard". They
   appear in the info panel and in Properties.
5. **Filter and select by pattern**, covered in section 3.
6. **Folder totals:** the quick view and info panel show folder totals using the size calculation
   Properties already had (the new counter that duplicated it was removed).
7. **Duplicate:** Ctrl+Shift+D or `Y` makes "name (copy)" beside the original.
8. **Info panel:** F11 or `i`. It shows a preview, type, size, dimensions, permissions, owner and a
   checksum.

## 6. Views

- **Columns (Ctrl+3):** in the style of Finder. There's one column per folder along the path, it
  scrolls sideways as you go deeper, and a preview column sits at the end.
- **Gallery (Ctrl+4):** a large preview of the current item with a thumbnail strip underneath.
- **Switching views:** the toolbar button cycles List › Grid › Columns › Gallery, and the View options
  menu lists all four.
- **Going up a folder** now selects the folder you came from, in every view.
- **Long names** are shortened in the middle, keeping the start and the end, in the style of Finder.
  Two-line grid labels use a new helper, `Platform.elideMiddle`.

## 7. Drag and drop

- **Live drag card:** the card reads `[▣ 5 items | Move]`. The right-hand part follows Ctrl (copy),
  Shift (move) and Ctrl+Shift / Alt (link), and turns red for Trash. omanta draws the card in its own
  windows; the native drag picture is blank because Qt can't change it once a drag has started. Drags
  from other apps get a badge instead.
- **Drop as a link:** dropping can now create links.
- **Spring-loaded folders:** resting a drag on a folder, sidebar place or tab opens it. The delay is in
  Preferences (3 s by default).
- **Crash fix:** drags now run from `DragSource`, an object that lasts as long as the app. Before, a
  row destroyed mid-drag (a spring-loaded folder opening) freed the drag's data while the drag was
  still running, which caused two segfaults.
- **Safe refusals:** drops on places that can't take files are refused, so an app you dragged from
  never thinks its files were moved.

## 8. Visual polish (`polish/ui`)

- **Menus (`OmMenu` / `OmMenuItem`):**
  - About 30 new flat icons in the theme's colours.
  - Hovered or focused rows get an accent pill with an accent bar on the left; destructive rows get
    a red one.
  - Each row shows its shortcut in the current keyboard mode.
  - Menus fade and settle in on open. Duplicate and Copy/Move to Other Pane were added to the
    right-click menu.
- **Dialogs (`OmDialog`):**
  - All dialogs share a theme card with an accent frame and a soft shadow.
  - Titles and buttons are themed (`OmButton`): the main action is filled with the accent colour, and
    destructive buttons are red.
  - The background behind an open dialog is dimmed.
  - Properties' tabs (`OmTabButton`) are plain text with an accent underline.
- **Popovers and drop-downs:** popovers use `OmPopup`. Drop-down lists use `OmComboBox`, whose rows
  look like menu rows.
- **Quick view motion:** the quick view fades and settles in on open and glides into and out of
  picture-in-picture.
- **One set of timings:** all motion uses shared timings in `Colors.qml`. Closing is always instant so
  the next key press isn't swallowed.
- **Other visual fixes:**
  - The status line no longer shortens "4 items" to "4 …ms".
  - The Trash drag action is red.
  - The preview has an accent border.

## 9. Maintenance

- Replaced Qt 6.10's deprecated `invalidateFilter` / `invalidateRowsFilter` calls with the new
  `beginFilterChange` / `endFilterChange` API.
- PKGBUILD: `qt6-multimedia` and `qt6-webengine` are now dependencies, and the Sushi optdepend is
  described differently.
- README: rewrote the feature list and added a Keys table.

## 10. Tests

- **New suite:** `tst_quickview` covers preview kinds, size caps, Markdown safety, archive listing
  and checksums.
- **Extended suites:** search (regex, glob, invalid patterns), sort/filter (pattern rules, select by
  pattern), settings (new keys, session state), places (free space), file operations (duplicate).
- **New integration tests** that drive the real window:
  - vim keys, the filter and the quick view (including PiP);
  - PDF and media previews;
  - session restore;
  - split-pane transfers;
  - focus after opening a tab;
  - shortcuts search;
  - Columns and Gallery views;
  - drag label and spring-loading;
  - a real drag gesture.
- **Screenshot hooks:** `OMANTA_TEST_MENU_SCREENSHOT` and `OMANTA_TEST_POLISH_SHOTS`.
- **Security fixes:** `tst_thumbnails` (sandbox, decode policy, cache permissions), `tst_archives`
  (AES/legacy encryption, extraction modes, expansion and free-space guards, the confirmation
  flow) and `tst_switcher` (symlinked configs).
- **Flaky tests made stable:** status checks now wait for loading to finish. `tst_thumbnails` still
  fails now and then when the suites run in parallel; that problem predates this fork.

## Files added

- **QML:** `ChecksumBox`, `ColumnsView`, `DragState`, `FileDropArea`, `FilterBar`, `GalleryView`,
  `InfoPanel`, `Keymap.js`, `OmButton`, `OmButtonBox`, `OmCard`, `OmComboBox`, `OmDialog`, `OmMenu`,
  `OmMenuItem`, `OmMenuSeparator`, `OmPopup`, `OmTabButton`, `PreviewPane`, `QuickView`,
  `QuickViewMedia`, `QuickViewPdf`.
- **C++:** `Checksum`, `DragSource`, `QuickViewInfo`.
- **Tests:** `tst_quickview.cpp`.
- **Docs:** `todo.md` (security), `log.md` (this file).
