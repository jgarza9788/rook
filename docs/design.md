# Rook — Design Document

Oct 4, 2026 · @Justin Garza

## Overview & goals

Rook is a native C++/QML file manager for Wayland desktops where the command palette is the primary way to do everything. It combines the core features of the best file managers (Nautilus, Dolphin, Finder, Total Commander, Directory Opus, Yazi/ranger) with pro tools like bulk rename, regex, folder compare, and scripted actions.

**Principles**

- **Palette first:** every action is a registered command; menus, toolbars, and shortcuts are views of the same registry.
- **Keyboard complete:** no feature requires a mouse; mouse stays first-class.
- **Preview before commit:** every destructive or bulk operation shows a dry-run diff and is undoable.
- **Never block the UI:** all I/O runs off the GUI thread; the UI stays at 60+ fps on 100k-entry folders.
- **Scriptable:** commands are callable from CLI, D-Bus, and user scripts.

**Non-goals (v1)**

- Windows/macOS (target is any Linux Wayland desktop: Omarchy/Hyprland, niri, Sway, KDE, GNOME; X11 best-effort)
- Cloud-sync client (mount via GVfs/rclone instead)
- Built-in text editor or media player beyond preview

## Baseline: omanta

Rook (working name during early design: *omaFiles*) forks [omanta](https://github.com/jgarza9788/omanta) (`polish/ui` branch, base commit `b0eba82`), a Qt Quick + GIO Nautilus replacement for Omarchy, MIT-licensed. Keep its proven I/O core; replace its window chrome and interaction model.

| Area | omanta today | Rook plan |
| --- | --- | --- |
| Stack | Qt 6 Quick, GIO/GVfs, libarchive, tinysparql, CMake + Ninja | Keep; add KSyntaxHighlighting, PCRE2/QRegularExpression, optional KIO bridge later |
| Views | List, grid, tree expansion, tabs, split view (F3) | Keep; add Miller columns, dual-pane commander mode, gallery, details+preview |
| File ops | Copy/move/rename/batch rename/trash/delete, undo/redo, progress popover | Keep engine; replace batch rename with Rename Studio; add job queue UI |
| Sidebar | Devices, smb/sftp, Trash, Recent, Starred, bookmarks | Keep; add tags, saved searches, workspaces |
| Search | Recursive name + full-text via localsearch | Keep; add palette-native filters, regex, content grep (ripgrep-style) |
| Preview | Sushi (external) | Replace with in-app preview pane; Sushi optional |
| Extensibility | TOML context-menu actions | Extend TOML to full command definitions + scripting |
| Integration | org.freedesktop.FileManager1, omanta-switch, Omarchy theming | Keep FileManager1; rebrand to rook-switch, Omarchy toggle optional; theming via Omarchy or desktop fallback |
| Tests | Headless ctest suites, sanitizers, bubblewrap integration | Keep; add palette and rename-engine unit tests |

**Rename/rebrand tasks:** binary, app ID, D-Bus service files, config paths (`~/.config/rook/`), desktop entry, icons, switch script.

**Upstream sync**

Rook lives at [jgarza9788/rook](https://github.com/jgarza9788/rook) with full omanta history, and tracks omanta as a second remote:

| Remote | Repo | Notes |
| --- | --- | --- |
| `origin` | jgarza9788/rook | Rook trunk is `main` |
| `upstream` | jgarza9788/omanta | Push disabled; sync from `master` (polish/ui was merged into it at `23e0163`) |

```sh
git fetch upstream
git log --oneline main..upstream/master      # review what's new
git merge upstream/master                    # take everything
git cherry-pick <sha>                        # or take selected fixes
```

- Do rebrand renames in dedicated commits using `git mv` so rename detection keeps upstream merges clean.
- Keep the I/O core (`rook-io`) layout close to omanta's for as long as practical; it is the part most worth syncing.

## Architecture

&#91;embedded content: architecture · 4 layers\]

QML is a thin view layer; all logic lives in C++ and is exposed as commands and item models. The palette, menus, shortcuts, CLI, and D-Bus all call the same registry.

**Modules (CMake targets)**

| Target | Responsibility |
| --- | --- |
| `rook-core` | Command registry, undo journal, settings, state DB (SQLite) |
| `rook-io` | omanta's GIO file-op engine, job queue, watchers, archives |
| `rook-rename` | Rule pipeline, token resolver, conflict/cycle planner (pure C++, unit-testable) |
| `rook-search` | Fuzzy matcher, filter parser, content grep, localsearch bridge |
| `rook-preview` | Thumbnailers, metadata extractors, syntax highlighting |
| `rook-ui` | QML modules, view models, theme singleton |
| `rook-plugins` | TOML command loader, QJSEngine script host |

**Threading**

- GUI thread: QML + models only.
- Job queue: dedicated worker pool for copy/move/delete; per-job cancel tokens; progress via queued signals.
- Listing, thumbnails, metadata, hashing: QThreadPool tasks, results batched to models every \~16 ms.
- Rename engine runs plan/preview synchronously for <1k files, worker thread above that.

## Command palette

One palette (Ctrl+Shift+P / `:`) runs every command, navigates anywhere, and filters the current view. Prefix characters switch mode so one box covers what other apps split across dialogs.

| Prefix | Mode | Example |
| --- | --- | --- |
| (none) | Commands + recent locations, ranked | `bulk ren` → Rename Studio |
| `/` | Go to path, with completion | `/etc/nginx` |
| `~` | Bookmarks, frecent dirs (zoxide-style) | `~proj` |
| `@` | Filter current view (glob, regex, size, date) | `@ re:^IMG_\d+ size>2M` |
| `?` | Search recursive / content | `? TODO in:*.cpp` |
| `#` | Tags | `#invoices` |
| `>` | Shell command on selection | `> ffmpeg -i {} {.}.mp4` |
| `!` | Run user script / macro | `!resize-web` |
| `=` | Quick calc / size math | `= 4.7G / 3` |

**Command registry (C++)**

- `Command { id, title, category, icon, keybinding, args schema, when-clause, run() }` registered at startup by core modules and plugins.
- When-clauses gate availability by context (`selection.count > 1`, `view.isRemote`, `focus == pane`).
- Ranking: fuzzy score (fzf-style) × frecency × context relevance; results stream into a `QAbstractListModel`.
- Commands with args open inline argument steps (multi-step palette), e.g. Compress → format → name → level.
- Every executed command is logged to history for repeat (`.`) and macro recording.

**Keybindings**

- Default preset: Nautilus-compatible (inherited from omanta). Optional presets: Vim (hjkl, `yy`/`dd`/`pp`, marks), Commander (F5–F8), Finder.
- All bindings user-remappable in `keybindings.toml`; chords and leader keys supported.
- Which-key overlay after leader key shows available next keys.

## Core feature set

The table lists the baseline features Rook must match, with the file manager each is borrowed from.

| Category | Features | Inspired by |
| --- | --- | --- |
| Views | List, grid, Miller columns, tree, gallery, compact; per-folder view memory | Finder, Dolphin |
| Panes | Tabs, split/dual pane, up to 4 panes, sync-browse, swap panes | Total Commander, Directory Opus |
| Navigation | Breadcrumbs (editable), back/forward history per tab, frecent jump, type-ahead, marks | Yazi, ranger, Nautilus |
| Selection | Select by pattern/regex, invert, select same type/date, persistent selection across folders | Directory Opus |
| File ops | Copy/move/link/trash/delete, queued jobs with pause/resume/reorder, conflict resolver (skip, rename, overwrite-if-newer, compare) | Dolphin, Total Commander |
| Undo | Full undo/redo history panel for all ops incl. rename and extract | omanta |
| Preview | Side preview pane: images, video, audio, PDF, Markdown, code w/ syntax highlight, archives, fonts, hex | Yazi, Finder Quick Look |
| Metadata | Columns for EXIF, ID3, video duration, dimensions, git status, checksums | Directory Opus |
| Search | Instant filter, recursive name, content search, saved searches as virtual folders | Finder Smart Folders |
| Tags | Color + text tags (xattr `user.xdg.tags`), tag sidebar | Finder, Dolphin |
| Places | Devices, mount/eject, smb/sftp/MTP via GVfs, bookmarks, Recent, Trash | Nautilus |
| Archives | Browse archives as folders, extract, compress (zip/7z/tar.\*/encrypted) | Dolphin, 7-Zip |
| Terminal | Embedded terminal pane following current dir; "open terminal here" | Dolphin |
| Integration | Open With, default apps, FileManager1 D-Bus, drag-drop, system clipboard | omanta |

## Pro features

Rename Studio is the flagship pro feature; the rest close the gap with Directory Opus and Total Commander.

**Rename Studio (bulk rename)**

- Live preview table: old name → new name, conflicts and invalid chars flagged red, unchanged rows dimmed.
- Stackable rule pipeline, reorderable, each toggleable:
  - Find/replace (plain, case-insensitive, regex with capture groups `$1`, named groups)
  - Insert/remove at position, trim, change case (lower/UPPER/Title/camel/snake/kebab)
  - Counter/sequence (start, step, padding, reset per folder)
  - Metadata tokens: `{exif.date:yyyy-MM-dd}`, `{id3.artist}`, `{mtime}`, `{parent}`, `{hash:8}`
  - Extension rules, transliterate/strip diacritics
  - Edit-as-text mode: open names in a buffer, edit freely, apply (vidir/Dolphin style)
- Save rule stacks as presets; run presets from the palette (`!rename-photos`).
- Apply is atomic per batch: two-phase rename handles swaps and cycles; one undo step.

**Regex everywhere**

- Selection, filter, search, rename, and content grep accept `re:` patterns (PCRE2 via QRegularExpression).
- Inline regex tester shows matches highlighted against current file names.

**More pro tools**

- **Folder compare/sync:** diff two panes by name/size/date/hash; one- or two-way sync with preview.
- **Duplicate finder:** size → partial hash → full hash (xxHash/BLAKE3) pipeline.
- **Disk usage:** treemap/sunburst of current folder, cached scans.
- **Checksums:** compute/verify MD5/SHA-256/BLAKE3; `.sha256` sidecar support.
- **Batch convert:** user-defined command templates (image resize, ffmpeg transcode) run as queued jobs.
- **Macros:** record palette actions into a replayable script.
- **Scripting:** command API exposed to QJSEngine (JS) and external CLI (`rook cmd <id> --args`).
- **Permissions/ownership editor:** chmod/chown with recursive preview; elevated ops via polkit.
- **Secure delete** and **file splitting/joining**.
- **Git awareness:** status column and badges in repos.

## UI/UX layout & theming

The window is minimal chrome around panes: no menubar, a thin header, and the palette as an overlay centered at the top.

**Layout zones**

1. **Header:** breadcrumb/path bar, tab strip, palette trigger, job indicator.
2. **Sidebar (F9):** places, tags, saved searches, workspaces; auto-hides in narrow windows.
3. **Panes:** 1–4 file panes, each with its own view mode and history.
4. **Preview pane:** toggleable right panel; follows focused selection.
5. **Bottom drawer:** jobs queue, terminal, undo history, Rename Studio preview (dockable).
6. **Status line:** selection count/size, free space, filter/mode indicator (Vim-style mode badge).

**Interaction**

- Palette overlay animates in under 100 ms; Esc closes; results update per keystroke.
- Context menus show the same commands as the palette, with their keybinding hints.
- Toasts for completed jobs with inline Undo.
- Workspaces save pane layout + tabs + paths; switch from palette.

**Theming**

- Theme sources in priority order: Omarchy live theme (kept from omanta) when present, else xdg-desktop-portal color scheme + Qt palette (KDE/GNOME/other compositors).
- Theme tokens in QML singleton (`Theme.qml`): colors, radii, spacing, fonts; user overrides in `theme.toml`.
- Density modes: compact / comfortable; icon sizes per view (kept from omanta).

## Data, config, performance, security

Config is plain TOML in `~/.config/rook/`; state and caches live in XDG state/cache dirs.

| File / store | Purpose |
| --- | --- |
| `config.toml` | General settings, view defaults, behavior |
| `keybindings.toml` | Keymap preset + overrides |
| `commands/*.toml` | User commands, context actions (extends omanta TOML actions) |
| `rename-presets/*.toml` | Rename Studio rule stacks |
| `theme.toml` | Theme token overrides |
| `state.sqlite` (XDG state) | Frecency, history, workspaces, per-folder view memory, tags index |
| XDG thumbnail cache | Shared freedesktop thumbnails (kept from omanta) |

**Performance targets**

- Open 100k-entry folder: first rows in under 150 ms, full listing streamed in batches.
- Palette results in under 16 ms per keystroke for 5k commands + 10k recent paths.
- Directory watching via GFileMonitor/inotify; debounced model updates.
- Thumbnails and metadata loaded lazily for visible rows only, thread pool sized to cores.

**Security & safety**

- Shell commands (`>` mode, user scripts) show the resolved command before first run and require confirmation for destructive templates.
- Elevated operations only via polkit helper; never run the UI as root.
- Secure delete warns on SSD/CoW filesystems where overwrite is not guaranteed.
- Encrypted archive passwords kept in memory only, optional libsecret storage.

## Roadmap, risks & open questions

&#91;embedded content: roadmap · 5 phases, not to scale\]

Phase 1 is the accent because every later feature registers as a command; building features first means retrofitting them into the palette.

**Risks**

| Risk | Mitigation |
| --- | --- |
| Palette becomes a junk drawer as commands grow | Categories, when-clauses, and context ranking; cap visible results |
| QML list performance on huge folders | C++ models with batched inserts; profile with QML profiler early |
| Rename Studio data loss on partial failure | Two-phase rename with journal; resume or roll back on crash |
| Drift from upstream omanta fixes | Keep I/O core in separate module; fetch `upstream` regularly and merge/cherry-pick (see Upstream sync) |
| Scope creep from "all features" goal | Phase gates: each phase ships before the next starts |

**Open questions**

- [x] What does the polish UI branch change vs master, and which parts carry over? (Moot: polish/ui was merged into omanta master; see `log.md` for the change list.)
- [ ] Packaging beyond Arch: AUR only, or also Flatpak and distro packages? (Decided: target any Wayland desktop.)
- [ ] Scripting language: QJSEngine (JS) only, or also Lua/Python?
- [ ] Keep Sushi preview as fallback or drop it?
- [ ] License for pro features: stay MIT, or separate paid tier?
