#pragma once

#include <QObject>
#include <QPointF>
#include <QStringList>
#include <QtQmlIntegration>

// Runs file drags on behalf of the views, from an object that lives as long
// as the app.
//
// A drag used to belong to the row it started from (QML's Drag attached to a
// delegate). Spring-loaded folders change the listing mid-drag, which
// destroys that row — and with it the QDrag and its QMimeData, while
// QDrag::exec was still running. The compositor then asked for the data and
// Qt read freed memory (two SIGSEGVs in data_source_send, 2026-10-04). Owned
// here, the drag outlives whatever view started it.
class DragSource : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool active READ active NOTIFY activeChanged)

public:
    explicit DragSource(QObject *parent = nullptr);

    bool active() const { return m_active; }

    // Starts a drag of `paths` (locations). Returns a token, or 0 if a drag
    // is already running; finished(token, action) reports how it ended. The
    // drag runs on the next event-loop pass — never inside the caller's
    // handler. Its native picture is blank: the window under the pointer
    // draws rook's live card (DragState), which a fixed picture can't be.
    Q_INVOKABLE int start(const QStringList &paths, const QPointF &hotSpot);

Q_SIGNALS:
    void activeChanged();
    // action: Qt::DropAction the target chose (0 when cancelled).
    void finished(int token, int action);

private:
    void exec(int token, const QStringList &paths, const QPoint &hotSpot);

    bool m_active = false;
    int m_token = 0;
};
