#include "TestFixture.h"
#include "ThumbnailProvider.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QQuickImageResponse>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <memory>

#include <sys/stat.h>

// Thumbnails, tested against the freedesktop spec rather than against my
// assumptions about it. Two of these exist because the first working build got
// them wrong: every video fell back to a generic icon, and a perfectly ordinary
// high-resolution PNG refused to render.
class TestThumbnails : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void bucketsRoundUpToSpecSizes();
    void cachePathFollowsTheSpec();

    void registryFindsVideoThumbnailers();
    void rendersASmallImage();
    void rendersAHighResolutionImage();
    void honoursExifOrientation();

    void storesAndReusesACachedThumbnail();
    void invalidatesTheCacheWhenTheFileChanges();
    void invalidatesTheCacheWithinTheSameSecond();
    void sourceRoundTripsAwkwardNames();
    void providerAnswersOnlyTheVersionAsked();
    void remembersFailuresButNotForever();

    void detectsTypeByContentNotExtension();
    void canThumbnailRespectsTypeAndSize();

    void cacheFilesArePrivate();
    void cacheDirectoriesAreTightened();
    void cacheWritesLeaveNoTemporaries();
    void onlyCoreFormatsDecodeInProcess();
    void refusesAbsurdDimensionsFromTheHeader();
    void sandboxCommandConfinesTheThumbnailer();
    void thumbnailerRunsInTheSandbox();
    void rejectsHostileThumbnailerOutput();
    void userInstalledThumbnailersStillRun();
    void rendersRealFilesThroughTheSandbox();

private:
    static QString writeImage(const TempTree &tree, const QString &name, int w, int h)
    {
        QImage image(w, h, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QPainter painter(&image);
        painter.fillRect(0, 0, w / 2, h, Qt::magenta);
        painter.end();
        const QString path = tree.filePath(name);
        // Format given explicitly: QImage::save() infers it from the suffix,
        // and this helper is deliberately used to write files without one.
        image.save(path, "png");
        return path;
    }

    // A PNG whose header claims a size no real image has; the body is junk.
    static QString writeGiantHeader(const TempTree &tree, const QString &name)
    {
        const auto crc32 = [](const QByteArray &data) {
            quint32 crc = 0xffffffffu;
            for (const char byte : data) {
                crc ^= quint8(byte);
                for (int bit = 0; bit < 8; ++bit)
                    crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
            }
            return ~crc;
        };
        const auto be32 = [](quint32 value) {
            QByteArray out(4, 0);
            for (int i = 0; i < 4; ++i)
                out[i] = char(value >> (24 - 8 * i));
            return out;
        };
        const auto chunk = [&](const QByteArray &type, const QByteArray &data) {
            return be32(quint32(data.size())) + type + data + be32(crc32(type + data));
        };
        QByteArray ihdr = be32(100000) + be32(100000);
        ihdr += QByteArray::fromHex("0802000000"); // 8-bit RGB
        const QByteArray png = QByteArray::fromHex("89504e470d0a1a0a") + chunk("IHDR", ihdr)
            + chunk("IDAT", QByteArray(64, 'x')) + chunk("IEND", {});
        const QString path = tree.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        file.write(png);
        return path;
    }

    static int modeOf(const QString &path)
    {
        struct stat st;
        return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? int(st.st_mode & 07777) : -1;
    }

    // Copies its input to its output: the smallest honest thumbnailer.
    static QStringList copyingThumbnailer()
    {
        return { QStringLiteral("/bin/sh"), QStringLiteral("-c"),
                 QStringLiteral("cp \"$1\" \"$2\""), QStringLiteral("sh"),
                 QStringLiteral("%i"), QStringLiteral("%o") };
    }
};

void TestThumbnails::initTestCase()
{
    // Never scribble on the real thumbnail cache while testing.
    QStandardPaths::setTestModeEnabled(true);
}

void TestThumbnails::bucketsRoundUpToSpecSizes()
{
    QCOMPARE(ThumbnailCache::bucketFor(16), 128);
    QCOMPARE(ThumbnailCache::bucketFor(128), 128);
    QCOMPARE(ThumbnailCache::bucketFor(129), 256);
    QCOMPARE(ThumbnailCache::bucketFor(512), 512);
    QCOMPARE(ThumbnailCache::bucketFor(4096), 1024);

    // The directory names are part of the shared-cache contract.
    QCOMPARE(ThumbnailCache::bucketName(128), QStringLiteral("normal"));
    QCOMPARE(ThumbnailCache::bucketName(256), QStringLiteral("large"));
    QCOMPARE(ThumbnailCache::bucketName(512), QStringLiteral("x-large"));
    QCOMPARE(ThumbnailCache::bucketName(1024), QStringLiteral("xx-large"));
}

void TestThumbnails::cachePathFollowsTheSpec()
{
    // The whole point of the spec is a shared cache: the filename must be the
    // MD5 of the file's URI, or thumbnails other applications generated will
    // never be found and ours will never be reused.
    const QString path = QStringLiteral("/home/someone/Pictures/a photo.jpg");
    const QByteArray uri = QUrl::fromLocalFile(path).toEncoded();
    const QString expected =
        QString::fromLatin1(QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex())
        + QStringLiteral(".png");

    const QString cachePath = ThumbnailCache::cachePathFor(path, 256);
    QVERIFY2(cachePath.endsWith(expected), qPrintable(cachePath));
    QVERIFY2(cachePath.contains(QStringLiteral("/thumbnails/large/")), qPrintable(cachePath));
}

void TestThumbnails::registryFindsVideoThumbnailers()
{
    // The first implementation parsed .thumbnailer files with QSettings, whose
    // INI reader treats ';' as a comment introducer — the exact character
    // separating the MimeType list. The registry came back empty and every
    // video silently fell back to a generic icon.
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpegthumbnailer")).isEmpty())
        QSKIP("ffmpegthumbnailer is not installed");

    QVERIFY2(ThumbnailCache::canHandle(QStringLiteral("video/quicktime")),
             "video/quicktime must resolve to a thumbnailer");
    QVERIFY(ThumbnailCache::canHandle(QStringLiteral("video/mp4")));

    const QStringList command = ThumbnailCache::commandFor(QStringLiteral("video/quicktime"));
    QVERIFY(!command.isEmpty());
    QVERIFY(command.first().contains(QStringLiteral("ffmpegthumbnailer")));
    // The placeholders must survive parsing or substitution has nothing to do.
    QVERIFY(command.contains(QStringLiteral("%i")));
    QVERIFY(command.contains(QStringLiteral("%o")));
}

void TestThumbnails::rendersASmallImage()
{
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("small.png"), 400, 300);

    const QImage thumb = ThumbnailCache::render(path, QStringLiteral("image/png"), 256);
    QVERIFY(!thumb.isNull());
    QVERIFY(thumb.width() <= 256 && thumb.height() <= 256);
    // Aspect ratio must survive, or previews look squashed.
    QVERIFY(qAbs(qreal(thumb.width()) / thumb.height() - 4.0 / 3.0) < 0.05);
}

void TestThumbnails::rendersAHighResolutionImage()
{
    // Qt refuses image allocations over 256MB by default, which rejected an
    // ordinary 3160×2272 scan before it could be scaled down. Thumbnailing is
    // exactly the case where decoding a large file is the point.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("scan.png"), 5000, 4000);

    const QImage thumb = ThumbnailCache::render(path, QStringLiteral("image/png"), 256);
    QVERIFY2(!thumb.isNull(), "a high-resolution image must still produce a thumbnail");
    QVERIFY(thumb.width() <= 256 && thumb.height() <= 256);
}

void TestThumbnails::honoursExifOrientation()
{
    // No EXIF here, but the reader must at least not mangle a plain image —
    // the setting that enables rotation is easy to lose in a refactor.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("wide.png"), 600, 200);

    const QImage thumb = ThumbnailCache::renderImageFile(path, 128);
    QVERIFY(!thumb.isNull());
    QVERIFY2(thumb.width() > thumb.height(), "a landscape image must stay landscape");
}

void TestThumbnails::storesAndReusesACachedThumbnail()
{
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("cached.png"), 300, 300);

    const QImage generated = ThumbnailCache::render(path, QStringLiteral("image/png"), 256);
    QVERIFY(!generated.isNull());
    ThumbnailCache::store(path, 256, generated);

    QVERIFY(QFile::exists(ThumbnailCache::cachePathFor(path, 256)));

    const QImage reloaded = ThumbnailCache::loadValid(path, 256);
    QVERIFY2(!reloaded.isNull(), "a freshly stored thumbnail must load back");
    QCOMPARE(reloaded.size(), generated.size());
}

void TestThumbnails::invalidatesTheCacheWhenTheFileChanges()
{
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("edited.png"), 300, 300);
    ThumbnailCache::store(path, 256, ThumbnailCache::render(path, QStringLiteral("image/png"), 256));
    QVERIFY(!ThumbnailCache::loadValid(path, 256).isNull());

    // Edit the file: the stale preview must stop being served.
    QTest::qWait(1100); // mtime has one-second resolution
    writeImage(tree, QStringLiteral("edited.png"), 300, 300);
    tree.setModified(QStringLiteral("edited.png"), QDateTime::currentDateTime().addSecs(5));

    QVERIFY2(ThumbnailCache::loadValid(path, 256).isNull(),
             "an edited file must not keep showing its old thumbnail");
}

void TestThumbnails::invalidatesTheCacheWithinTheSameSecond()
{
    // Thumb::MTime is whole seconds: a picture re-saved inside the second it
    // was written, then pinned back to that second, still has to invalidate.
    TempTree tree;
    const QDateTime when = QDateTime::currentDateTime().addSecs(-30);
    const QString path = writeImage(tree, QStringLiteral("resaved.png"), 300, 100);
    tree.setModified(QStringLiteral("resaved.png"), when);
    ThumbnailCache::store(path, 256, ThumbnailCache::render(path, QStringLiteral("image/png"), 256));
    QVERIFY(!ThumbnailCache::loadValid(path, 256).isNull());

    writeImage(tree, QStringLiteral("resaved.png"), 100, 300);
    tree.setModified(QStringLiteral("resaved.png"), when);
    QVERIFY2(ThumbnailCache::loadValid(path, 256).isNull(),
             "a same-second re-save must not keep showing its old thumbnail");
}

void TestThumbnails::sourceRoundTripsAwkwardNames()
{
    Thumbnails thumbnails;
    const QDateTime when = QDateTime::fromMSecsSinceEpoch(1700000000123);
    for (const QString &path : {QStringLiteral("/tmp/plain.png"),
                                QStringLiteral("/tmp/shot #1 100% done?.png"),
                                QStringLiteral("/tmp/%2F%25 literal.png"),
                                QStringLiteral("/tmp/ünïcødé 写真.jpg")}) {
        const QUrl url(thumbnails.source(path, when, 42));
        QCOMPARE(url.scheme(), QStringLiteral("image"));
        QCOMPARE(url.host(), QStringLiteral("thumbnail"));
        QVERIFY2(url.query().isEmpty() && url.fragment().isEmpty(), qPrintable(url.toString()));
        // What QQuickPixmap hands the provider as the id.
        const QString id = url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
        QCOMPARE(Thumbnails::pathFromId(id), path);
    }

    // Any change to the file changes the URL, and nothing else does.
    const QString base = thumbnails.source(QStringLiteral("/tmp/a.png"), when, 42);
    QCOMPARE(thumbnails.source(QStringLiteral("/tmp/a.png"), when, 42), base);
    QVERIFY(thumbnails.source(QStringLiteral("/tmp/a.png"), when.addMSecs(1), 42) != base);
    QVERIFY(thumbnails.source(QStringLiteral("/tmp/a.png"), when, 43) != base);
}

void TestThumbnails::providerAnswersOnlyTheVersionAsked()
{
    // Qt caches whatever comes back under the URL. A URL naming an older
    // version must not be answered with a picture of the file as it is now.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("versioned.png"), 300, 100);
    const QFileInfo info(path);
    Thumbnails thumbnails;
    ThumbnailProvider provider;
    const auto ask = [&](const QString &url) {
        const QString id = QUrl(url).toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
        std::unique_ptr<QQuickImageResponse> response(
            provider.requestImageResponse(id, QSize(128, 128)));
        QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
        if (!finished.wait(10000))
            return QString("timeout");
        std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
        return response->errorString().isEmpty() ? QString("image") : response->errorString();
    };

    QCOMPARE(ask(thumbnails.source(path, info.lastModified(), info.size())), QString("image"));
    QCOMPARE(ask(thumbnails.source(path, info.lastModified().addMSecs(-1), info.size())),
             QString("file changed"));
    QCOMPARE(ask(thumbnails.source(path, info.lastModified(), info.size() + 1)),
             QString("file changed"));
    // No mtime known (a remote row): whatever is there is the answer.
    QCOMPARE(ask(thumbnails.source(path, QDateTime(), 0)), QString("image"));
}

void TestThumbnails::remembersFailuresButNotForever()
{
    TempTree tree;
    const QString path = tree.writeFile(QStringLiteral("broken.png"), 32); // not a real PNG

    QVERIFY(!ThumbnailCache::hasFailed(path));
    ThumbnailCache::markFailed(path);
    QVERIFY2(ThumbnailCache::hasFailed(path),
             "a failure must be remembered, or every scroll retries it");

    // A file that has changed deserves another attempt.
    tree.setModified(QStringLiteral("broken.png"), QDateTime::currentDateTime().addSecs(120));
    QVERIFY2(!ThumbnailCache::hasFailed(path),
             "a changed file must be retried rather than written off forever");
}

void TestThumbnails::detectsTypeByContentNotExtension()
{
    // A real PNG saved with no extension. Guessing from the filename returns
    // application/octet-stream, nothing handles that, and the file silently
    // never gets a preview — which is exactly what happened.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("no-extension-here"), 200, 200);

    QCOMPARE(ThumbnailCache::contentTypeOf(path), QStringLiteral("image/png"));

    const QImage thumb = ThumbnailCache::render(path, ThumbnailCache::contentTypeOf(path), 128);
    QVERIFY2(!thumb.isNull(), "an extensionless image must still thumbnail");
}

void TestThumbnails::canThumbnailRespectsTypeAndSize()
{
    Thumbnails thumbnails;

    QVERIFY(thumbnails.canThumbnail(QStringLiteral("image/jpeg"), 1000));
    QVERIFY(!thumbnails.canThumbnail(QStringLiteral("text/plain"), 1000));
    QVERIFY(!thumbnails.canThumbnail(QString(), 1000));

    // The size cap guards the in-process image decode only.
    QVERIFY(!thumbnails.canThumbnail(QStringLiteral("image/jpeg"),
                                     thumbnails.maximumFileSize() + 1));

    // A video over the cap still thumbnails — the external thumbnailer reads
    // frames, not the whole file (Nautilus's rule; a 4GB recording previews).
    // External thumbnailers are optional; a minimal installation may have none.
    QCOMPARE(thumbnails.canThumbnail(QStringLiteral("video/mp4"),
                                      thumbnails.maximumFileSize() + 1),
             ThumbnailCache::canHandle(QStringLiteral("video/mp4")));

    thumbnails.setEnabled(false);
    QVERIFY(!thumbnails.canThumbnail(QStringLiteral("image/jpeg"), 1000));
}

void TestThumbnails::cacheFilesArePrivate()
{
    // Thumbnails are small copies of private pictures: the spec makes them
    // 0600 in 0700 directories, whatever the umask says.
    const mode_t previous = ::umask(022);
    // From an empty cache, so creating the directories is what is tested,
    // not tightening ones an earlier run left. Test mode: never the real one.
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/thumbnails");
    QVERIFY2(root.contains(QStringLiteral("/.qttest/")), qPrintable(root));
    QDir(root).removeRecursively();
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("private.png"), 200, 200);
    ThumbnailCache::store(path, 128, ThumbnailCache::render(path, QStringLiteral("image/png"), 128));
    const QString cached = ThumbnailCache::cachePathFor(path, 128);
    QVERIFY(QFile::exists(cached));
    QCOMPARE(modeOf(cached), 0600);
    QCOMPARE(modeOf(QFileInfo(cached).absolutePath()), 0700);
    QCOMPARE(modeOf(QFileInfo(cached).absolutePath() + QStringLiteral("/..")), 0700);

    ThumbnailCache::markFailed(path);
    const QString marker = ThumbnailCache::failMarkerFor(path);
    QVERIFY(QFile::exists(marker));
    QCOMPARE(modeOf(marker), 0600);
    QCOMPARE(modeOf(QFileInfo(marker).absolutePath()), 0700);
    QCOMPARE(modeOf(QFileInfo(marker).absolutePath() + QStringLiteral("/..")), 0700);
    ::umask(previous);
}

void TestThumbnails::cacheDirectoriesAreTightened()
{
    // A cache made under the umask by an older build (or another app) is
    // brought back to 0700 the next time a thumbnail is written into it.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("loose.png"), 100, 100);
    const QString bucket = QFileInfo(ThumbnailCache::cachePathFor(path, 512)).absolutePath();
    QVERIFY(QDir().mkpath(bucket));
    QVERIFY(::chmod(QFile::encodeName(bucket).constData(), 0755) == 0);
    ThumbnailCache::store(path, 512, ThumbnailCache::render(path, QStringLiteral("image/png"), 512));
    QCOMPARE(modeOf(bucket), 0700);
}

void TestThumbnails::cacheWritesLeaveNoTemporaries()
{
    // Written to a temporary and renamed into place: nothing but the
    // finished PNG may be left beside it, and a rewrite replaces it whole.
    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("atomic.png"), 120, 80);
    const QString cached = ThumbnailCache::cachePathFor(path, 1024);
    QFile::remove(cached);
    const QDir bucket(QFileInfo(cached).absolutePath());
    const QStringList before = bucket.exists() ? bucket.entryList(QDir::Files | QDir::Hidden)
                                               : QStringList();
    for (int i = 0; i < 2; ++i)
        ThumbnailCache::store(path, 1024,
                              ThumbnailCache::render(path, QStringLiteral("image/png"), 1024));
    QStringList added = bucket.entryList(QDir::Files | QDir::Hidden);
    for (const QString &name : before)
        added.removeAll(name);
    QCOMPARE(added, QStringList { QFileInfo(cached).fileName() });
    QVERIFY(!ThumbnailCache::loadValid(path, 1024).isNull());
}

void TestThumbnails::onlyCoreFormatsDecodeInProcess()
{
    for (const char *type : { "image/png", "image/jpeg", "image/gif", "image/bmp" })
        QVERIFY2(ThumbnailCache::decodesInProcess(QString::fromLatin1(type)), type);
    // Plugin decoders a hostile file could aim at stay out of the process
    // whenever a sandboxed thumbnailer can take them.
    for (const char *type : { "image/tiff", "image/webp", "image/heif", "image/svg+xml",
                              "image/jp2", "image/x-icns", "image/x-tga" })
        QVERIFY2(!ThumbnailCache::decodesInProcess(QString::fromLatin1(type)), type);
}

void TestThumbnails::refusesAbsurdDimensionsFromTheHeader()
{
    // 100000×100000 is 40GB of pixels: refused before anything is allocated
    // for it — and quickly. (The header check and Qt's allocation limit both
    // stop this one; the header check also covers decoders that allocate
    // outside Qt's accounting.)
    TempTree tree;
    const QString path = writeGiantHeader(tree, QStringLiteral("giant.png"));
    QElapsedTimer timer;
    timer.start();
    QVERIFY(ThumbnailCache::renderImageFile(path, 256).isNull());
    QVERIFY(ThumbnailCache::render(path, QStringLiteral("image/png"), 256).isNull());
    QVERIFY2(timer.elapsed() < 5000, "the header alone must decide");
}

void TestThumbnails::sandboxCommandConfinesTheThumbnailer()
{
    if (QStandardPaths::findExecutable(QStringLiteral("bwrap")).isEmpty()) {
        QVERIFY(ThumbnailCache::sandboxedCommand({ QStringLiteral("x") }, QString(), QString())
                    .isEmpty());
        QSKIP("bwrap is not installed");
    }
    const QStringList command = ThumbnailCache::sandboxedCommand(
        { QStringLiteral("thumbnailer"), QStringLiteral("--flag") },
        QStringLiteral("/data/in.mp4"), QStringLiteral("/tmp/out-dir"));
    QVERIFY(command.first().endsWith(QStringLiteral("/bwrap")));
    for (const char *flag : { "--unshare-all", "--die-with-parent", "--clearenv", "--new-session" })
        QVERIFY2(command.contains(QString::fromLatin1(flag)), flag);

    // The input read-only, the output directory the only writable bind.
    const auto bound = [&](const QString &kind, const QString &path) {
        for (int i = 0; i + 2 < command.size(); ++i) {
            if (command.at(i) == kind && command.at(i + 1) == path && command.at(i + 2) == path)
                return true;
        }
        return false;
    };
    QVERIFY(bound(QStringLiteral("--ro-bind"), QStringLiteral("/data/in.mp4")));
    QVERIFY(bound(QStringLiteral("--bind"), QStringLiteral("/tmp/out-dir")));
    QCOMPARE(command.count(QStringLiteral("--bind")), 1);
    QVERIFY(!command.contains(QStringLiteral("--share-net")));
    QVERIFY(!command.contains(QDir::homePath()));

    // The thumbnailer's own argv comes last, untouched, after "--".
    const int separator = command.indexOf(QStringLiteral("--"));
    QVERIFY(separator > 0);
    QCOMPARE(command.mid(separator + 1),
             QStringList({ QStringLiteral("thumbnailer"), QStringLiteral("--flag") }));
}

void TestThumbnails::thumbnailerRunsInTheSandbox()
{
    if (!ThumbnailCache::sandboxAvailable())
        QSKIP("bwrap cannot build a sandbox here");

    TempTree tree;
    const QString path = writeImage(tree, QStringLiteral("input.png"), 300, 200);

    // A thumbnailer that probes its cage before doing honest work: it must
    // see no session environment, no network but loopback and no home
    // directory. Writing beside its input lands in the sandbox's own tmpfs
    // (bwrap builds the input's parents there), never in the real folder.
    qputenv("OMANTA_TEST_SECRET", "leaked");
    const QString probe = QStringLiteral(
        "[ -z \"$OMANTA_TEST_SECRET\" ] || exit 3; "
        "[ \"$(grep -c : /proc/net/dev)\" -eq 1 ] || exit 4; "
        "[ ! -e \"$3\" ] || exit 5; "
        "touch \"$(dirname \"$1\")/escaped\" 2>/dev/null; "
        "cp \"$1\" \"$2\"");
    const QImage thumb = ThumbnailCache::runThumbnailer(
        { QStringLiteral("/bin/sh"), QStringLiteral("-c"), probe, QStringLiteral("sh"),
          QStringLiteral("%i"), QStringLiteral("%o"), QDir::homePath() },
        path, 128);
    qunsetenv("OMANTA_TEST_SECRET");

    QVERIFY2(!thumb.isNull(), "the sandboxed thumbnailer failed one of its checks");
    QVERIFY(thumb.width() <= 128 && thumb.height() <= 128);
    QVERIFY(!QFile::exists(tree.filePath(QStringLiteral("escaped"))));
}

void TestThumbnails::userInstalledThumbnailersStillRun()
{
    // A thumbnailer living outside /usr (here, a script in a temp "bin", as
    // ~/.local/bin would be) is resolved from the session's PATH and let in
    // read-only on its own.
    TempTree tree;
    const QString script = tree.filePath(QStringLiteral("bin/my-thumbnailer"));
    QDir().mkpath(QFileInfo(script).absolutePath());
    {
        QFile file(script);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("#!/bin/sh\ncp \"$1\" \"$2\"\n");
    }
    QVERIFY(QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                              | QFileDevice::ExeOwner));
    const QByteArray path = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(QFileInfo(script).absolutePath()) + ':' + path);
    const QString input = writeImage(tree, QStringLiteral("in.png"), 200, 100);
    const QImage thumb = ThumbnailCache::runThumbnailer(
        { QStringLiteral("my-thumbnailer"), QStringLiteral("%i"), QStringLiteral("%o") },
        input, 128);
    qputenv("PATH", path);
    QVERIFY2(!thumb.isNull(), "a thumbnailer outside /usr must still run");
}

void TestThumbnails::rejectsHostileThumbnailerOutput()
{
    // Whatever a thumbnailer writes is as untrusted as what it read.
    TempTree tree;

    // An honest PNG comes back, scaled to the bucket.
    const QString honest = writeImage(tree, QStringLiteral("honest.png"), 600, 300);
    const QImage scaled = ThumbnailCache::runThumbnailer(copyingThumbnailer(), honest, 128);
    QVERIFY(!scaled.isNull());
    QVERIFY(scaled.width() <= 128 && scaled.height() <= 128);

    // A header claiming 100000² is refused before decoding.
    const QString giant = writeGiantHeader(tree, QStringLiteral("giant.png"));
    QVERIFY(ThumbnailCache::runThumbnailer(copyingThumbnailer(), giant, 128).isNull());

    // Anything but PNG is refused, whatever plugin could read it.
    QImage bitmap(64, 64, QImage::Format_RGB32);
    bitmap.fill(Qt::red);
    const QString bmp = tree.filePath(QStringLiteral("not-a-png.bmp"));
    QVERIFY(bitmap.save(bmp, "bmp"));
    QVERIFY(ThumbnailCache::runThumbnailer(copyingThumbnailer(), bmp, 128).isNull());

    // A failing thumbnailer yields nothing at all.
    QVERIFY(ThumbnailCache::runThumbnailer({ QStringLiteral("/bin/false") }, honest, 128).isNull());
}

void TestThumbnails::rendersRealFilesThroughTheSandbox()
{
    // The installed thumbnailers must still work inside the cage — a
    // sandbox that breaks them would just trade previews for safety.
    TempTree tree;

    // A TIFF: an exotic type, so a sandboxed thumbnailer takes it when one
    // handles TIFF, and Qt's reader otherwise. Either way it previews.
    QImage picture(320, 200, QImage::Format_RGB32);
    picture.fill(Qt::darkGreen);
    const QString tiff = tree.filePath(QStringLiteral("scan.tiff"));
    if (picture.save(tiff, "tiff")) {
        const QImage thumb = ThumbnailCache::render(tiff, QStringLiteral("image/tiff"), 128);
        QVERIFY2(!thumb.isNull(), "a TIFF must still get a thumbnail");
        QVERIFY(thumb.width() > thumb.height());
    }

    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()
        || !ThumbnailCache::canHandle(QStringLiteral("video/mp4")))
        QSKIP("ffmpeg or a video thumbnailer is not installed");
    const QString video = tree.filePath(QStringLiteral("clip with spaces.mp4"));
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 { QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"),
                   QStringLiteral("lavfi"), QStringLiteral("-i"),
                   QStringLiteral("testsrc=duration=1:size=320x240"), video });
    QVERIFY(ffmpeg.waitForFinished(30000));
    QCOMPARE(ffmpeg.exitCode(), 0);
    const QImage thumb = ThumbnailCache::render(video, QStringLiteral("video/mp4"), 128);
    QVERIFY2(!thumb.isNull(), "a video must still get a thumbnail");
    QVERIFY(thumb.width() <= 128 && thumb.height() <= 128);
}

QTEST_MAIN(TestThumbnails)
#include "tst_thumbnails.moc"
