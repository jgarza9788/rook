#include "Application.h"
#include "Settings.h"
#include "Location.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QVariantMap>

namespace {

constexpr const char *kWindowUrl = "qrc:/qt/qml/Rook/qml/Main.qml";

} // namespace

Application::Application(QQmlApplicationEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    // A quit with windows still up (logout, `rook --quit` one day): all of
    // them are the session. Closing the last window saves it in
    // windowClosed() instead, before this runs with none left.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        QList<QObject *> open;
        for (const QPointer<QObject> &window : std::as_const(m_windows)) {
            if (window)
                open.append(window);
        }
        if (!open.isEmpty())
            saveSession(open);
    });
}

int Application::windowCount() const
{
    int alive = 0;
    for (const QPointer<QObject> &window : m_windows) {
        if (!window.isNull())
            ++alive;
    }
    return alive;
}

void Application::openWindow(const QString &path, const QString &selectName)
{
    createWindow(path, selectName);
}

QObject *Application::createWindow(const QString &path, const QString &selectName,
                                   const QVariantMap &session)
{
    QString target = Location::clean(path);
    // URIs are taken on trust — a sync stat of a remote location would block,
    // and the model reports an unreachable one properly once the tab is up.
    if (target.isEmpty() || (!Location::isUri(target) && !QFileInfo(target).isDir()))
        target = QDir::homePath();

    QQmlComponent component(m_engine, QUrl(QLatin1String(kWindowUrl)));
    if (component.isError()) {
        for (const QQmlError &error : component.errors())
            qCritical("rook: %s", qUtf8Printable(error.toString()));
        return nullptr;
    }

    QVariantMap initial{
        { QStringLiteral("initialPath"), target },
        { QStringLiteral("initialSelection"), selectName },
    };
    if (!session.isEmpty())
        initial.insert(QStringLiteral("initialSession"), session);

    QObject *window = component.createWithInitialProperties(initial);
    if (!window) {
        qCritical("rook: could not create window");
        return nullptr;
    }

    // Ownership must stay in C++. Handing a window to the QML garbage collector
    // is fatal here: nothing holds a JavaScript reference to it, so the next GC
    // pass reclaims it — and creating a second window is itself enough to
    // trigger one. The symptom is both windows silently vanishing while the
    // process stays alive.
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
    window->setParent(this);

    m_windows.append(window);
    Q_EMIT windowCountChanged();

    raiseWindow(window);
    return window;
}

void Application::raiseWindow(QObject *window)
{
    if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
        quickWindow->show();
        quickWindow->raise();
        quickWindow->requestActivate();
    }
}

void Application::openPaths(const QStringList &paths, bool newWindow)
{
    if (paths.isEmpty()) {
        openWindow(QDir::homePath());
        return;
    }

    for (const QString &path : paths) {
        // A file given directly means "show me this", not "fail" — open its
        // folder with it selected, the way dropping a file on a manager works.
        QString folder = QDir::homePath();
        QString select;

        if (Location::isUri(path)) {
            folder = Location::clean(path);
        } else {
            const QFileInfo info(path);
            if (info.isDir()) {
                folder = info.absoluteFilePath();
            } else if (info.exists()) {
                folder = info.absolutePath();
                select = info.fileName();
            }
        }

        // Without --new-window, a location that is already on screen is raised
        // rather than duplicated. This is what Nautilus does, and why running
        // `rook ~/Projects` twice doesn't leave you with two identical
        // windows to tidy up.
        if (!newWindow) {
            if (QObject *existing = windowShowing(folder)) {
                raiseWindow(existing);
                continue;
            }
        }

        openWindow(folder, select);
    }
}

QObject *Application::windowShowing(const QString &path) const
{
    for (const QPointer<QObject> &window : m_windows) {
        if (window.isNull())
            continue;
        if (window->property("currentPath").toString() == path)
            return window.get();
    }
    return nullptr;
}

namespace {

// The folder a path should be revealed in, and the name to select there. A
// directory reveals itself — its own contents are what "show me this" means.
QString revealFolder(const QString &path)
{
    if (Location::isUri(path))
        return Location::clean(path);
    const QFileInfo info(path);
    return info.isDir() ? info.absoluteFilePath() : info.absolutePath();
}

} // namespace

QStringList Application::revealGrouped(const QStringList &paths,
                                       QHash<QString, QStringList> *byParent) const
{
    // Group by parent so revealing five files from one folder opens one window
    // rather than five identical ones.
    QStringList orderedParents;
    for (const QString &path : paths) {
        const QString parent = revealFolder(path);
        if (!byParent->contains(parent))
            orderedParents.append(parent);
        (*byParent)[parent].append(path);
    }
    return orderedParents;
}

void Application::showItems(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    QHash<QString, QStringList> byParent;
    const QStringList parents = revealGrouped(paths, &byParent);

    for (const QString &parent : parents) {
        const QString first = byParent.value(parent).constFirst();
        openWindow(parent, QFileInfo(first).isDir() ? QString() : QFileInfo(first).fileName());
    }
}

void Application::showItemProperties(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    QHash<QString, QStringList> byParent;
    const QStringList parents = revealGrouped(paths, &byParent);

    for (const QString &parent : parents) {
        const QStringList group = byParent.value(parent);
        const QFileInfo first(group.constFirst());

        // The window this call created, not whichever one happens to be showing
        // the folder — an older window on the same path would swallow the
        // dialog and leave the new one blank.
        QObject *window = createWindow(parent, first.isDir() ? QString() : first.fileName());
        if (!window)
            continue;

        QMetaObject::invokeMethod(window, "showPropertiesFor",
                                  Q_ARG(QVariant, QVariant::fromValue(group)));
    }
}

QVariantMap Application::windowState() const
{
    // The most recently opened window is the one under test; a richer notion of
    // "active" is not worth the bookkeeping until something needs it.
    for (auto it = m_windows.crbegin(); it != m_windows.crend(); ++it) {
        if (it->isNull())
            continue;
        const QObject *window = it->get();
        return QVariantMap{
            { QStringLiteral("path"), window->property("currentPath") },
            { QStringLiteral("selectionCount"), window->property("selectionCount") },
            { QStringLiteral("currentName"), window->property("currentName") },
            { QStringLiteral("viewMode"), window->property("viewMode") },
            { QStringLiteral("showHidden"), window->property("showHidden") },
            { QStringLiteral("sortKey"), window->property("sortKey") },
            { QStringLiteral("sortDescending"), window->property("sortDescending") },
            { QStringLiteral("zoom"), window->property("zoom") },
            { QStringLiteral("visibleCount"), window->property("visibleCount") },
            { QStringLiteral("tabCount"), window->property("tabCount") },
            { QStringLiteral("splitOpen"), window->property("splitOpen") },
            { QStringLiteral("foldersFirst"), window->property("foldersFirst") },
            { QStringLiteral("listColumns"), window->property("listColumns") },
            { QStringLiteral("iconCaptions"), window->property("iconCaptions") },
            { QStringLiteral("currentSizeCell"), window->property("currentSizeCell") },
            { QStringLiteral("currentDepth"), window->property("currentDepth") },
            { QStringLiteral("currentExpanded"), window->property("currentExpanded") },
            { QStringLiteral("operationsCount"), window->property("operationsCount") },
            { QStringLiteral("activePane"), window->property("activePane") },
            { QStringLiteral("preferencesOpen"), window->property("preferencesOpen") },
            { QStringLiteral("propertiesOpen"), window->property("propertiesOpen") },
            { QStringLiteral("propertiesName"), window->property("propertiesName") },
            { QStringLiteral("propertiesCount"), window->property("propertiesCount") },
            { QStringLiteral("propertiesSize"), window->property("propertiesSize") },
            { QStringLiteral("propertiesMode"), window->property("propertiesMode") },
            { QStringLiteral("propertiesTab"), window->property("propertiesTab") },
            { QStringLiteral("sidebarVisible"), window->property("sidebarVisible") },
            { QStringLiteral("placesCount"), window->property("placesCount") },
            { QStringLiteral("searchOpen"), window->property("searchOpen") },
            { QStringLiteral("searchDateRange"), window->property("searchDateRange") },
            { QStringLiteral("searchDateKind"), window->property("searchDateKind") },
            { QStringLiteral("searchTypeFilter"), window->property("searchTypeFilter") },
            { QStringLiteral("searchQuery"), window->property("searchQuery") },
            { QStringLiteral("searchContent"), window->property("searchContent") },
            { QStringLiteral("serverBarVisible"), window->property("serverBarVisible") },
        };
    }
    return {};
}

QVariantMap Application::windowSession(QObject *window)
{
    QVariant state;
    if (!window || !QMetaObject::invokeMethod(window, "sessionState", Q_RETURN_ARG(QVariant, state)))
        return {};
    QVariant value = state;
    if (value.canConvert<QJSValue>())
        value = value.value<QJSValue>().toVariant();
    return value.toMap();
}

QString Application::sessionJson() const
{
    QJsonArray windows;
    for (const QPointer<QObject> &window : m_windows) {
        const QVariantMap state = windowSession(window);
        if (!state.isEmpty())
            windows.append(QJsonObject::fromVariantMap(state));
    }
    return QString::fromUtf8(QJsonDocument(windows).toJson(QJsonDocument::Compact));
}

void Application::saveSession(const QList<QObject *> &windows) const
{
    if (!m_saveSession)
        return;
    QJsonArray saved;
    for (QObject *window : windows) {
        const QVariantMap state = windowSession(window);
        if (!state.isEmpty())
            saved.append(QJsonObject::fromVariantMap(state));
    }
    Settings settings;
    if (!settings.restoreSession())
        return;
    settings.setSessionState(QString::fromUtf8(QJsonDocument(saved).toJson(QJsonDocument::Compact)));
}

bool Application::restoreSession(const QString &json)
{
    const QJsonArray windows = QJsonDocument::fromJson(json.toUtf8()).array();
    int opened = 0;
    for (const QJsonValue &value : windows) {
        QVariantMap session = value.toObject().toVariantMap();
        QVariantList kept;
        for (const QVariant &tabValue : session.value(QStringLiteral("tabs")).toList()) {
            QVariantMap tab = tabValue.toMap();
            const auto usable = [](const QString &location) {
                // A folder deleted since, or an unmounted drive: dropped. URIs
                // are taken on trust, as everywhere else (no sync network stat).
                return !location.isEmpty()
                    && (Location::isUri(location) || QFileInfo(location).isDir());
            };
            if (!usable(tab.value(QStringLiteral("path")).toString()))
                continue;
            if (!usable(tab.value(QStringLiteral("second")).toString()))
                tab.insert(QStringLiteral("split"), false);
            kept.append(tab);
        }
        if (kept.isEmpty())
            continue;
        session.insert(QStringLiteral("tabs"), kept);
        if (createWindow(kept.first().toMap().value(QStringLiteral("path")).toString(), {}, session))
            ++opened;
    }
    return opened > 0;
}

void Application::windowClosed(QObject *window)
{
    // The last window to close is the session to come back to.
    if (windowCount() == 1)
        saveSession({ window });

    m_windows.removeIf([window](const QPointer<QObject> &tracked) {
        return tracked.isNull() || tracked == window;
    });
    Q_EMIT windowCountChanged();

    // The window is still emitting `closing` — tear it down once that returns.
    if (window)
        window->deleteLater();

    if (m_windows.isEmpty())
        QGuiApplication::quit();
}
