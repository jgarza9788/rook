#include "ArchiveEngine.h"
#include "Checksum.h"
#include "QuickViewInfo.h"
#include "TestFixture.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QImage>
#include <QSignalSpy>
#include <QTest>

#include <unistd.h>

// The quick view's reading side, and the on-demand checksum it shares with
// Properties and the info panel. What matters: the right kind per file, caps
// that hold, nothing loaded from a markdown file's links.
class TestQuickView : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kindsByContent();
    void textIsCappedWithLineNumbers();
    void binaryIsNotText();
    void markdownLoadsNothing();
    void folderListsAndCaps();
    void archiveListingWritesNothing();
    void asyncPathLandsOnlyTheNewest();
    void checksumMatchesAndCompares();
    void checksumCancels();

private:
    static bool zip(const QStringList &sources, const QString &archive, QString *error)
    {
        return ArchiveEngine::compress(sources, archive, error, [] { return false; },
                                       [](qint64, qint64) {});
    }

    static QString write(const TempTree &tree, const QString &name, const QByteArray &bytes)
    {
        const QString path = tree.filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(bytes);
        return path;
    }
};

void TestQuickView::kindsByContent()
{
    TempTree tree;
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(tree.filePath("red.png")));
    write(tree, "notes.txt", "hello\nworld\n");
    write(tree, "README.md", "# Title\n\nbody\n");
    write(tree, "config", "key = value\n"); // no extension: sniffed
    tree.makeDir("folder");
    QString error;
    QVERIFY(zip({ tree.filePath("notes.txt") }, tree.filePath("a.zip"), &error));

    const auto image_ = QuickViewInfo::inspect(tree.filePath("red.png"));
    QCOMPARE(image_.kind, QStringLiteral("image"));
    QCOMPARE(image_.imageWidth, 40);
    QCOMPARE(image_.imageHeight, 20);
    QCOMPARE(QuickViewInfo::inspect(tree.filePath("notes.txt")).kind, QStringLiteral("text"));
    QCOMPARE(QuickViewInfo::inspect(tree.filePath("README.md")).kind, QStringLiteral("markdown"));
    QCOMPARE(QuickViewInfo::inspect(tree.filePath("config")).kind, QStringLiteral("text"));
    QCOMPARE(QuickViewInfo::inspect(tree.filePath("folder")).kind, QStringLiteral("folder"));
    QCOMPARE(QuickViewInfo::inspect(tree.filePath("a.zip")).kind, QStringLiteral("archive"));

    const auto missing = QuickViewInfo::inspect(tree.filePath("nope"));
    QCOMPARE(missing.kind, QStringLiteral("info"));
    QVERIFY(!missing.error.isEmpty());
}

void TestQuickView::textIsCappedWithLineNumbers()
{
    TempTree tree;
    QByteArray many;
    for (int i = 0; i < QuickViewInfo::kTextLines + 50; ++i)
        many += "line\n";
    write(tree, "many.txt", many);
    const auto result = QuickViewInfo::inspect(tree.filePath("many.txt"));
    QCOMPARE(result.kind, QStringLiteral("text"));
    QVERIFY(result.truncated);
    QCOMPARE(result.text.count(QLatin1Char('\n')) + 1, QuickViewInfo::kTextLines);
    QCOMPARE(result.lineNumbers.split(QLatin1Char('\n')).size(), QuickViewInfo::kTextLines);

    write(tree, "big.txt", QByteArray(QuickViewInfo::kTextBytes * 2, 'a'));
    const auto big = QuickViewInfo::inspect(tree.filePath("big.txt"));
    QVERIFY(big.truncated);
    QCOMPARE(big.text.size(), QuickViewInfo::kTextBytes);

    write(tree, "short.txt", "a\r\nb\n");
    const auto crlf = QuickViewInfo::inspect(tree.filePath("short.txt"));
    QVERIFY(!crlf.truncated);
    QCOMPARE(crlf.text, QStringLiteral("a\nb\n"));
}

void TestQuickView::binaryIsNotText()
{
    TempTree tree;
    write(tree, "blob", QByteArray("ELF\0\0\0binary", 13));
    QCOMPARE(QuickViewInfo::inspect(tree.filePath("blob")).kind, QStringLiteral("info"));
}

void TestQuickView::markdownLoadsNothing()
{
    const QString html = QuickViewInfo::safeMarkdownHtml(QStringLiteral(
        "# Hi\n\n![tracker](https://example.com/pixel.png)\n\n"
        "![ref][logo]\n\n[logo]: https://example.com/logo.png\n\n"
        "<img src=\"https://example.com/raw.png\">\n\n"
        "A [link](https://example.com) stays text.\n"));
    QVERIFY(!html.contains(QStringLiteral("<img"), Qt::CaseInsensitive));
    QVERIFY(!html.contains(QStringLiteral("pixel.png")));
    QVERIFY(html.contains(QStringLiteral("tracker"))); // the alt text survives
    QVERIFY(html.contains(QStringLiteral("Hi")));
}

void TestQuickView::folderListsAndCaps()
{
    TempTree tree;
    tree.makeDir("big/sub");
    for (int i = 0; i < QuickViewInfo::kListEntries + 10; ++i)
        tree.writeFile(QStringLiteral("big/f%1.txt").arg(i, 4, 10, QLatin1Char('0')));
    const auto result = QuickViewInfo::inspect(tree.filePath("big"));
    QCOMPARE(result.kind, QStringLiteral("folder"));
    QCOMPARE(result.entryCount, QuickViewInfo::kListEntries + 11);
    QCOMPARE(result.entries.size(), QuickViewInfo::kListEntries);
    QVERIFY(result.truncated);
    // Folders first.
    QCOMPARE(result.entries.first().toMap().value("name").toString(), QStringLiteral("sub"));
}

void TestQuickView::archiveListingWritesNothing()
{
    TempTree tree;
    tree.writeFile("src/a.txt", 100);
    tree.writeFile("src/b.txt", 50);
    QString error;
    QVERIFY(zip({ tree.filePath("src") }, tree.filePath("pack.zip"), &error));

    const QStringList before = QDir(tree.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    QList<ArchiveEngine::ListedEntry> entries;
    bool truncated = false;
    qint64 bytes = 0;
    QVERIFY(ArchiveEngine::list(tree.filePath("pack.zip"), 100, &entries, &truncated, &bytes, &error));
    QCOMPARE(entries.size(), 3); // src/, a, b
    QCOMPARE(bytes, qint64(150));
    QVERIFY(!truncated);
    QCOMPARE(QDir(tree.path()).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot), before);

    entries.clear();
    QVERIFY(ArchiveEngine::list(tree.filePath("pack.zip"), 2, &entries, &truncated, &bytes, &error));
    QCOMPARE(entries.size(), 2);
    QVERIFY(truncated);

    write(tree, "broken.zip", "PK\x03\x04 not really");
    entries.clear();
    QVERIFY(!ArchiveEngine::list(tree.filePath("broken.zip"), 10, &entries, &truncated, &bytes, &error));
    QVERIFY(!error.isEmpty());
}

void TestQuickView::asyncPathLandsOnlyTheNewest()
{
    TempTree tree;
    write(tree, "one.txt", "one");
    write(tree, "two.md", "# two");
    QuickViewInfo info;
    info.setPath(tree.filePath("one.txt"));
    info.setPath(tree.filePath("two.md"));
    QVERIFY(info.loading());
    QTRY_VERIFY(!info.loading());
    QCOMPARE(info.kind(), QStringLiteral("markdown"));
    QCOMPARE(info.url(), QUrl::fromLocalFile(tree.filePath("two.md")));
}

void TestQuickView::checksumMatchesAndCompares()
{
    TempTree tree;
    const QString path = write(tree, "data.bin", "rook checksum test\n");
    const QString expected = QString::fromLatin1(
        QCryptographicHash::hash("rook checksum test\n", QCryptographicHash::Sha256).toHex());

    Checksum checksum;
    checksum.setPath(path);
    checksum.start();
    QTRY_VERIFY(!checksum.running());
    QCOMPARE(checksum.result(), expected);
    QCOMPARE(checksum.progress(), 1.0);
    QVERIFY(checksum.matches(expected.toUpper()));
    QVERIFY(checksum.matches(expected + QStringLiteral("  data.bin\n"))); // sha256sum output
    QVERIFY(!checksum.matches(QStringLiteral("deadbeef")));

    checksum.setAlgorithm(QStringLiteral("md5"));
    QVERIFY(checksum.result().isEmpty()); // a new algorithm forgets the old answer
    checksum.start();
    QTRY_VERIFY(!checksum.running());
    QCOMPARE(checksum.result(), QString::fromLatin1(
        QCryptographicHash::hash("rook checksum test\n", QCryptographicHash::Md5).toHex()));

    checksum.setPath(tree.filePath("missing"));
    checksum.start();
    QTRY_VERIFY(!checksum.running());
    QVERIFY(checksum.result().isEmpty());
    QVERIFY(!checksum.error().isEmpty());
}

void TestQuickView::checksumCancels()
{
    std::atomic_bool cancelled(true);
    TempTree tree;
    const QString path = write(tree, "x", QByteArray(4 << 20, 'z'));
    QVERIFY(Checksum::compute(path, QCryptographicHash::Sha256, cancelled).isEmpty());
}

QTEST_MAIN(TestQuickView)
#include "tst_quickview.moc"
