# Security TODO

Findings from the October 2026 review, most severe first. **All seven are fixed**
(October 2026, branch `polish/ui`); each entry ends with what was done and the
tests that cover it.

## 1. Thumbnailers run unsandboxed — Medium-High
`src/ThumbnailProvider.cpp:320` (`ThumbnailCache::renderViaThumbnailer`) runs every
`*.thumbnailer` Exec directly with `QProcess`. ffmpegthumbnailer, PDF and office
thumbnailers parse any file that shows up in a viewed folder (e.g. a fresh download)
with no click. Nautilus (gnome-desktop) wraps the same thumbnailers in bubblewrap +
seccomp with no network.

**Fix:** when `bwrap` is available, run under `bwrap --unshare-all --die-with-parent
--clearenv`, ro-binding `/usr`, `/etc/ld.so.cache`, `/etc/fonts` and the input file,
with only the output temp dir writable. Fall back with a one-time warning when bwrap
is missing.

**Done:** `ThumbnailCache::sandboxedCommand` / `runThumbnailer`. Also `--new-session`,
`--proc`, `--dev`, a tmpfs `/tmp`, and a clean `PATH`/`HOME`. bwrap is probed once per
session (user namespaces can be disabled even when it is installed). A thumbnailer
outside `/usr` gets just its own binary bound read-only. Tests: `tst_thumbnails`
`sandboxCommandConfinesTheThumbnailer`, `thumbnailerRunsInTheSandbox` (no env, no network,
no home), `rendersRealFilesThroughTheSandbox`, `userInstalledThumbnailersStillRun`.

## 2. In-process image decoding without limits — Medium
Image thumbnails are decoded with `QImage` inside the omanta process. A bug in a Qt
image plugin (tiff/heic/webp) compromises the file manager itself.

**Fix:** decode through `QImageReader` with `setAllocationLimit`, check the reported
size first; optionally route exotic formats through the sandboxed path from #1.

**Done:** only PNG/JPEG/GIF/BMP are decoded in-process; everything else goes to a
sandboxed thumbnailer whenever one handles it, with no in-process fallback. Sources
over 16384² are refused from the header. Thumbnailer output is read strictly as PNG,
capped at 4096² and 128MB. Tests: `onlyCoreFormatsDecodeInProcess`,
`refusesAbsurdDimensionsFromTheHeader`, `rejectsHostileThumbnailerOutput`.

## 3. Thumbnail cache permissions and non-atomic writes — Low-Medium
`src/ThumbnailProvider.cpp:118-155` (`store`, `markFailed`) use `QDir().mkpath` and
`QImage::save`, so the umask decides the modes (0755 / 0644). The freedesktop
thumbnail spec requires 0700 directories, 0600 files, and write-to-temp-then-rename.
Thumbnails otherwise leak private image content to other local users.

**Fix:** `QSaveFile` + `setPermissions(ReadOwner | WriteOwner)` (pattern in
`src/ServerStore.cpp`), create directories 0700.

**Done:** as above. Existing directories with group/other bits are tightened to 0700.
Tests: `cacheFilesArePrivate`, `cacheDirectoriesAreTightened`,
`cacheWritesLeaveNoTemporaries`.

## 4. Archive encryption is ZipCrypto only — Low-Medium
`src/ArchiveEngine.cpp:288` sets `zip:encryption=zipcrypt`, which is broken by
known-plaintext attacks.

**Fix:** offer AES-256 zip (`zip:encryption=aes256`) and encrypted 7z in
`qml/CompressDialog.qml`, default to AES, keep ZipCrypto as "Legacy (Windows
Explorer compatible)".

**Done:** "Encrypted ZIP" is AES-256; "Encrypted ZIP, legacy" is ZipCrypto
(`ArchiveEngine::ZipEncryption`, `FileOperations::compress(..., legacyEncryption)`).
Encrypted 7z is **not** offered: libarchive can't write it. Tests:
`encryptedZipIsAesByDefault`, `legacyZipCryptoIsAnExplicitChoice` (Info-ZIP `unzip`
reads it), `wrongAesPasswordAsksAgain`, `operationsChooseTheEncryption`, and
`bsdtarCanReadOurEncryptedZip` for both.

## 5. Extraction keeps archive permissions; no bomb guard — Low
`src/ArchiveEngine.cpp:388` uses `ARCHIVE_EXTRACT_PERM`, so stored modes bypass the
umask (0777 world-writable files, setgid directories). Nothing limits the expanded
size (especially the raw `.gz` path).

**Fix:** strip group/other write and setuid/setgid after extraction (or apply a
masked mode); add a cancellable guard when expansion exceeds ~50× the archive size
or the destination's free space, and ask before continuing.

**Done:** `ARCHIVE_EXTRACT_PERM` is dropped (the umask applies, special bits go) and
each entry's mode is masked with `0777 & ~(g+w, o+w)`. Extraction now writes block by
block, so the guard and cancellation act inside one huge entry. Past max(50× the
archive, 1 GiB), the operation parks and the window asks "Extract Anyway?" for that
one archive (`largeExtractionNeedsConfirmation` / `confirmLargeExtraction`). Running
out of free space (keeping 64 MiB of headroom) fails without asking. Tests:
`extractionDropsDangerousModes`, `extractionHonoursTheUmask`,
`groupWriteNeverComesFromTheArchive`, `hugeExpansionAsksFirst`,
`hugeExpansionInsideATarAsksToo`, `extractionStopsBeforeTheDiskIsFull`,
`operationsAskBeforeAHugeExpansion`, `decliningAHugeExpansionDropsIt`,
`consentCoversOneArchiveOnly`.

## 6. Release supply chain — Low
`packaging/PKGBUILD` builds from `#tag=v$pkgver` with `sha256sums=('SKIP')` (tags are
mutable); the README has users `curl` an unsigned `.pkg.tar.zst` with no checksum.

**Fix:** pin `#commit=<sha>` (or signed tags + `validpgpkeys`), publish SHA256SUMS
with each release and show the verification step in the README.

**Done:** the PKGBUILD pins `_commit=20fbae8…` (what upstream `v0.1.23` resolves to)
with a real `sha256sums` (pacman ≥ 6.1 checksums git sources). Checked with `makepkg
--verifysource`: it passes, and a wrong hash fails. The README downloads `SHA256SUMS`
and runs `sha256sum -c` before `pacman -U`. **Still to do by hand:** upload a
`SHA256SUMS` file to the release (`sha256sum omanta-*.pkg.tar.zst > SHA256SUMS`).

## 7. `omanta-switch` replaces symlinked config files — Low
`packaging/bin/omanta-switch:82-96` (`mktemp` + `mv`) and `:165-168` (`sed -i`)
replace `bindings.lua` / `omarchy-menu.jsonc`. A symlink into a dotfiles repo becomes
a regular file, and the mktemp file's 0600 mode is carried over.

**Fix:** resolve with `readlink -f` and write through it (`cat "$tmp" > "$target"`),
keeping the original mode.

**Done:** `replace_contents` writes through the link with `cat >`, which keeps the link,
the inode and the mode. Both `remove_bindings_block` and `remove_menu_block` use it.
Test: `tst_switcher` `keepsSymlinkedConfigsSymlinked`.

## Not issues (checked)
- D-Bus `OpenPaths` / `ShowItems` trust any session-bus caller: the normal same-user
  boundary.
- Custom action TOMLs only load from root-owned `/usr/share/omanta/actions` or the
  user's own `~/.config/omanta/actions`; commands run as argv, terminal wrappers
  are single-quote escaped.
- Full-text search binds user input as SPARQL parameters.
- Copies/moves use NOFOLLOW, staged publication and `RENAME_NOREPLACE`.
