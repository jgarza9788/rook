#include "ThumbnailProvider.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QQuickImageResponse>
#include <QRunnable>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>

#include <gio/gio.h>

#include <sys/stat.h>
#include <cerrno>

namespace {

QMutex g_registryMutex;
bool g_registryLoaded = false;
QHash<QString, QStringList> g_thumbnailers; // mime type → argv template

QString thumbnailRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
           + QStringLiteral("/thumbnails");
}

// The spec keys the cache on the MD5 of the file's URI — not its path — so the
// hash must match byte for byte what other applications produce.
QString hashFor(const QString &filePath)
{
    const QByteArray uri = QUrl::fromLocalFile(filePath).toEncoded();
    return QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex());
}

QString uriFor(const QString &filePath)
{
    return QString::fromLatin1(QUrl::fromLocalFile(filePath).toEncoded());
}

// The spec wants the cache private: thumbnails are miniature copies of
// whatever the user looks at. 0700 from the thumbnail root down; a directory
// an older build (or another application) made under the umask is tightened.
bool makePrivateDirectory(const QString &directory)
{
    const QByteArray native = QFile::encodeName(directory);
    if (::mkdir(native.constData(), 0700) == 0)
        return true;
    if (errno != EEXIST)
        return false;
    // stat, not lstat: a cache symlinked elsewhere is still a cache.
    struct stat st;
    if (::stat(native.constData(), &st) != 0 || !S_ISDIR(st.st_mode))
        return false;
    if (st.st_mode & 077)
        ::chmod(native.constData(), 0700);
    return true;
}

bool ensurePrivatePath(const QString &directory)
{
    const QString root = thumbnailRoot();
    // Above the root is the ordinary cache directory; it keeps its own mode.
    QDir().mkpath(QFileInfo(root).absolutePath());
    if (!makePrivateDirectory(root))
        return false;
    QString current = root;
    for (const QString &part : QDir(root).relativeFilePath(directory)
                                   .split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        current += QLatin1Char('/') + part;
        if (!makePrivateDirectory(current))
            return false;
    }
    return true;
}

// Written beside the target and renamed over it, 0600 — the spec's rule, so
// another reader never sees half a PNG and no other user can read it at all.
bool savePrivatePng(const QString &path, const QImage &image)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!image.save(&file, "png")) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

// The image types decoded in-process: Qt's own, long-fuzzed readers. The
// rest (TIFF, WebP, JPEG 2000, SVG, ICNS...) go to a sandboxed thumbnailer
// when there is one, because a decoder bug there would otherwise run inside
// the file manager itself.
const QSet<QString> &inProcessImageTypes()
{
    static const QSet<QString> types = {
        QStringLiteral("image/png"),
        QStringLiteral("image/jpeg"),
        QStringLiteral("image/gif"),
        QStringLiteral("image/bmp"),
        QStringLiteral("image/x-bmp"),
        QStringLiteral("image/x-ms-bmp"),
    };
    return types;
}

// Far past any real photo or scan (16384²), and refused from the header alone
// — before a decoder that ignores the scaled size gets to allocate for it.
constexpr qint64 kMaximumSourcePixels = 16384LL * 16384LL;

// What a thumbnailer hands back is as untrusted as its input: a compromised
// one controls the PNG. Read strictly as PNG, sized from the header first.
QImage readThumbnailerOutput(const QString &path, int size)
{
    if (!QFileInfo(path).isFile())
        return {};
    QImageReader reader(path, "png");
    reader.setAutoDetectImageFormat(false);
    const QSize reported = reader.size();
    if (!reported.isValid() || qint64(reported.width()) * reported.height() > 4096LL * 4096LL)
        return {};
    reader.setAllocationLimit(128);
    QImage image = reader.read();
    if (!image.isNull() && (image.width() > size || image.height() > size))
        image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

QMutex g_sandboxMutex;
bool g_sandboxProbed = false;
bool g_sandboxWorks = false;

} // namespace

int ThumbnailCache::bucketFor(int requestedSize)
{
    if (requestedSize <= 128)
        return 128;
    if (requestedSize <= 256)
        return 256;
    if (requestedSize <= 512)
        return 512;
    return 1024;
}

QString ThumbnailCache::bucketName(int bucket)
{
    switch (bucket) {
    case 128: return QStringLiteral("normal");
    case 256: return QStringLiteral("large");
    case 512: return QStringLiteral("x-large");
    default: return QStringLiteral("xx-large");
    }
}

QString ThumbnailCache::cachePathFor(const QString &filePath, int bucket)
{
    return QStringLiteral("%1/%2/%3.png")
        .arg(thumbnailRoot(), bucketName(bucket), hashFor(filePath));
}

QString ThumbnailCache::failMarkerFor(const QString &filePath)
{
    // Kept under our own name so a failure here never suppresses another
    // application's attempt, and vice versa.
    return QStringLiteral("%1/fail/omanta/%2.png").arg(thumbnailRoot(), hashFor(filePath));
}

QImage ThumbnailCache::loadValid(const QString &filePath, int bucket)
{
    const QString cached = cachePathFor(filePath, bucket);
    if (!QFileInfo::exists(cached))
        return {};

    QImage image(cached);
    if (image.isNull())
        return {};

    // Thumb::MTime is what makes the cache correct rather than merely fast: an
    // edited file must not keep showing its old preview.
    const QFileInfo info(filePath);
    const QString recorded = image.text(QStringLiteral("Thumb::MTime"));
    if (recorded.isEmpty() || recorded.toLongLong() != info.lastModified().toSecsSinceEpoch())
        return {};

    // Thumb::MTime has one-second resolution, so a picture re-saved within
    // the second it was written would pass that check. Size (optional in the
    // spec) and our own millisecond stamp catch those, where present.
    const QString size = image.text(QStringLiteral("Thumb::Size"));
    if (!size.isEmpty() && size.toLongLong() != info.size())
        return {};
    const QString msecs = image.text(QStringLiteral("X-Omanta::MTime-MSec"));
    if (!msecs.isEmpty() && msecs.toLongLong() != info.lastModified().toMSecsSinceEpoch())
        return {};

    return image;
}

ThumbnailCache::Version ThumbnailCache::Version::of(const QString &filePath)
{
    const QFileInfo info(filePath);
    if (!info.exists())
        return {};
    return { info.lastModified().toMSecsSinceEpoch(), info.size() };
}

void ThumbnailCache::store(const QString &filePath, int bucket, QImage image,
                           const Version &rendered)
{
    if (image.isNull() || rendered.size < 0)
        return;

    const QString cached = cachePathFor(filePath, bucket);
    if (!ensurePrivatePath(QFileInfo(cached).absolutePath()))
        return;

    image.setText(QStringLiteral("Thumb::URI"), uriFor(filePath));
    image.setText(QStringLiteral("Thumb::MTime"),
                  QString::number(QDateTime::fromMSecsSinceEpoch(rendered.modifiedMSecs)
                                      .toSecsSinceEpoch()));
    image.setText(QStringLiteral("Thumb::Size"), QString::number(rendered.size));
    image.setText(QStringLiteral("X-Omanta::MTime-MSec"), QString::number(rendered.modifiedMSecs));
    image.setText(QStringLiteral("Software"), QStringLiteral("omanta"));

    savePrivatePng(cached, image);
}

void ThumbnailCache::markFailed(const QString &filePath, const Version &attempted)
{
    if (attempted.size < 0)
        return;
    const QString marker = failMarkerFor(filePath);
    if (!ensurePrivatePath(QFileInfo(marker).absolutePath()))
        return;

    // A 1×1 image carrying the mtime: enough to stop retrying every scroll,
    // and it stops applying once the file itself changes.
    QImage marker1x1(1, 1, QImage::Format_ARGB32);
    marker1x1.fill(Qt::transparent);
    marker1x1.setText(QStringLiteral("Thumb::URI"), uriFor(filePath));
    marker1x1.setText(QStringLiteral("Thumb::MTime"),
                      QString::number(QDateTime::fromMSecsSinceEpoch(attempted.modifiedMSecs)
                                          .toSecsSinceEpoch()));
    marker1x1.setText(QStringLiteral("Thumb::Size"), QString::number(attempted.size));
    savePrivatePng(marker, marker1x1);
}

bool ThumbnailCache::hasFailed(const QString &filePath)
{
    const QString marker = failMarkerFor(filePath);
    if (!QFileInfo::exists(marker))
        return false;

    QImage image(marker);
    const QFileInfo info(filePath);
    const QString recorded = image.text(QStringLiteral("Thumb::MTime"));
    const QString size = image.text(QStringLiteral("Thumb::Size"));
    // A file that has changed since it failed deserves another go.
    return !recorded.isEmpty() && recorded.toLongLong() == info.lastModified().toSecsSinceEpoch()
        && (size.isEmpty() || size.toLongLong() == info.size());
}

void ThumbnailCache::ensureRegistryLoaded()
{
    QMutexLocker lock(&g_registryMutex);
    if (g_registryLoaded)
        return;
    g_registryLoaded = true;

    QStringList directories;
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        directories << base + QStringLiteral("/thumbnailers");

    for (const QString &directory : std::as_const(directories)) {
        const QDir dir(directory);
        if (!dir.exists())
            continue;

        for (const QFileInfo &entry : dir.entryInfoList({ QStringLiteral("*.thumbnailer") },
                                                        QDir::Files)) {
            QFile file(entry.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            // Parsed by hand rather than with QSettings. A .thumbnailer is a
            // desktop-entry file, and QSettings' INI reader mangles them: it
            // treats ';' as a comment introducer, and ';' is exactly the
            // character separating the MimeType list. The result was an empty
            // registry and every video falling back to a generic icon.
            QString exec, tryExec, mimeLine;
            bool inEntry = false;

            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                    continue;

                if (line.startsWith(QLatin1Char('['))) {
                    inEntry = line.compare(QLatin1String("[Thumbnailer Entry]"),
                                           Qt::CaseInsensitive) == 0;
                    continue;
                }
                if (!inEntry)
                    continue;

                const int equals = line.indexOf(QLatin1Char('='));
                if (equals <= 0)
                    continue;

                const QString key = line.left(equals).trimmed();
                const QString value = line.mid(equals + 1).trimmed();

                if (key == QLatin1String("Exec"))
                    exec = value;
                else if (key == QLatin1String("TryExec"))
                    tryExec = value;
                else if (key == QLatin1String("MimeType"))
                    mimeLine = value;
            }

            if (exec.isEmpty() || mimeLine.isEmpty())
                continue;
            if (!tryExec.isEmpty() && QStandardPaths::findExecutable(tryExec).isEmpty())
                continue;

            const QStringList argv = QProcess::splitCommand(exec);
            if (argv.isEmpty() || QStandardPaths::findExecutable(argv.first()).isEmpty())
                continue;

            for (const QString &mime : mimeLine.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                if (!g_thumbnailers.contains(mime.trimmed()))
                    g_thumbnailers.insert(mime.trimmed(), argv);
            }
        }
    }
}

bool ThumbnailCache::canHandle(const QString &mimeType)
{
    ensureRegistryLoaded();
    QMutexLocker lock(&g_registryMutex);
    return g_thumbnailers.contains(mimeType);
}

QStringList ThumbnailCache::commandFor(const QString &mimeType)
{
    ensureRegistryLoaded();
    QMutexLocker lock(&g_registryMutex);
    return g_thumbnailers.value(mimeType);
}

// ---------------------------------------------------------------------------

QString ThumbnailCache::contentTypeOf(const QString &filePath)
{
    // g_content_type_guess() with no data looks only at the extension, so a
    // valid PNG saved without one came back as application/octet-stream and
    // never got a thumbnail. query_info sniffs the contents, which is also what
    // the directory model does — so the two agree.
    QString mimeType;
    GFile *file = g_file_new_for_path(filePath.toUtf8().constData());
    if (GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                            G_FILE_QUERY_INFO_NONE, nullptr, nullptr)) {
        if (const char *type = g_file_info_get_content_type(info))
            mimeType = QString::fromUtf8(type);
        g_object_unref(info);
    }
    g_object_unref(file);
    return mimeType;
}

bool ThumbnailCache::decodesInProcess(const QString &mimeType)
{
    return inProcessImageTypes().contains(mimeType);
}

QImage ThumbnailCache::render(const QString &filePath, const QString &mimeType, int size)
{
    QImage image;
    if (mimeType.startsWith(QLatin1String("image/"))) {
        // An exotic format with a sandboxed thumbnailer never reaches an
        // in-process decoder — not even as a fallback when the thumbnailer
        // fails, since a file built to break that decoder fails there first.
        const bool sandboxedElsewhere = canHandle(mimeType) && sandboxAvailable();
        if (decodesInProcess(mimeType) || !sandboxedElsewhere)
            image = renderImageFile(filePath, size);
    }
    if (image.isNull())
        image = renderViaThumbnailer(filePath, mimeType, size);
    return image;
}

QImage ThumbnailCache::renderImageFile(const QString &filePath, int size)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true); // honour EXIF orientation

    // Qt caps decoded image allocations at 256MB by default, which rejects
    // ordinary high-resolution scans before they can be scaled down. Generating
    // a thumbnail is precisely the case where decoding a large file is the
    // intended behaviour.
    reader.setAllocationLimit(1024);

    const QSize original = reader.size();
    if (original.isValid()) {
        if (qint64(original.width()) * original.height() > kMaximumSourcePixels)
            return {};
        // Ask the decoder for a reduced size where it can oblige. On a 60MP
        // scan that is the difference between instant and a visible stall.
        QSize target = original;
        target.scale(size, size, Qt::KeepAspectRatio);
        reader.setScaledSize(target);
    }

    QImage image = reader.read();
    if (image.isNull())
        return {};

    if (image.width() > size || image.height() > size)
        image = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

QImage ThumbnailCache::renderViaThumbnailer(const QString &filePath, const QString &mimeType, int size)
{
    const QStringList argv = ThumbnailCache::commandFor(mimeType);
    if (argv.isEmpty())
        return {};
    return runThumbnailer(argv, filePath, size);
}

QStringList ThumbnailCache::sandboxedCommand(const QStringList &command, const QString &input,
                                             const QString &outputDirectory)
{
    const QString bwrap = QStandardPaths::findExecutable(QStringLiteral("bwrap"));
    if (bwrap.isEmpty() || command.isEmpty())
        return {};

    // gnome-desktop's thumbnailer sandbox, near enough: no network, no other
    // namespaces, nothing of the session's environment, the system read-only,
    // the one input readable and only the output directory writable.
    QStringList sandboxed = {
        bwrap,
        QStringLiteral("--unshare-all"),
        QStringLiteral("--die-with-parent"),
        QStringLiteral("--new-session"),
        QStringLiteral("--clearenv"),
        QStringLiteral("--setenv"), QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin"),
        QStringLiteral("--setenv"), QStringLiteral("HOME"), QStringLiteral("/tmp"),
        QStringLiteral("--setenv"), QStringLiteral("GIO_USE_VFS"), QStringLiteral("local"),
        QStringLiteral("--ro-bind"), QStringLiteral("/usr"), QStringLiteral("/usr"),
    };
    // Merged-/usr systems link these into /usr; others have real directories.
    for (const char *top : { "/bin", "/sbin", "/lib", "/lib64", "/lib32" }) {
        const QFileInfo info(QString::fromLatin1(top));
        if (info.isSymLink())
            sandboxed << QStringLiteral("--symlink") << info.symLinkTarget() << info.filePath();
        else if (info.isDir())
            sandboxed << QStringLiteral("--ro-bind") << info.filePath() << info.filePath();
    }
    for (const char *shared : { "/etc/ld.so.cache", "/etc/fonts", "/etc/alternatives",
                                "/etc/localtime", "/var/cache/fontconfig" })
        sandboxed << QStringLiteral("--ro-bind-try") << QString::fromLatin1(shared)
                  << QString::fromLatin1(shared);
    sandboxed << QStringLiteral("--proc") << QStringLiteral("/proc")
              << QStringLiteral("--dev") << QStringLiteral("/dev")
              << QStringLiteral("--tmpfs") << QStringLiteral("/tmp");
    // A thumbnailer installed outside the system tree (~/.local/bin) is
    // let in on its own, read-only; nothing else of the home directory is.
    // Judged by where its directory really is: /bin/sh is visible, because
    // /bin leads into /usr inside the sandbox too.
    const QString program = command.first();
    const QString programDirectory =
        QFileInfo(QFileInfo(program).absolutePath()).canonicalFilePath();
    if (program.startsWith(QLatin1Char('/')) && programDirectory != QLatin1String("/usr")
        && !programDirectory.startsWith(QLatin1String("/usr/"))) {
        const QString real = QFileInfo(program).canonicalFilePath();
        sandboxed << QStringLiteral("--ro-bind-try") << (real.isEmpty() ? program : real)
                  << program;
    }
    // After the /tmp tmpfs, so files under /tmp stay visible. Same paths as
    // outside, so %i, %u and %o need no rewriting.
    if (!input.isEmpty())
        sandboxed << QStringLiteral("--ro-bind") << input << input;
    if (!outputDirectory.isEmpty())
        sandboxed << QStringLiteral("--bind") << outputDirectory << outputDirectory;
    sandboxed << QStringLiteral("--chdir") << QStringLiteral("/") << QStringLiteral("--");
    return sandboxed + command;
}

bool ThumbnailCache::sandboxAvailable()
{
    QMutexLocker lock(&g_sandboxMutex);
    if (g_sandboxProbed)
        return g_sandboxWorks;
    g_sandboxProbed = true;

    // bwrap present is not bwrap working: unprivileged user namespaces can be
    // switched off. One trial run answers for the session.
    const QString truePath = QStandardPaths::findExecutable(QStringLiteral("true"));
    QStringList probe = sandboxedCommand({ truePath.isEmpty() ? QStringLiteral("/usr/bin/true")
                                                              : truePath },
                                         QString(), QString());
    if (!probe.isEmpty()) {
        QProcess process;
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.start(probe.takeFirst(), probe);
        g_sandboxWorks = process.waitForFinished(5000)
            && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(1000);
        }
    }
    if (!g_sandboxWorks)
        qWarning("omanta: bubblewrap (bwrap) is missing or cannot create a sandbox; "
                 "thumbnailers run unsandboxed");
    return g_sandboxWorks;
}

QImage ThumbnailCache::runThumbnailer(QStringList argv, const QString &filePath, int size)
{
    if (argv.isEmpty())
        return {};

    // A directory of its own, so it is the only thing the sandbox can write.
    QTemporaryDir outputDirectory(QDir::tempPath() + QStringLiteral("/omanta-thumb-XXXXXX"));
    if (!outputDirectory.isValid())
        return {};
    const QString output = outputDirectory.filePath(QStringLiteral("thumbnail.png"));

    for (QString &argument : argv) {
        argument.replace(QStringLiteral("%i"), filePath);
        argument.replace(QStringLiteral("%u"), QString::fromLatin1(QUrl::fromLocalFile(filePath).toEncoded()));
        argument.replace(QStringLiteral("%o"), output);
        argument.replace(QStringLiteral("%s"), QString::number(size));
    }
    if (sandboxAvailable()) {
        // Resolved here, with the session's PATH: inside, PATH is /usr/bin.
        const QString resolved = QStandardPaths::findExecutable(argv.first());
        if (!resolved.isEmpty())
            argv.first() = resolved;
        argv = sandboxedCommand(argv, QFileInfo(filePath).absoluteFilePath(), outputDirectory.path());
    }
    if (argv.isEmpty())
        return {};

    QProcess process;
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    const QString program = argv.takeFirst();
    process.start(program, argv);
    // A wedged decoder must not hold a pool thread forever.
    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};

    return readThumbnailerOutput(output, size);
}

namespace {

class ThumbnailResponse : public QQuickImageResponse, public QRunnable
{
public:
    ThumbnailResponse(const QString &filePath, const ThumbnailCache::Version &expected, int size)
        : m_filePath(filePath)
        , m_expected(expected)
        , m_bucket(ThumbnailCache::bucketFor(size))
    {
        setAutoDelete(false);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    void run() override
    {
        const QFileInfo info(m_filePath);
        if (!info.exists() || !info.isFile()) {
            fail(QStringLiteral("no such file"));
            return;
        }

        if (superseded(ThumbnailCache::Version::of(m_filePath)))
            return;

        if (QImage cached = ThumbnailCache::loadValid(m_filePath, m_bucket); !cached.isNull()) {
            m_image = cached;
            Q_EMIT finished();
            return;
        }

        if (ThumbnailCache::hasFailed(m_filePath)) {
            fail(QStringLiteral("previously failed"));
            return;
        }

        // A file being saved over while it renders (an editor writing, a
        // copy landing) yields either the old picture or a half-written
        // decode failure. Neither may be recorded against the new version,
        // so render again until the file holds still.
        QImage generated;
        ThumbnailCache::Version version;
        for (int attempt = 0; attempt < 3; ++attempt) {
            version = ThumbnailCache::Version::of(m_filePath);
            const QString mimeType = ThumbnailCache::contentTypeOf(m_filePath);
            generated = ThumbnailCache::render(m_filePath, mimeType, m_bucket);
            if (ThumbnailCache::Version::of(m_filePath) == version) {
                if (generated.isNull())
                    ThumbnailCache::markFailed(m_filePath, version);
                else
                    ThumbnailCache::store(m_filePath, m_bucket, generated, version);
                break;
            }
        }

        if (superseded(version))
            return;

        if (generated.isNull()) {
            fail(QStringLiteral("could not render"));
            return;
        }

        m_image = generated;
        Q_EMIT finished();
    }

    QString errorString() const override { return m_error; }

private:
    // Qt caches the answer under the URL, and the URL names one version of
    // the file. Answering it with a picture of any other version would
    // cache that picture as this version's, to be served back if the file
    // is ever at this version again. The view asks afresh once its model
    // catches up with the file.
    bool superseded(const ThumbnailCache::Version &actual)
    {
        if (m_expected.modifiedMSecs <= 0 || actual == m_expected)
            return false;
        fail(QStringLiteral("file changed"));
        return true;
    }

    void fail(const QString &reason)
    {
        // The view falls back to the file-type icon when a response errors, so
        // failing is a normal outcome here, not an exceptional one.
        m_error = reason;
        Q_EMIT finished();
    }

    QString m_filePath;
    ThumbnailCache::Version m_expected;
    int m_bucket;
    QImage m_image;
    QString m_error;
};

} // namespace

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    const int size = requestedSize.width() > 0 ? requestedSize.width() : 128;
    ThumbnailCache::Version expected;
    const QString path = Thumbnails::pathFromId(id, &expected);
    auto *response = new ThumbnailResponse(path, expected, size);
    QThreadPool::globalInstance()->start(response);
    return response;
}

// ---------------------------------------------------------------------------

Thumbnails::Thumbnails(QObject *parent)
    : QObject(parent)
{
}

void Thumbnails::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    Q_EMIT enabledChanged();
}

void Thumbnails::setMaximumFileSize(qint64 bytes)
{
    if (m_maximumFileSize == bytes)
        return;
    m_maximumFileSize = bytes;
    Q_EMIT maximumFileSizeChanged();
}

bool Thumbnails::canThumbnail(const QString &mimeType, qint64 fileSize) const
{
    if (!m_enabled || mimeType.isEmpty())
        return false;

    // Images can always be decoded (in-process, or sandboxed for the exotic
    // formats); everything else needs a registered thumbnailer, and asking
    // about a type nothing handles just costs a process launch that will fail.
    //
    // The size cap guards ONLY the in-process image decode — Nautilus's
    // rule. An external thumbnailer (video, PDF) reads a few frames, not
    // the whole file, so a 4GB screen recording still gets its preview.
    if (mimeType.startsWith(QLatin1String("image/")))
        return m_maximumFileSize <= 0 || fileSize <= m_maximumFileSize;

    return ThumbnailCache::canHandle(mimeType);
}

QString Thumbnails::source(const QString &filePath, const QDateTime &modified,
                           qint64 fileSize) const
{
    // "<version>/<encoded path>": the version is opaque to the provider,
    // which only needs the path back.
    const qint64 msecs = modified.isValid() ? modified.toMSecsSinceEpoch() : 0;
    return QStringLiteral("image://thumbnail/%1-%2/%3")
        .arg(msecs)
        .arg(fileSize)
        .arg(QString::fromLatin1(QUrl::toPercentEncoding(filePath, "/")));
}

QString Thumbnails::pathFromId(const QString &id, ThumbnailCache::Version *version)
{
    const int slash = id.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    if (version) {
        const QStringList parts = id.left(slash).split(QLatin1Char('-'));
        if (parts.size() == 2)
            *version = { parts.at(0).toLongLong(), parts.at(1).toLongLong() };
    }
    // Qt hands the id over partly decoded, but never a literal '%' (it stays
    // %25), so decoding once more always recovers the original bytes.
    return QUrl::fromPercentEncoding(id.mid(slash + 1).toUtf8());
}
