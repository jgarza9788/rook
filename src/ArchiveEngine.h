#pragma once

#include "FileOperationTypes.h"

#include <QString>
#include <QStringList>

#include <functional>

// Compression and extraction over libarchive, as plain blocking functions the
// operation worker calls on its thread. libarchive rather than gnome-autoar:
// same underlying library Nautilus ends up in, but with direct control of
// progress, cancellation and the staging strategy — and no GObject async to
// bridge. Local paths only; archives on gvfs mounts are out of scope.
namespace ArchiveEngine {

// True to abort. Checked between entries and between data chunks.
using Cancelled = std::function<bool()>;
// bytes done, bytes total (0 while the total is still being counted).
using Progress = std::function<void(qint64, qint64)>;

// How a password-protected zip is encrypted. AES-256 (WinZip AE-2) is what
// 7-Zip, WinRAR, bsdtar and current Windows read; ZipCrypto is the legacy
// scheme every unzipper reads, including older Windows Explorer — and broken
// by known-plaintext attacks, so it is only ever an explicit choice.
enum class ZipEncryption { Aes256, ZipCrypto };

// The formats the Compress dialog offers, chosen by the archive path's
// extension: .zip, .tar.xz, .7z. A non-empty password produces an encrypted
// zip (zip only — libarchive cannot write encrypted 7z or tar).
bool compress(const QStringList &sources, const QString &archivePath,
              QString *error, const Cancelled &cancelled, const Progress &progress,
              const QString &password = QString(),
              ZipEncryption encryption = ZipEncryption::Aes256);

// Guards against an archive built to fill the disk. Extraction stops and
// asks once the output passes `expansionRatio` × the archive's own size (and
// `expansionFloorBytes`, so small archives of text never ask); it stops
// outright before the destination's free space runs out.
struct ExtractLimits {
    bool allowLargeExpansion = false; // the user already said yes
    qint64 expansionRatio = 50;
    qint64 expansionFloorBytes = 1024LL * 1024 * 1024;
    qint64 availableBytes = -1; // -1: ask the destination's filesystem
};
// What the operation worker starts from. Tests lower these rather than
// writing gigabytes.
ExtractLimits &defaultExtractLimits();

// Extracts with Nautilus's (autoar's) landing rule: an archive with a single
// top-level entry extracts as that entry; anything else lands in a new folder
// named after the archive. Either way the target is unique-ified Nautilus
// style ("name 2", "name 3") rather than overwriting. Everything goes through
// a hidden staging directory first, so a failed extraction leaves nothing
// behind; entry paths are sanitized, so a hostile archive cannot write
// outside the destination. `produced` receives the final top-level path.
// `needsPassphrase` (optional) is set when the failure was a missing or wrong
// password — the caller can ask for one and retry instead of showing an error.
// `created` optionally receives the per-entry journal for a safe Undo.
// Stored permissions are honoured only as far as the umask allows, minus
// setuid/setgid/sticky and group/other write: an archive cannot plant a
// world-writable file or a setgid directory. `needsExpansionConfirmation`
// (optional) is set when `limits` stopped an unusually large expansion — the
// caller can ask and retry with allowLargeExpansion.
bool extract(const QString &archivePath, const QString &destinationDir,
             QString *produced, QString *error, const Cancelled &cancelled,
             const Progress &progress, const QString &password = QString(),
             bool *needsPassphrase = nullptr, QList<CreatedEntry> *created = nullptr,
             const ExtractLimits &limits = ExtractLimits(),
             bool *needsExpansionConfirmation = nullptr);

bool undoExtraction(const QList<CreatedEntry> &created, QString *error,
                    const Cancelled &cancelled);

// Where the archive extension starts in `name` (compound-aware: ".tar.gz" is
// one unit), or -1. Used for the landing folder's name and by the UI to
// suggest archive names.
// The quick view's look inside: entry names and sizes from the headers
// alone — nothing is written anywhere. Stops after `limit` entries
// (*truncated says so); *total counts what was listed. Encrypted headers and
// damaged archives answer false with *error.
struct ListedEntry {
    QString path;
    qint64 size = 0;
    bool directory = false;
};
bool list(const QString &archivePath, int limit, QList<ListedEntry> *entries,
          bool *truncated, qint64 *totalBytes, QString *error);

int archiveExtensionOffset(const QString &name);

// Content types "Extract Here" is offered for — what libarchive here can
// actually open, phrased as the types GIO reports.
bool isArchiveContentType(const QString &contentType);

} // namespace ArchiveEngine
