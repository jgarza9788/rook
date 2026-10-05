#include "QuickViewInfo.h"

#include "ArchiveEngine.h"
#include "Location.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextDocument>
#include <QThreadPool>

#include <gio/gio.h>

#include <algorithm>

namespace {

bool isMarkdown(const QString &type, const QString &name)
{
    return type == QLatin1String("text/markdown") || type == QLatin1String("text/x-markdown")
        || name.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)
        || name.endsWith(QLatin1String(".markdown"), Qt::CaseInsensitive);
}

bool isTextType(const QString &type)
{
    if (type.isEmpty())
        return false;
    const QByteArray raw = type.toUtf8();
    if (g_content_type_is_a(raw.constData(), "text/plain"))
        return true;
    static const QStringList also = {
        QStringLiteral("application/json"), QStringLiteral("application/xml"),
        QStringLiteral("application/javascript"), QStringLiteral("application/x-shellscript"),
        QStringLiteral("application/toml"), QStringLiteral("application/x-yaml"),
        QStringLiteral("application/x-desktop"), QStringLiteral("application/sql"),
        QStringLiteral("application/x-perl"), QStringLiteral("application/x-ruby"),
        QStringLiteral("application/x-php"), QStringLiteral("application/x-subrip"),
    };
    return also.contains(type);
}

// Text the quick view can show: decodes as UTF-8 and holds no NUL bytes
// (the cheap and reliable "this is binary" test).
bool readText(const QString &path, QuickViewInfo::Result *result)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QByteArray bytes = file.read(QuickViewInfo::kTextBytes + 1);
    if (bytes.contains('\0'))
        return false;
    bool truncated = bytes.size() > QuickViewInfo::kTextBytes;
    if (truncated)
        bytes.truncate(QuickViewInfo::kTextBytes);
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = decoder.decode(bytes);
    // A cut through a multi-byte character at the 256 KB mark is not an
    // encoding error; anything earlier is.
    if (decoder.hasError() && !truncated)
        return false;

    // Cap lines too: one long minified line is fine, 200k short ones are not.
    int lines = 0;
    qsizetype at = 0;
    while ((at = text.indexOf(QLatin1Char('\n'), at)) >= 0) {
        if (++lines >= QuickViewInfo::kTextLines) {
            text.truncate(at);
            truncated = true;
            break;
        }
        ++at;
    }
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const int count = int(text.count(QLatin1Char('\n'))) + 1;
    QStringList numbers;
    numbers.reserve(count);
    for (int i = 1; i <= count; ++i)
        numbers.append(QString::number(i));

    result->text = text;
    result->lineNumbers = numbers.join(QLatin1Char('\n'));
    result->truncated = truncated;
    return true;
}

void listFolder(const QString &path, QuickViewInfo::Result *result)
{
    QDir dir(path);
    QFileInfoList all = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System,
                                          QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    result->entryCount = int(all.size());
    for (const QFileInfo &info : std::as_const(all)) {
        if (result->entries.size() >= QuickViewInfo::kListEntries) {
            result->truncated = true;
            break;
        }
        result->entries.append(QVariantMap{
            { QStringLiteral("name"), info.fileName() },
            { QStringLiteral("isDir"), info.isDir() && !info.isSymLink() },
            { QStringLiteral("size"), info.isFile() ? info.size() : qint64(-1) },
        });
    }
    if (!dir.isReadable())
        result->error = QStringLiteral("This folder cannot be read");
}

void listArchive(const QString &path, QuickViewInfo::Result *result)
{
    QList<ArchiveEngine::ListedEntry> entries;
    bool truncated = false;
    qint64 bytes = 0;
    QString error;
    ArchiveEngine::list(path, QuickViewInfo::kArchiveEntries, &entries, &truncated, &bytes, &error);
    result->error = error;
    result->truncated = truncated;
    result->entryCount = int(entries.size());
    result->entriesBytes = bytes;
    for (const ArchiveEngine::ListedEntry &entry : std::as_const(entries)) {
        result->entries.append(QVariantMap{
            { QStringLiteral("name"), entry.path },
            { QStringLiteral("isDir"), entry.directory },
            { QStringLiteral("size"), entry.directory ? qint64(-1) : entry.size },
        });
    }
}

} // namespace

QuickViewInfo::QuickViewInfo(QObject *parent)
    : QObject(parent)
{
}

bool QuickViewInfo::hasMedia()
{
#ifdef ROOK_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

bool QuickViewInfo::hasPdf()
{
#ifdef ROOK_HAVE_PDF
    return true;
#else
    return false;
#endif
}

QUrl QuickViewInfo::url() const
{
    if (m_path.isEmpty())
        return {};
    return Location::isUri(m_path) ? QUrl(m_path) : QUrl::fromLocalFile(m_path);
}

void QuickViewInfo::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    Q_EMIT pathChanged();

    const quint64 generation = ++m_generation;
    m_loading = !path.isEmpty();
    if (path.isEmpty()) {
        m_result = Result{};
        Q_EMIT loaded();
        return;
    }
    Q_EMIT loaded();

    QPointer<QuickViewInfo> self(this);
    QThreadPool::globalInstance()->start([self, generation, path] {
        Result result = inspect(path);
        QMetaObject::invokeMethod(self.data(), [self, generation, result = std::move(result)] {
            // Only the newest request lands: holding j through a folder
            // fires many, and the slow ones must not overwrite the last.
            if (!self || self->m_generation != generation)
                return;
            self->m_result = result;
            self->m_loading = false;
            Q_EMIT self->loaded();
        }, Qt::QueuedConnection);
    });
}

QuickViewInfo::Result QuickViewInfo::inspect(const QString &location)
{
    Result result;
    GFile *file = Location::make(location);
    GError *error = nullptr;
    GFileInfo *info = g_file_query_info(
        file,
        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE, nullptr, &error);
    g_object_unref(file);
    if (!info) {
        result.name = Location::displayName(location);
        result.error = QString::fromUtf8(error ? error->message : "Could not read this item");
        g_clear_error(&error);
        return result;
    }

    // Generic accessors: the typed getters go CRITICAL on a sparse GFileInfo.
    if (const char *name = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME))
        result.name = QString::fromUtf8(name);
    if (const char *type = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE))
        result.contentType = QString::fromUtf8(type);
    const bool isDir = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_STANDARD_TYPE)
                       == G_FILE_TYPE_DIRECTORY;
    result.size = qint64(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_SIZE));
    if (g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED))
        result.modified = QDateTime::fromSecsSinceEpoch(
            qint64(g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED)));
    g_object_unref(info);
    if (!result.contentType.isEmpty()) {
        char *description = g_content_type_get_description(result.contentType.toUtf8().constData());
        result.typeDescription = QString::fromUtf8(description ? description : "");
        g_free(description);
    }

    if (!Location::isLocal(location))
        return result; // info only — see the header
    const QString path = Location::clean(location);
    const QString &type = result.contentType;

    if (isDir) {
        result.kind = QStringLiteral("folder");
        listFolder(path, &result);
        return result;
    }
    if (type.startsWith(QLatin1String("image/"))) {
        QImageReader reader(path);
        const QSize size = reader.size();
        if (reader.canRead()) {
            result.kind = reader.supportsAnimation() && reader.imageCount() > 1
                ? QStringLiteral("animated") : QStringLiteral("image");
            result.imageWidth = size.width();
            result.imageHeight = size.height();
            return result;
        }
    }
    if (ArchiveEngine::isArchiveContentType(type)) {
        result.kind = QStringLiteral("archive");
        listArchive(path, &result);
        return result;
    }
    if ((type.startsWith(QLatin1String("video/")) || type.startsWith(QLatin1String("audio/")))
        && hasMedia()) {
        result.kind = QStringLiteral("media");
        return result;
    }
    if (type == QLatin1String("application/pdf") && hasPdf()) {
        result.kind = QStringLiteral("pdf");
        return result;
    }
    // Text: by type, or by sniffing anything small enough that has no type
    // worth trusting (configs, logs, extension-less scripts).
    const bool textual = isTextType(type) || isMarkdown(type, result.name)
        || type == QLatin1String("application/octet-stream") || type.isEmpty();
    if (textual && readText(path, &result)) {
        if (isMarkdown(type, result.name)) {
            result.kind = QStringLiteral("markdown");
            result.html = safeMarkdownHtml(result.text);
        } else {
            result.kind = QStringLiteral("text");
        }
    }
    return result;
}

QString QuickViewInfo::safeMarkdownHtml(const QString &markdown)
{
    // ![alt](url "title") and ![alt][ref] → the alt text, in brackets so
    // it reads as "an image was here".
    static const QRegularExpression inlineImage(QStringLiteral("!\\[([^\\]]*)\\]\\([^)]*\\)"));
    static const QRegularExpression refImage(QStringLiteral("!\\[([^\\]]*)\\]\\[[^\\]]*\\]"));
    QString source = markdown;
    source.replace(inlineImage, QStringLiteral("[\\1]"));
    source.replace(refImage, QStringLiteral("[\\1]"));

    QTextDocument document;
    document.setMarkdown(source, QTextDocument::MarkdownFeatures(
                                     QTextDocument::MarkdownDialectGitHub
                                     | QTextDocument::MarkdownNoHTML));
    QString html = document.toHtml();
    // Belt and braces: whatever survived as an <img> (a reference-style
    // definition the regexes missed) is removed rather than trusted.
    static const QRegularExpression imgTag(QStringLiteral("<img\\b[^>]*>"),
                                           QRegularExpression::CaseInsensitiveOption);
    html.remove(imgTag);
    return html;
}
