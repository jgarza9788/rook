#include "DragSource.h"
#include "Location.h"

#include <QDrag>
#include <QMimeData>
#include <QPixmap>
#include <QTimer>
#include <QUrl>

DragSource::DragSource(QObject *parent)
    : QObject(parent)
{
}

int DragSource::start(const QStringList &paths, const QPointF &hotSpot)
{
    if (paths.isEmpty() || m_active)
        return 0;
    const int token = ++m_token;
    m_active = true;
    Q_EMIT activeChanged();
    // QDrag::exec spins a nested event loop; starting it from inside a QML
    // handler (mid mouse-move delivery) is how Qt Quick itself avoids
    // re-entrancy trouble — it posts too.
    QTimer::singleShot(0, this, [this, token, paths, hotSpot] {
        exec(token, paths, hotSpot.toPoint());
    });
    return token;
}

void DragSource::exec(int token, const QStringList &paths, const QPoint &hotSpot)
{
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    urls.reserve(paths.size());
    for (const QString &path : paths)
        urls.append(Location::isUri(path) ? QUrl(path) : QUrl::fromLocalFile(path));
    mime->setUrls(urls);

    // Parented to this long-lived object: nothing a view does mid-drag can
    // delete it. QDrag takes ownership of the mime data.
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    // A blank 1×1 picture, not a null one — rook draws its own live card
    // over its windows, and a null pixmap is not something every platform
    // drag handles.
    QImage blank(1, 1, QImage::Format_ARGB32_Premultiplied);
    blank.fill(Qt::transparent);
    drag->setPixmap(QPixmap::fromImage(blank));
    drag->setHotSpot(hotSpot);
    const Qt::DropAction action =
        drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::MoveAction);
    drag->deleteLater();

    m_active = false;
    Q_EMIT activeChanged();
    Q_EMIT finished(token, int(action));
}
