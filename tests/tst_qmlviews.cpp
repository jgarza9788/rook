#include "Application.h"
#include "Clipboard.h"
#include "DirectoryModel.h"
#include "FileOperations.h"
#include "IconImageProvider.h"
#include "Mounter.h"
#include "Platform.h"
#include "ServerStore.h"
#include "Settings.h"
#include "StarredStore.h"
#include "SystemTheme.h"
#include "ThumbnailProvider.h"
#include "TestFixture.h"

#include <QDBusConnection>
#include <QDataStream>
#include <QPainter>
#include <QPdfWriter>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QJSValue>
#include <QMimeData>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <functional>

// Stands in for Sushi 50 on the session bus under a private name, so Space
// can be driven end to end without a preview window ever opening. Like the
// real one it takes (ssbs) and announces only Visible=false.
class FakePreviewer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.NautilusPreviewer2")
    Q_PROPERTY(bool Visible READ visible)

public:
    struct Call { QString uri; bool toggle; };
    QList<Call> calls;
    bool shown = false;

    bool visible() const { return shown; }

public Q_SLOTS:
    void ShowFile(const QString &uri, const QString &, bool closeIfShown, const QString &)
    {
        calls.append({uri, closeIfShown});
        setShown(!(closeIfShown && shown));
    }
    void Close() { setShown(false); }

public:
    // An arrow key pressed inside the preview window. Sushi 50 sends it as
    // (u) although it introspects as (q); both must work.
    template <typename Direction>
    void pressInPreview(Direction direction)
    {
        QDBusMessage event = QDBusMessage::createSignal(
            QStringLiteral("/org/gnome/NautilusPreviewer"),
            QStringLiteral("org.gnome.NautilusPreviewer2"), QStringLiteral("SelectionEvent"));
        event << QVariant::fromValue(direction);
        QDBusConnection::sessionBus().send(event);
    }

private:
    void setShown(bool value)
    {
        if (shown == value)
            return;
        shown = value;
        if (value)
            return; // Sushi 50 never announces an open
        QDBusMessage changed = QDBusMessage::createSignal(
            QStringLiteral("/org/gnome/NautilusPreviewer"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
        changed << QStringLiteral("org.gnome.NautilusPreviewer2")
                << QVariantMap{{QStringLiteral("Visible"), value}} << QStringList();
        QDBusConnection::sessionBus().send(changed);
    }
};

class TestQmlViews : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void selectionAndVirtualDelegates();
    void emptyTrashRefreshesOpenViews();
    void pasteKeepsCopiedFilesOnClipboard();
    void thumbnailsFollowInPlaceEdits();
    void dragPreviewSurvivesItsOwner();
    void spacePreviewsInSushi();
    void tabCloseButtonClosesTab();
    void tabTitlesFollowNavigation();
    void vimKeysFilterAndQuickView();
    void quickViewOptionalKinds();
    void sessionRoundTrip();
    void splitPaneTransfers();
    void longNamesKeepTheirEnds();
    void newTabTakesTheKeys();
    void shortcutsSearch();
    void columnsAndGalleryViews();
    void dragLabelAndSpringLoadedFolders();
    void dragGestureStartsADrag();
    void polishScreenshots();

private:
    QTemporaryDir m_cache;
};

void TestQmlViews::initTestCase()
{
    // Thumbnails are generated here, and must never land in the real
    // ~/.cache/thumbnails shared with every other application.
    QVERIFY(m_cache.isValid());
    qputenv("XDG_CACHE_HOME", m_cache.path().toUtf8());
    // Every window offers the Omarchy Toggle-menu row on first launch; these
    // suites must never edit the real desktop's menu or bindings.
    qputenv("ROOK_SWITCH", "/nonexistent/rook-switch");
    // Space must never reach the real Sushi from a test run.
    qputenv("ROOK_PREVIEWER_SERVICE",
            QByteArray("org.omarchy.rook.TestPreviewer") + QByteArray::number(QCoreApplication::applicationPid()));
}

static QVariant invoke(QObject *object, const char *method)
{
    QVariant result;
    QMetaObject::invokeMethod(object, method, Q_RETURN_ARG(QVariant, result));
    return result.value<QJSValue>().toVariant();
}

// The visual tree includes instantiated view delegates, unlike QObject's
// ownership tree where a Loader or the view can keep them elsewhere.
static QQuickItem *findItem(QQuickItem *item, const char *property, const QVariant &value)
{
    if (item->property(property) == value)
        return item;
    for (QQuickItem *child : item->childItems()) {
        if (auto *found = findItem(child, property, value))
            return found;
    }
    return nullptr;
}

static QQuickItem *findFileRow(QQuickItem *item, const QString &path)
{
    return findItem(item, "filePath", path);
}

static void checkListIconSizing(QQuickWindow *window, QQuickItem *tab,
                                const TempTree &tree, const QStringList &names)
{
    QVERIFY(window);
    window->requestActivate();
    QTRY_VERIFY(window->isActive());
    tab->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("viewMode").toString(), QStringLiteral("list"));
    const QString path = tree.filePath(names.first());
    QTRY_VERIFY(findItem(tab, "previewPath", path));
    auto *preview = findItem(tab, "previewPath", path);
    QCOMPARE(preview->width(), 18);
    QCOMPARE(findFileRow(tab, path)->height(), 30);

    QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(preview->width(), 24);
    QTest::keyClick(window, Qt::Key_Plus, Qt::ControlModifier);
    QTRY_COMPARE(preview->width(), 32);
    QCOMPARE(preview->property("sourceSize").toSize(), QSize(32, 32));
    QTRY_COMPARE(findFileRow(tab, path)->height(), 44);
    for (int i = 0; i < 8; ++i)
        QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(preview->width(), 64);

    QList<QQuickItem *> rows;
    for (const QString &name : names) {
        auto *row = findFileRow(tab, tree.filePath(name));
        QVERIFY(row);
        rows << row;
    }
    std::sort(rows.begin(), rows.end(), [](auto *a, auto *b) {
        return a->property("index").toInt() < b->property("index").toInt();
    });
    for (int i = 1; i < rows.size(); ++i)
        QTRY_COMPARE(rows[i]->y() - rows[i - 1]->y(), rows[i - 1]->height());

    // Clicking an enlarged row and drawing a band from below the list must
    // still operate on precisely the files under the pointer.
    const QPoint target = rows[2]->mapToScene(QPointF(100, rows[2]->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target);
    QTRY_COMPARE(invoke(tab, "selectedPaths").toStringList(),
                 QStringList{rows[2]->property("filePath").toString()});
    const QPoint below = rows.last()->mapToScene(QPointF(200, rows.last()->height() + 20)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, below);
    QTest::mouseMove(window, target, 30);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, target);
    const QStringList lastTwo{rows[2]->property("filePath").toString(),
                              rows[3]->property("filePath").toString()};
    QTRY_COMPARE(invoke(tab, "selectedPaths").toStringList(), lastTwo);

    const QString screenshot = qEnvironmentVariable("ROOK_TEST_SCREENSHOT");
    if (!screenshot.isEmpty())
        QVERIFY(window->grabWindow().save(screenshot));

    auto *options = findItem(window->contentItem(), "tip", QStringLiteral("View options"));
    QVERIFY(options);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        options->mapToScene(QPointF(options->width() / 2, options->height() / 2)).toPoint());
    QTRY_VERIFY(findItem(window->contentItem(), "tip", QStringLiteral("Zoom out (Ctrl+-)")));
    auto *smaller = findItem(window->contentItem(), "tip", QStringLiteral("Zoom out (Ctrl+-)"));
    QTRY_VERIFY(smaller->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        smaller->mapToScene(QPointF(smaller->width() / 2, smaller->height() / 2)).toPoint());
    QTRY_COMPARE(preview->width(), 48);
    QTest::keyClick(window, Qt::Key_Escape);

    // List and grid sizes survive switching independently.
    QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 64);
    QTest::keyClick(window, Qt::Key_Equal, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 80);
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 48);
    for (int i = 0; i < 8; ++i)
        QTest::keyClick(window, Qt::Key_Minus, Qt::ControlModifier);
    QTRY_COMPARE(findItem(tab, "previewPath", path)->width(), 16);
    QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
    QTRY_COMPARE(findItem(tab, "previewPath", path)->width(), 18);
    QTest::keyClick(window, Qt::Key_2, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 80);

    // Sizes are remembered: saved to the settings file, and a new tab (as a
    // new window or a restart would) opens at them rather than the default.
    const auto saved = [] {
        QFile settings(qEnvironmentVariable("ROOK_SETTINGS_FILE"));
        return settings.open(QIODevice::ReadOnly) ? settings.readAll() : QByteArray();
    };
    QTRY_VERIFY(saved().contains("iconZoom=80"));
    QTest::keyClick(window, Qt::Key_T, Qt::ControlModifier);
    QTRY_VERIFY(window->property("currentTab").value<QObject *>() != tab);
    auto *second = window->property("currentTab").value<QObject *>();
    QTRY_COMPARE(second->property("zoom").toInt(), 80);
    QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier);
    QTRY_VERIFY(window->property("currentTab").value<QObject *>() == tab);

    QTest::keyClick(window, Qt::Key_0, Qt::ControlModifier);
    QTRY_COMPARE(tab->property("zoom").toInt(), 64);
}

static void checkDragPreviews(QQuickWindow *window, QQuickItem *tab,
                              const TempTree &tree, const QStringList &names,
                              const QColor &thumbnailColor = {})
{
    const QString path = tree.filePath(names.first());
    const int pressDelay = QGuiApplication::styleHints()->mouseDoubleClickInterval() + 1;
    for (const QString &mode : {QStringLiteral("list"), QStringLiteral("icon")}) {
        tab->setProperty("viewMode", mode);
        if (thumbnailColor.isValid()) {
            // Match the drag's thumbnail request so it can reuse a ready
            // image, avoiding dependence on the decoder's scheduling speed.
            QVERIFY(QMetaObject::invokeMethod(tab, "setZoom", Q_ARG(QVariant, 36)));
        }
        QTRY_VERIFY(findFileRow(tab, path));
        auto *row = findFileRow(tab, path);
        if (thumbnailColor.isValid()) {
            QTRY_VERIFY(findItem(row, "previewPath", path));
            QTRY_COMPARE(findItem(row, "previewPath", path)->property("status").toInt(), 1);
        }
        QTRY_VERIFY(findItem(row, "ready", false));
        auto *drag = findItem(row, "ready", false);
        for (bool multiple : {false, true}) {
            QVERIFY(QMetaObject::invokeMethod(tab, multiple ? "selectAll" : "clearSelection"));
            // In grid view the centre is within the icon/label hit area;
            // in list view it lands in the row, away from its expander.
            const QPoint point = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point, pressDelay);
            QTRY_VERIFY(drag->property("ready").toBool());
            QCOMPARE(drag->property("itemCount").toInt(), multiple ? names.size() : 1);
            // What DragSource will carry once the gesture becomes a drag.
            QVariant carried = drag->property("paths");
            if (carried.canConvert<QJSValue>())
                carried = carried.value<QJSValue>().toVariant();
            const QStringList paths = invoke(tab, "selectedPaths").toStringList();
            QCOMPARE(carried.toStringList(), paths);
            QCOMPARE(paths.size(), multiple ? names.size() : 1);

            auto *grab = qobject_cast<QQuickItemGrabResult *>(
                drag->property("grabResult").value<QObject *>());
            QVERIFY(grab);
            const QImage image = grab->image();
            QVERIFY(!image.isNull());
            const qreal scale = window->devicePixelRatio();
            QVERIFY(image.width() <= 300 * scale);
            QCOMPARE(image.height(), qRound(56 * scale));
            // A grab must contain the card despite its transparent parent.
            QVERIFY(image.pixelColor(image.width() / 2, image.height() / 2).alpha() > 0);
            if (thumbnailColor.isValid())
                QCOMPARE(image.pixelColor(qRound(28 * scale), qRound(28 * scale)), thumbnailColor);
            const QString screenshot = qEnvironmentVariable("ROOK_TEST_DRAG_SCREENSHOT");
            if (!screenshot.isEmpty())
                QVERIFY(image.save(screenshot + "-" + mode + (multiple ? "-multi.png" : "-single.png")));

            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_VERIFY(!drag->property("ready").toBool());
            QVERIFY(drag->property("previewUrl").toUrl().isEmpty());
        }

        // Releasing before the asynchronous grab completes must not leave a
        // stale preview ready for the next press (or start a late drag).
        const QPoint point = row->mapToScene(QPointF(row->width() / 2, row->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point, pressDelay);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
        QTest::qWait(100);
        QVERIFY(!drag->property("ready").toBool());
    }
}


static void checkLiveSelection(QQuickWindow *window, QQuickItem *tab, Settings *settings)
{
    for (const QString &mode : {QStringLiteral("list"), QStringLiteral("icon"), QStringLiteral("tree")}) {
        TempTree tree;
        tree.writeFile("a");
        tree.writeFile("__proto__");
        tree.writeFile("z");
        settings->setUseTreeView(mode == "tree");
        tab->setProperty("viewMode", mode == "icon" ? "icon" : "list");
        tab->setProperty("path", tree.path());
        QTRY_COMPARE(window->property("visibleCount").toInt(), 3);
        QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, "a")));
        tab->setProperty("currentIndex", tab->property("anchorIndex"));
        const auto currentName = [&] {
            auto *files = tab->property("files").value<QObject *>();
            QVariant value;
            QMetaObject::invokeMethod(files, "valueAt", Q_RETURN_ARG(QVariant, value),
                Q_ARG(int, tab->property("currentIndex").toInt()), Q_ARG(QString, QString("name")));
            return value.toString();
        };
        QCOMPARE(currentName(), QStringLiteral("a"));
        tab->setProperty("sortDescending", true);
        QTRY_COMPARE(currentName(), QStringLiteral("a"));
        tree.writeFile("b");
        QTRY_COMPARE(window->property("visibleCount").toInt(), 4);
        QCOMPARE(currentName(), QStringLiteral("a"));
        QVERIFY(QFile::remove(tree.filePath("z")));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 3);
        QCOMPARE(currentName(), QStringLiteral("a"));
        QCOMPARE(invoke(tab, "selectedPaths").toStringList(), QStringList{tree.filePath("a")});
        QVERIFY(QMetaObject::invokeMethod(tab, "toggleSelection", Q_ARG(QVariant, "__proto__")));
        QCOMPARE(tab->property("selectionCount").toInt(), 2);
        QVERIFY(QFile::remove(tree.filePath("a")));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
        QTRY_COMPARE(tab->property("selectionCount").toInt(), 1);
        QCOMPARE(tab->property("currentIndex").toInt(), -1);
        QCOMPARE(invoke(tab, "selectedPaths").toStringList(), QStringList{tree.filePath("__proto__")});
        QVERIFY(QFile::remove(tree.filePath("__proto__")));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
        QTRY_COMPARE(tab->property("selectionCount").toInt(), 0);
        QCOMPARE(tab->property("anchorIndex").toInt(), -1);
        QCOMPARE(tab->property("statusText").toString(), QStringLiteral("1 item"));
        tab->setProperty("sortDescending", false);
    }
    settings->setUseTreeView(false);
}

static void checkTrashFallback(QQmlApplicationEngine &engine, QObject *window, QQuickItem *tab)
{
    TempTree tree;
    const QString selected = tree.writeFile("selected");
    const QString copying = tree.writeFile("copying");
    tab->setProperty("path", tree.path());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, "selected")));
    QObject *dialog = nullptr;
    for (QObject *child : window->findChildren<QObject *>()) {
        if (child->property("message").toString() == "These files can't be moved to the trash.")
            dialog = child;
    }
    QVERIFY(dialog);
    auto *ops = engine.singletonInstance<FileOperations *>("Rook", "FileOperations");
    QVERIFY(ops);
    ops->copy({copying}, "rook-test-unsupported://host/destination");
    QTRY_VERIFY(!ops->busy());
    QVERIFY(!ops->lastError().isEmpty());
    QVERIFY(!dialog->property("visible").toBool());
    const QString unsupported = "rook-test-unsupported://host/original";
    QObject otherWindow;
    ops->trash({unsupported}, &otherWindow);
    QTRY_VERIFY(!ops->busy());
    QVERIFY(!dialog->property("visible").toBool());
    ops->trash({unsupported}, window);
    QTRY_VERIFY(!ops->busy());
    QTRY_VERIFY(dialog->property("visible").toBool());
    const QVariant pending = dialog->property("pending");
    QCOMPARE(pending.metaType() == QMetaType::fromType<QJSValue>()
        ? pending.value<QJSValue>().toVariant().toStringList() : pending.toStringList(),
        QStringList{unsupported});
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QVERIFY(QFileInfo::exists(selected));
    QVERIFY(QFileInfo::exists(copying));
}

// GitHub #6: the information and settings dialogs close from the mouse too —
// their ✕, or a press on the dimmed window around them. A dialog that takes
// input keeps Escape and its own buttons, so a stray click can't discard it.
static QObject *findDialog(QObject *window, const char *type)
{
    for (QObject *child : window->findChildren<QObject *>()) {
        if (QByteArray(child->metaObject()->className()).startsWith(type))
            return child;
    }
    return nullptr;
}

static void checkDialogsCloseByMouse(QQuickWindow *window)
{
    const QPoint outside(4, window->height() - 4);
    for (const char *type : {"AboutDialog", "PreferencesDialog", "ShortcutsDialog",
                             "VisibleColumnsDialog"}) {
        QObject *dialog = findDialog(window, type);
        QVERIFY2(dialog, type);
        auto *close = qobject_cast<QQuickItem *>(dialog->property("closeButton").value<QObject *>());
        QVERIFY2(close, type);

        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY2(dialog->property("opened").toBool(), type);
        QVERIFY2(close->isVisible(), type);
        const QPointF corner = close->mapToScene(QPointF(close->width() / 2, close->height() / 2));
        auto *background = qobject_cast<QQuickItem *>(dialog->property("background").value<QObject *>());
        const QRectF frame(background->mapToScene(QPointF(0, 0)), background->size());
        QVERIFY2(frame.contains(corner), type); // inside the dialog, not off in the dim
        QVERIFY2(corner.x() > frame.center().x() && corner.y() < frame.center().y(), type);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, corner.toPoint());
        QTRY_VERIFY2(!dialog->property("visible").toBool(), type);

        QVERIFY(QMetaObject::invokeMethod(dialog, "open"));
        QTRY_VERIFY2(dialog->property("opened").toBool(), type);
        QVERIFY2(!frame.contains(outside), type);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, outside);
        QTRY_VERIFY2(!dialog->property("visible").toBool(), type);
    }

    QObject *confirm = findDialog(window, "ConfirmDialog");
    QVERIFY(confirm);
    QVERIFY(QMetaObject::invokeMethod(confirm, "open"));
    QTRY_VERIFY(confirm->property("opened").toBool());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, outside);
    QTest::qWait(50);
    QVERIFY(confirm->property("visible").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!confirm->property("visible").toBool());
}

// GitHub #5: wide, F9 (toggleSidebar) flips the saved setting; narrow, the
// sidebar hides itself and toggling slides it over the files without touching
// the setting, and picking a place, a press on the dimmed files or Escape
// slides it away again.
static void checkSidebarNarrowing(QQuickWindow *window, Settings *settings)
{
    const auto visible = [window] { return window->property("sidebarVisible").toBool(); };
    const auto overlay = [window] { return window->property("sidebarOverlayOpen").toBool(); };
    QObject *sidebar = findDialog(window, "Sidebar");
    QVERIFY(sidebar);
    const QSize wide = window->size();
    QVERIFY(wide.width() >= 720);
    QVERIFY(visible());
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(!visible());
    QCOMPARE(settings->showSidebar(), false);
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(visible());
    QCOMPARE(settings->showSidebar(), true);

    window->resize(600, wide.height());
    QTRY_VERIFY(window->property("sidebarNarrow").toBool());
    QVERIFY(!visible());
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(visible() && overlay());
    QCOMPARE(settings->showSidebar(), true);
    const QString path = window->property("currentPath").toString();
    QVERIFY(QMetaObject::invokeMethod(sidebar, "navigateRequested", Q_ARG(QString, path)));
    QVERIFY(!overlay());

    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(overlay());
    QTest::qWait(250); // let the slide finish
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      QPoint(window->width() - 40, window->height() / 2));
    QTRY_VERIFY(!overlay());

    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    QVERIFY(overlay());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!overlay());

    // Opened while narrow, then widened: back to the saved setting.
    QVERIFY(QMetaObject::invokeMethod(window, "toggleSidebar"));
    window->resize(wide);
    QTRY_VERIFY(!window->property("sidebarNarrow").toBool());
    QVERIFY(!overlay());
    QVERIFY(visible());
}

static void collectItems(QQuickItem *item, const char *property, const QVariant &value,
                         QList<QQuickItem *> &found)
{
    if (item->property(property) == value)
        found.append(item);
    for (QQuickItem *child : item->childItems())
        collectItems(child, property, value, found);
}

// Disabled menu entries must look disabled: with nothing selected, Cut is
// off and New Folder is on, and the two must not share a text colour.
static void checkDisabledMenuItemsDim(QQuickWindow *window, QQuickItem *tab, const QString &folder)
{
    QObject *menu = nullptr;
    for (QObject *child : window->findChildren<QObject *>()) {
        if (child->objectName() == QLatin1String("contextMenu"))
            menu = child;
    }
    QVERIFY(menu);
    const QString before = tab->property("path").toString();
    QVERIFY(QMetaObject::invokeMethod(tab, "navigate", Q_ARG(QVariant, folder)));
    QTRY_COMPARE(tab->property("path").toString(), folder);
    QVERIFY(QMetaObject::invokeMethod(tab, "clearSelection"));
    QVERIFY(QMetaObject::invokeMethod(menu, "popup"));
    QTRY_VERIFY(menu->property("opened").toBool());

    auto entry = [menu](const QString &text) -> QQuickItem * {
        const int count = menu->property("count").toInt();
        for (int i = 0; i < count; ++i) {
            QQuickItem *item = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
            if (item && item->property("text").toString() == text)
                return item;
        }
        return nullptr;
    };
    QQuickItem *cut = entry(QStringLiteral("Cut"));
    QQuickItem *newFolder = entry(QStringLiteral("New Folder"));
    QVERIFY(cut && newFolder);
    QVERIFY(!cut->isEnabled());
    QVERIFY(newFolder->isEnabled());
    auto colour = [](QQuickItem *item) {
        auto *label = item->property("contentItem").value<QQuickItem *>();
        return label ? label->property("color").value<QColor>() : QColor();
    };
    const QColor on = colour(newFolder);
    const QColor off = colour(cut);
    QVERIFY(on.isValid() && off.isValid());
    QVERIFY2(on != off, qPrintable(off.name()));

    const QString menuShot = qEnvironmentVariable("ROOK_TEST_MENU_SCREENSHOT");
    if (!menuShot.isEmpty()) {
        // Hover a row, past the open animation, to show the lit state.
        QTest::mouseMove(newFolder->window(),
                         newFolder->mapToScene(QPointF(newFolder->width() / 2, newFolder->height() / 2)).toPoint());
        QTest::qWait(300);
        QVERIFY(newFolder->window()->grabWindow().save(menuShot));
    }

    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    QTRY_VERIFY(!menu->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(tab, "navigate", Q_ARG(QVariant, before)));
    QTRY_COMPARE(tab->property("path").toString(), before);
}

// GitHub #28: the header's + button asks for a new folder's name, and
// greys out where New Folder would do nothing (the virtual views).
static void checkNewFolderButton(QQuickWindow *window, QQuickItem *tab, const QString &folder)
{
    auto *button = findItem(window->contentItem(), "objectName", QStringLiteral("newFolderButton"));
    QVERIFY(button);
    QObject *prompt = nullptr;
    for (QObject *child : window->findChildren<QObject *>()) {
        if (QByteArray(child->metaObject()->className()).startsWith("PromptDialog")
            && child->property("prompt").toString() == QStringLiteral("Name for the new folder"))
            prompt = child;
    }
    QVERIFY(prompt);

    const QString before = tab->property("path").toString();
    QVERIFY(QMetaObject::invokeMethod(tab, "navigate", Q_ARG(QVariant, folder)));
    QTRY_VERIFY(button->isEnabled());
    const QPointF centre = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    QTRY_VERIFY(prompt->property("opened").toBool());
    QCOMPARE(prompt->property("initialText").toString(), QStringLiteral("New Folder"));
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!prompt->property("visible").toBool());

    QVERIFY(QMetaObject::invokeMethod(tab, "navigate", Q_ARG(QVariant, QStringLiteral("starred:///"))));
    QTRY_VERIFY(!button->isEnabled());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    QTest::qWait(100);
    QVERIFY(!prompt->property("visible").toBool());

    QVERIFY(QMetaObject::invokeMethod(tab, "navigate", Q_ARG(QVariant, before)));
    QTRY_COMPARE(tab->property("path").toString(), before);
}

// GitHub #10: clicking the path bar's empty space types a path — in a folder
// and in Starred, where the one crumb leaves the bar nearly all empty.
static void checkPathBarClickEdits(QQuickWindow *window, QQuickItem *tab, const QString &folder)
{
    auto *bar = findItem(window->contentItem(), "objectName", QStringLiteral("pathBar"));
    QVERIFY(bar);
    auto *blank = findItem(bar, "objectName", QStringLiteral("pathBarBlank"));
    QVERIFY(blank);
    const QString before = tab->property("path").toString();
    for (const QString &place : {folder, QStringLiteral("starred:///")}) {
        QVERIFY(QMetaObject::invokeMethod(tab, "navigate", Q_ARG(QVariant, place)));
        QTRY_COMPARE(bar->property("path").toString(), place);
        QVERIFY(!bar->property("editing").toBool());
        // Just inside the bar's right end, left of the kebab: past the crumbs.
        const QPointF end = bar->mapToScene(QPointF(bar->width() - 40, bar->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, end.toPoint());
        QTRY_VERIFY2(bar->property("editing").toBool(), qPrintable(place));
        QCOMPARE(tab->property("path").toString(), place); // edited, not navigated
        QTest::keyClick(window, Qt::Key_Escape);
        QTRY_VERIFY(!bar->property("editing").toBool());
    }
    QVERIFY(QMetaObject::invokeMethod(tab, "navigate", Q_ARG(QVariant, before)));
    QTRY_COMPARE(bar->property("path").toString(), before);
}

// Reported by email 2026-09-27: a long name and its copy were
// indistinguishable. One selected item is named in the status line, and in
// the icon view its whole name is shown over the cells below; a
// multi-selection keeps the count and shows no overlay.
static void checkSelectedNameShown(QQuickItem *tab, const QString &longName, const QString &other)
{
    const auto fullNames = [tab] {
        QList<QQuickItem *> all;
        collectItems(tab, "objectName", QStringLiteral("fullName"), all);
        QList<QQuickItem *> shown;
        for (QQuickItem *item : all) {
            if (item->isVisible())
                shown.append(item);
        }
        return shown;
    };
    tab->setProperty("viewMode", "icon");
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, longName)));
    // The status reads "Loading…" until the listing settles; wait it out.
    QTRY_VERIFY2(tab->property("statusText").toString()
                     .startsWith(QStringLiteral("“") + longName + QStringLiteral("” selected (")),
                 qPrintable(tab->property("statusText").toString()));
    QTRY_COMPARE(fullNames().size(), 1);

    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, other)));
    QTRY_COMPARE(fullNames().size(), 0); // short name: nothing to expand
    QVERIFY(tab->property("statusText").toString().startsWith(QStringLiteral("“") + other));

    QVERIFY(QMetaObject::invokeMethod(tab, "selectAll"));
    QVERIFY(tab->property("statusText").toString().endsWith(QStringLiteral(" selected")));
    QTRY_COMPARE(fullNames().size(), 0);
    QVERIFY(QMetaObject::invokeMethod(tab, "clearSelection"));
    tab->setProperty("viewMode", "list");
}

static void checkMountQuestion(QQuickWindow *window)
{
    auto *mounter = window->findChild<Mounter *>();
    QVERIFY(mounter);
    GMountOperation *operation = mounter->createOperation();
    int reply = -1;
    const auto cleanup = qScopeGuard([&] {
        g_signal_handlers_disconnect_by_data(operation, &reply);
        g_object_unref(operation);
    });
    g_signal_connect(operation, "reply", G_CALLBACK(+[](GMountOperation *, GMountOperationResult result,
                                                        gpointer data) {
        *static_cast<int *>(data) = int(result);
    }), &reply);
    const char *choices[] = {"Reject test host", "Trust test host", nullptr};
    g_signal_emit_by_name(operation, "ask-question", "Verify <untrusted> host fingerprint", choices);
    QTRY_VERIFY(findItem(window->contentItem(), "text", QString("Trust test host")));
    auto *button = findItem(window->contentItem(), "text", QString("Trust test host"));
    QTRY_VERIFY(button->isVisible());
    QTest::qWait(20);
    QCOMPARE(reply, -1); // no trust decision until the user acts
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_ABORTED));
    QTRY_VERIFY(!button->isVisible());
    reply = -1;
    g_signal_emit_by_name(operation, "ask-question", "Verify <untrusted> host fingerprint", choices);
    QTRY_VERIFY(findItem(window->contentItem(), "text", QString("Trust test host")));
    button = findItem(window->contentItem(), "text", QString("Trust test host"));
    QTRY_VERIFY(button->isVisible());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
    QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_HANDLED));
    QCOMPARE(g_mount_operation_get_choice(operation), 1);
    QTRY_VERIFY(!button->isVisible());

    reply = -1;
    g_signal_emit_by_name(operation, "ask-question", "Device disappeared", choices);
    QTRY_VERIFY(findItem(window->contentItem(), "text", QString("Trust test host")));
    button = findItem(window->contentItem(), "text", QString("Trust test host"));
    QTRY_VERIFY(button->isVisible());
    g_signal_emit_by_name(operation, "aborted");
    QTRY_VERIFY(!button->isVisible());
    QCOMPARE(reply, -1);

    g_signal_emit_by_name(operation, "ask-password", "Password", "user", "", G_ASK_PASSWORD_NEED_PASSWORD);
    auto *password = findItem(window->contentItem(), "placeholderText", QString("Password"));
    QVERIFY(password);
    QTRY_VERIFY(password->isVisible());
    password->setProperty("text", "temporary-secret");
    g_signal_emit_by_name(operation, "aborted");
    QTRY_VERIFY(!password->isVisible());
    QCOMPARE(password->property("text").toString(), QString());
    QCOMPARE(reply, -1);
    // Both aborts release the state, allowing the next real prompt to work.
    g_signal_emit_by_name(operation, "ask-password", "Password", "user", "", G_ASK_PASSWORD_NEED_PASSWORD);
    QTRY_VERIFY(password->isVisible());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(reply, int(G_MOUNT_OPERATION_ABORTED));
}

void TestQmlViews::selectionAndVirtualDelegates()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    const QStringList names{QStringLiteral("selected-") + QString(160, QLatin1Char('a')) + ".txt",
                            "constructor", "toString", "__proto__"};
    for (const QString &name : names)
        tree.writeFile(name);
    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QObject *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = child;
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 4);
    for (const QString &name : names) {
        QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, name)));
        QCOMPARE(tab->property("selectionCount").toInt(), 1);
        QCOMPARE(invoke(tab, "selectedPaths").toStringList(), QStringList{tree.filePath(name)});
        QCOMPARE(invoke(tab, "selectedItems").toList().size(), 1);
        QVERIFY(QMetaObject::invokeMethod(tab, "toggleSelection", Q_ARG(QVariant, name)));
        QCOMPARE(tab->property("selectionCount").toInt(), 0);
        QVERIFY(invoke(tab, "selectedPaths").toStringList().isEmpty());
    }
    QVERIFY(QMetaObject::invokeMethod(tab, "selectAll"));
    QCOMPARE(tab->property("selectionCount").toInt(), 4);
    QCOMPARE(invoke(tab, "selectedPaths").toStringList().size(), 4);
    QVERIFY(QMetaObject::invokeMethod(tab, "clearSelection"));
    QVERIFY(invoke(tab, "selectedPaths").toStringList().isEmpty());

    checkSelectedNameShown(tab, names.first(), names.at(1));
    if (QTest::currentTestFailed())
        return;
    checkListIconSizing(qobject_cast<QQuickWindow *>(window), tab, tree, names);
    if (QTest::currentTestFailed())
        return;
    checkDragPreviews(qobject_cast<QQuickWindow *>(window), tab, tree, names);
    if (QTest::currentTestFailed())
        return;

    TempTree photos;
    const QColor thumbnailColor("#31c983");
    QImage photo(80, 40, QImage::Format_RGB32);
    photo.fill(thumbnailColor);
    QVERIFY(photo.save(photos.filePath("photo.png")));
    photos.writeFile("other.txt");
    tab->setProperty("path", photos.path());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    checkDragPreviews(qobject_cast<QQuickWindow *>(window), tab, photos,
                      {"photo.png", "other.txt"}, thumbnailColor);
    if (QTest::currentTestFailed())
        return;

    const QString selected = tree.filePath(names.first());
    auto *stars = engine.singletonInstance<StarredStore *>("Rook", "StarredStore");
    auto *servers = engine.singletonInstance<ServerStore *>("Rook", "ServerStore");
    QVERIFY(stars);
    QVERIFY(servers);
    stars->star({selected});
    const QString server = QStringLiteral("sftp://example.invalid/share");
    servers->add(server);
    for (const QString &mode : {QStringLiteral("icon"), QStringLiteral("list")}) {
        tab->setProperty("viewMode", mode);
        tab->setProperty("path", tree.path());
        tab->setProperty("searchQuery", "selected");
        QTRY_VERIFY_WITH_TIMEOUT(!tab->property("searching").toBool()
            && window->property("visibleCount").toInt() == 1, 10000);
        QTRY_VERIFY(findFileRow(tab, selected));
        tab->setProperty("searchQuery", "");
        tab->setProperty("path", "starred:///");
        QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
        QTRY_VERIFY(findFileRow(tab, selected));
        tab->setProperty("path", "network:///");
        QTRY_VERIFY(window->property("visibleCount").toInt() >= 1);
        QTRY_VERIFY(findFileRow(tab, server));
    }
    tab->setProperty("searchQuery", "");
    // Show-hidden is a setting (#3): the tab follows it, and a toggle in the
    // tab (Ctrl+H, the menus) writes it back for every tab and the next run.
    auto *settings = engine.singletonInstance<Settings *>("Rook", "Settings");
    QVERIFY(settings);
    QCOMPARE(tab->property("showHidden").toBool(), false);
    settings->setShowHiddenFiles(true);
    QTRY_COMPARE(tab->property("showHidden").toBool(), true);
    tab->setProperty("showHidden", false);
    QCOMPARE(settings->showHiddenFiles(), false);
    settings->setShowHiddenFiles(true); // still following after its own write
    QTRY_COMPARE(tab->property("showHidden").toBool(), true);
    settings->setShowHiddenFiles(false);
    QTRY_COMPARE(tab->property("showHidden").toBool(), false);
    checkSidebarNarrowing(qobject_cast<QQuickWindow *>(window), settings);
    if (QTest::currentTestFailed())
        return;
    checkDialogsCloseByMouse(qobject_cast<QQuickWindow *>(window));
    if (QTest::currentTestFailed())
        return;
    checkPathBarClickEdits(qobject_cast<QQuickWindow *>(window), tab, tree.path());
    if (QTest::currentTestFailed())
        return;
    checkNewFolderButton(qobject_cast<QQuickWindow *>(window), tab, tree.path());
    if (QTest::currentTestFailed())
        return;
    checkDisabledMenuItemsDim(qobject_cast<QQuickWindow *>(window), tab, tree.path());
    if (QTest::currentTestFailed())
        return;
    checkMountQuestion(qobject_cast<QQuickWindow *>(window));
    if (QTest::currentTestFailed())
        return;
    checkTrashFallback(engine, window, tab);
    if (QTest::currentTestFailed())
        return;
    checkLiveSelection(qobject_cast<QQuickWindow *>(window), tab,
                       engine.singletonInstance<Settings *>("Rook", "Settings"));

}

void TestQmlViews::emptyTrashRefreshesOpenViews()
{
    if (qEnvironmentVariable("ROOK_TEST_TRASH_SANDBOX") != QLatin1String("1")) {
        const QString bwrap = QStandardPaths::findExecutable("bwrap");
        if (bwrap.isEmpty() || !QFileInfo::exists("/usr/lib/gvfsd-trash"))
            QSKIP("The isolated Trash integration test requires bubblewrap and gvfs");

        // Empty Trash must never touch the developer's files. The child gets
        // a fresh filesystem, home, runtime directory and private D-Bus/GVfs.
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(bwrap, {
            "--unshare-all", "--die-with-parent",
            "--ro-bind", "/usr", "/usr", "--ro-bind", "/etc", "/etc",
            "--symlink", "usr/bin", "/bin", "--symlink", "usr/lib", "/lib",
            "--symlink", "usr/lib", "/lib64", "--proc", "/proc", "--dev", "/dev",
            "--tmpfs", "/tmp", "--dir", "/rook-test-home", "--dir", "/run/test",
            "--setenv", "HOME", "/rook-test-home",
            "--setenv", "XDG_DATA_HOME", "/rook-test-home/.local/share",
            "--setenv", "XDG_CONFIG_HOME", "/rook-test-home/.config",
            "--setenv", "XDG_CACHE_HOME", "/rook-test-home/.cache",
            "--setenv", "XDG_RUNTIME_DIR", "/run/test",
            "--setenv", "ROOK_TEST_TRASH_SANDBOX", "1",
            "--setenv", "QT_QPA_PLATFORM", "offscreen",
            "--unsetenv", "QT_QPA_PLATFORMTHEME",
            "--unsetenv", "DISPLAY", "--unsetenv", "WAYLAND_DISPLAY",
            "--setenv", "QT_QUICK_BACKEND", "software",
            "--ro-bind", QCoreApplication::applicationFilePath(), "/test",
            "--chdir", "/rook-test-home",
            "dbus-run-session", "--", "/test", "emptyTrashRefreshesOpenViews"
        });
        QVERIFY(child.waitForStarted());
        QVERIFY(child.waitForFinished(30000));
        const QByteArray output = child.readAll();
        if (child.exitStatus() != QProcess::NormalExit || child.exitCode() != 0)
            qWarning().noquote() << output;
        QVERIFY2(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                 output.right(2500).constData());
        return;
    }

    QCOMPARE(QDir::homePath(), QStringLiteral("/rook-test-home"));
    QVERIFY(QDir().mkpath(QDir::homePath() + "/.local/share/Trash/files"));
    QVERIFY(QDir().mkpath(QDir::homePath() + "/.local/share/Trash/info"));
    // The original bug calls g_file_get_child(trash, "/") for the root's
    // attribute notification, then leaves the removal batch unfinished.
    g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(QStringLiteral("trash:///"));
    QList<QQuickWindow *> windows;
    for (QObject *child : application.children()) {
        if (auto *window = qobject_cast<QQuickWindow *>(child))
            windows << window;
    }
    QCOMPARE(windows.size(), 1);
    auto *window = windows.first();
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    auto *model = tab->findChild<DirectoryModel *>();
    QVERIFY(model);
    QTRY_VERIFY(!model->loading());
    QVERIFY2(model->errorMessage().isEmpty(), qPrintable(model->errorMessage()));
    QCOMPARE(model->count(), 0);
    QSignalSpy resets(model, &QAbstractItemModel::modelReset);
    auto *ops = engine.singletonInstance<FileOperations *>("Rook", "FileOperations");
    auto *settings = engine.singletonInstance<Settings *>("Rook", "Settings");
    QVERIFY(ops);
    QVERIFY(settings);
    QObject *confirmation = nullptr;
    for (QObject *child : window->findChildren<QObject *>()) {
        if (child->property("confirmText").toString() == QLatin1String("Empty Trash")) {
            confirmation = child;
            break;
        }
    }
    QVERIFY(confirmation);

    for (const QString &mode : {QStringLiteral("list"), QStringLiteral("icon"), QStringLiteral("tree")}) {
        settings->setUseTreeView(mode == QLatin1String("tree"));
        tab->setProperty("viewMode", mode == QLatin1String("icon") ? "icon" : "list");
        TempTree tree(TempTree::UnderHome);
        const QString first = tree.writeFile("first.txt");
        const QString second = tree.writeFile("with spaces.txt");
        tree.writeFile("folder/child.txt");
        ops->trash({first, second, tree.filePath("folder")});
        QTRY_VERIFY(!ops->busy());
        QVERIFY2(ops->lastError().isEmpty(), qPrintable(ops->lastError()));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 3);

        QVERIFY(QMetaObject::invokeMethod(tab, "selectAll"));
        QCOMPARE(tab->property("selectionCount").toInt(), 3);
        QVERIFY(QMetaObject::invokeMethod(confirmation, "open"));
        QTRY_VERIFY(confirmation->property("visible").toBool());
        auto *button = findItem(window->contentItem(), "text", QStringLiteral("Empty Trash"));
        QVERIFY(button);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_VERIFY(!confirmation->property("visible").toBool());
        QTRY_VERIFY(!ops->busy());
        QVERIFY2(ops->lastError().isEmpty(), qPrintable(ops->lastError()));
        QTRY_COMPARE(model->count(), 0);
        QTRY_COMPARE(window->property("visibleCount").toInt(), 0);
        QCOMPARE(tab->property("path").toString(), QStringLiteral("trash:///"));
        QTRY_COMPARE(tab->property("selectionCount").toInt(), 0);
        QCOMPARE(tab->property("statusText").toString(), QStringLiteral("0 items"));
        QCOMPARE(resets.count(), 0);
    }
}

QTEST_MAIN(TestQmlViews)
// Copy, then paste: the copied files stay on the clipboard, so they can be
// pasted again (into another folder, or here as a second copy). Only a cut is
// used up by its paste. The bug this guards cleared the clipboard after every
// paste, because paste() passed "from a cut" as true unconditionally.
void TestQmlViews::pasteKeepsCopiedFilesOnClipboard()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    const QString copied = tree.writeFile("source/copied.txt");
    const QString cut = tree.writeFile("source/cut.txt");
    QVERIFY(QDir().mkpath(tree.filePath("target")));

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.filePath("target"));
    QObject *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = child;
            break;
        }
    }
    QVERIFY(window);
    auto *clipboard = engine.singletonInstance<Clipboard *>("Rook", "Clipboard");
    auto *operations = engine.singletonInstance<QObject *>("Rook", "FileOperations");
    QVERIFY(clipboard);
    QVERIFY(operations);

    clipboard->copyFiles({copied});
    QVERIFY(QMetaObject::invokeMethod(window, "paste"));
    QTRY_VERIFY(QFileInfo::exists(tree.filePath("target/copied.txt")));
    QTRY_VERIFY(!operations->property("busy").toBool());
    QVERIFY(QFileInfo::exists(copied));
    QCOMPARE(clipboard->paths(), QStringList{copied});
    QVERIFY(!clipboard->isCut());

    // A second paste of the same copy lands too. The name is taken now, so
    // it waits on the conflict dialog; answer Keep both, as a person would.
    QVERIFY(QMetaObject::invokeMethod(window, "paste"));
    QTRY_VERIFY(window->property("pendingTransfer").value<QJSValue>().isObject());
    QVERIFY(QMetaObject::invokeMethod(window, "performTransfer",
                                      Q_ARG(QVariant, int(FileOperations::RenameNew))));
    QTRY_COMPARE(QDir(tree.filePath("target")).entryList(QDir::Files).size(), 2);
    QTRY_VERIFY(!operations->property("busy").toBool());
    QCOMPARE(clipboard->paths(), QStringList{copied});

    clipboard->cutFiles({cut});
    // Reported by email 2026-09-27: a cut looked like a copy until the paste.
    // Cut files are dimmed in both views; a copied one never is.
    QVERIFY(clipboard->isCutPath(cut));
    QVERIFY(!clipboard->isCutPath(copied));
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    const auto dimmed = [tab](const QString &path) {
        QQuickItem *delegate = findFileRow(tab, path);
        if (!delegate)
            return -1;
        for (QQuickItem *child : delegate->childItems()) {
            if (qFuzzyCompare(child->opacity(), 0.5))
                return 1;
        }
        return 0;
    };
    tab->setProperty("path", tree.filePath("source"));
    for (const QString &mode : {QStringLiteral("icon"), QStringLiteral("list")}) {
        tab->setProperty("viewMode", mode);
        QTRY_COMPARE(dimmed(cut), 1);
        QTRY_COMPARE(dimmed(copied), 0);
    }
    tab->setProperty("path", tree.filePath("target"));
    QTRY_COMPARE(tab->property("path").toString(), tree.filePath("target"));
    QVERIFY(QMetaObject::invokeMethod(window, "paste"));
    QTRY_VERIFY(QFileInfo::exists(tree.filePath("target/cut.txt")));
    QTRY_VERIFY(!operations->property("busy").toBool());
    QVERIFY(!QFileInfo::exists(cut));
    QVERIFY(clipboard->paths().isEmpty());
    QVERIFY(!clipboard->isCutPath(cut));
}

void TestQmlViews::thumbnailsFollowInPlaceEdits()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());

    // Every edit is stamped with the same whole second: a picture re-saved
    // within the second it was made must still get a fresh preview.
    const QDateTime stamp = QDateTime::currentDateTime().addSecs(-60);
    const auto writePicture = [&](const QString &name, int w, int h) {
        QImage image(w, h, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        QVERIFY(image.save(tree.filePath(name), "png"));
        tree.setModified(name, stamp);
    };
    // '#', '%' and '?' would otherwise be read as URL syntax.
    const QStringList names{QStringLiteral("edited.png"),
                            QStringLiteral("shot #1 100% done?.png")};
    for (const QString &name : names)
        writePicture(name, 120, 40);

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QObject *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = child;
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);

    // Landscape before the edit, portrait after: the painted shape tells the
    // old picture from the new one without reading pixels back.
    // Visible only: a view mode switch leaves the old view's delegates in
    // the tree until the Loader's deferred delete runs.
    const auto preview = [&](const QString &name) {
        const std::function<QQuickItem *(QQuickItem *)> find = [&](QQuickItem *item) -> QQuickItem * {
            if (!item->isVisible())
                return nullptr;
            if (item->property("previewPath") == tree.filePath(name))
                return item;
            for (QQuickItem *child : item->childItems()) {
                if (auto *found = find(child))
                    return found;
            }
            return nullptr;
        };
        return find(tab);
    };
    const auto isThumbnail = [](QQuickItem *item) {
        return item && item->property("source").toUrl().scheme() == QLatin1String("image")
            && item->property("source").toUrl().host() == QLatin1String("thumbnail")
            && item->property("status").toInt() == 1; // Image.Ready
    };
    const auto isLandscape = [](QQuickItem *item) {
        return item->property("paintedWidth").toReal() > item->property("paintedHeight").toReal();
    };

    for (const QString &mode : {QStringLiteral("icon"), QStringLiteral("list")}) {
        tab->setProperty("viewMode", mode);
        for (const QString &name : names) {
            writePicture(name, 120, 40);
            QTRY_VERIFY2(isThumbnail(preview(name)), qPrintable(mode + ": " + name));
            QTRY_VERIFY2(isLandscape(preview(name)), qPrintable(mode + ": " + name));
        }

        // The file monitor alone must bring the new picture in. Same whole
        // second, so it is the size (and millisecond mtime, where the
        // filesystem keeps one) that tells the versions apart; a different
        // pixel count guarantees the bytes differ.
        for (const QString &name : names) {
            const qint64 before = QFileInfo(tree.filePath(name)).size();
            writePicture(name, 40, 160);
            QVERIFY(QFileInfo(tree.filePath(name)).size() != before);
        }
        for (const QString &name : names) {
            QTRY_VERIFY2(isThumbnail(preview(name)), qPrintable(mode + " live: " + name));
            QTRY_VERIFY2(!isLandscape(preview(name)), qPrintable(mode + " live: " + name));
        }

        // And so must Reload, which rebuilds every row from scratch.
        QVERIFY(QMetaObject::invokeMethod(tab, "reload"));
        QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
        for (const QString &name : names) {
            QTRY_VERIFY2(isThumbnail(preview(name)), qPrintable(mode + " reload: " + name));
            QVERIFY2(!isLandscape(preview(name)), qPrintable(mode + " reload: " + name));
        }
    }
}

void TestQmlViews::dragPreviewSurvivesItsOwner()
{
    // Leaving a view right after pressing a file destroys the drag proxy
    // while its deferred capture check is still queued. That check must die
    // with the proxy instead of running against a torn-down scope.
    QTest::failOnWarning(QRegularExpression("TypeError|is not a function|ReferenceError|invalid context"));
    TempTree tree;
    const QString file = tree.writeFile("pressed.txt");
    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);

    // A view delegate, as the list and icon views hold it: navigating away
    // resets the model, which clears the delegate's context straight away
    // and deletes the item later.
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport Rook\n"
                      "ListView { width: 200; height: 200; model: 1\n"
                      "  delegate: FileDrag { width: 10; height: 10; pressed: true; dragging: false } }",
                      QUrl());
    QScopedPointer<QObject> view(component.create());
    QVERIFY2(view, qPrintable(component.errorString()));
    for (int round = 0; round < 20; ++round) {
        view->setProperty("model", 1);
        QQuickItem *drag = nullptr;
        QTRY_VERIFY(QMetaObject::invokeMethod(view.get(), "itemAtIndex",
                        Q_RETURN_ARG(QQuickItem *, drag), Q_ARG(int, 0)) && drag);
        QVERIFY(QMetaObject::invokeMethod(drag, "prepare",
            Q_ARG(QVariant, QStringList{file}), Q_ARG(QVariant, QStringLiteral("pressed.txt")),
            Q_ARG(QVariant, QString()), Q_ARG(QVariant, QString())));
        view->setProperty("model", 0); // leave the view before the check runs
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }
}

// Space previews the selected file through the system previewer (Sushi,
// org.gnome.NautilusPreviewer2): Space again closes it, the preview follows
// the selection while it is up, Space inside a type-ahead name is part of the
// name, and with no previewer the item opens instead with a note saying why.
void TestQmlViews::spacePreviewsInSushi()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("needs a session bus");
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    const QString service = qEnvironmentVariable("ROOK_PREVIEWER_SERVICE");
    FakePreviewer fake;
    QVERIFY(bus.registerObject(QStringLiteral("/org/gnome/NautilusPreviewer"), &fake,
                               QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties));
    QVERIFY(bus.registerService(service));
    const auto cleanup = qScopeGuard([&] {
        bus.unregisterService(service);
        bus.unregisterObject(QStringLiteral("/org/gnome/NautilusPreviewer"));
    });

    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    // This is the Sushi path and the classic type-ahead keys, both opt-in now.
    {
        QFile settings(config.filePath("ROOK_SETTINGS_FILE"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("previewer=sushi\nkeyboardMode=classic\n");
    }
    const QString first = tree.writeFile("alpha.txt");
    const QString second = tree.writeFile("beta notes.md");
    QVERIFY(QDir().mkpath(tree.filePath("folder")));

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 3);
    window->requestActivate();
    tab->forceActiveFocus();
    QTRY_VERIFY(tab->hasActiveFocus());
    auto *previewer = engine.singletonInstance<QObject *>("Rook", "Previewer");
    QVERIFY(previewer);
    const QString firstUri = QUrl::fromLocalFile(first).toString(QUrl::FullyEncoded);
    const QString secondUri = QUrl::fromLocalFile(second).toString(QUrl::FullyEncoded);

    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("alpha.txt"))));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(fake.calls.size(), 1);
    QCOMPARE(fake.calls.last().uri, firstUri);
    QVERIFY(fake.calls.last().toggle);
    QTRY_VERIFY(previewer->property("visible").toBool());

    // It follows the selection while up (the URI is encoded: a space in the name).
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("beta notes.md"))));
    QTRY_COMPARE(fake.calls.size(), 2);
    QCOMPARE(fake.calls.last().uri, secondUri);
    QVERIFY(!fake.calls.last().toggle);

    // An arrow pressed inside the preview (GTK_DIR_LEFT) steps the selection
    // back one item, and the preview follows.
    fake.pressInPreview(uint(4)); // what Sushi 50 actually sends
    QTRY_COMPARE(fake.calls.size(), 3);
    QCOMPARE(fake.calls.last().uri, firstUri);
    QCOMPARE(tab->property("selectionCount").toInt(), 1);
    fake.pressInPreview(ushort(5)); // what it declares: right, forward again
    QTRY_COMPARE(fake.calls.size(), 4);
    QCOMPARE(fake.calls.last().uri, secondUri);

    // Closed from the preview window itself (Escape there): the selection
    // stops driving it.
    QVERIFY(QMetaObject::invokeMethod(&fake, "Close"));
    QTRY_VERIFY(!previewer->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("alpha.txt"))));
    QTest::qWait(100);
    QCOMPARE(fake.calls.size(), 4);

    // Space opens it again, and Space once more closes it.
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(fake.calls.size(), 5);
    QTRY_VERIFY(previewer->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("beta notes.md"))));
    QTRY_COMPARE(fake.calls.size(), 6);
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_COMPARE(fake.calls.size(), 7);
    QVERIFY(fake.calls.last().toggle);
    QTRY_VERIFY(!previewer->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("alpha.txt"))));
    QTest::qWait(100);
    QCOMPARE(fake.calls.size(), 7);

    // Mid type-ahead, Space belongs to the name being typed.
    QTest::keyClick(window, Qt::Key_B);
    QTest::keyClick(window, Qt::Key_Space);
    QTest::qWait(100);
    QCOMPARE(fake.calls.size(), 7);
    QTest::qWait(1000); // the type-ahead prefix times out

    // No previewer: nothing opens — a look-only key must not open or
    // extract (a folder here, so a regression navigates rather than
    // launching an app on the desktop) — and the window says why.
    bus.unregisterService(service);
    QTRY_VERIFY(!previewer->property("visible").toBool()); // its owner left
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("folder"))));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->property("flashText").toString().contains(QStringLiteral("Sushi")));
    QTest::qWait(200);
    QCOMPARE(tab->property("path").toString(), tree.path());
    QCOMPARE(fake.calls.size(), 7);
}

// The tab strip's delegates, in model order. The tab pages carry a tabPath
// too; only a strip delegate has a close button.
static QList<QQuickItem *> tabDelegates(QQuickItem *item)
{
    QList<QQuickItem *> found;
    if (item->property("tabPath").isValid()
        && findItem(item, "text", QStringLiteral("\u00d7")))
        found.append(item);
    for (QQuickItem *child : item->childItems())
        found += tabDelegates(child);
    std::sort(found.begin(), found.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->property("index").toInt() < b->property("index").toInt();
    });
    return found;
}

static QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}

void TestQmlViews::tabCloseButtonClosesTab()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    tree.writeFile("alpha.txt");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    auto *first = window->property("currentTab").value<QObject *>();
    QVERIFY(first);
    window->requestActivate();
    qobject_cast<QQuickItem *>(first)->forceActiveFocus();
    QTRY_VERIFY(qobject_cast<QQuickItem *>(first)->hasActiveFocus());

    // The x closes its own tab, not just switches to it.
    QTest::keyClick(window, Qt::Key_T, Qt::ControlModifier);
    QTRY_COMPARE(window->property("tabCount").toInt(), 2);
    QList<QQuickItem *> tabs;
    QTRY_COMPARE((tabs = tabDelegates(window->contentItem())).size(), 2);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      centreOf(findItem(tabs.at(0), "text", QStringLiteral("×"))));
    QTRY_COMPARE(window->property("tabCount").toInt(), 1);
    QVERIFY(window->property("currentTab").value<QObject *>() != first);

    // The rest of the tab still switches on a left-click and closes on a
    // middle one.
    QTest::keyClick(window, Qt::Key_T, Qt::ControlModifier);
    QTRY_COMPARE(window->property("tabCount").toInt(), 2);
    QTRY_COMPARE((tabs = tabDelegates(window->contentItem())).size(), 2);
    auto *second = window->property("currentTab").value<QObject *>();
    const QPoint label = tabs.at(0)->mapToScene(QPointF(20, tabs.at(0)->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, label);
    QTRY_VERIFY(window->property("currentTab").value<QObject *>() != second);
    QCOMPARE(window->property("tabCount").toInt(), 2);
    QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier, label);
    QTRY_COMPARE(window->property("tabCount").toInt(), 1);
    QCOMPARE(window->property("currentTab").value<QObject *>(), second);
}

// The tab strip's labels, in tab order. A strip delegate is the item with
// both a tabPath and a close button (the tab pages carry a tabPath too).
static QStringList tabStripLabels(QQuickItem *root)
{
    QList<QQuickItem *> strip;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (item->property("tabPath").isValid()
            && findItem(item, "text", QStringLiteral("×")))
            strip.append(item);
        for (QQuickItem *child : item->childItems())
            walk(child);
    };
    walk(root);
    std::sort(strip.begin(), strip.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->property("index").toInt() < b->property("index").toInt();
    });
    QStringList labels;
    for (QQuickItem *delegate : strip) {
        for (QQuickItem *child : delegate->childItems()) {
            const QString text = child->property("text").toString();
            if (!text.isEmpty() && text != QStringLiteral("×"))
                labels.append(text);
        }
    }
    return labels;
}

void TestQmlViews::tabTitlesFollowNavigation()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign.*undefined|TypeError"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    for (const char *folder : {"one", "two", "three"})
        QVERIFY(QDir().mkpath(tree.filePath(folder)));

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    const auto current = [&] { return window->property("currentTab").value<QObject *>(); };
    const auto go = [&](const char *folder) {
        QVERIFY(QMetaObject::invokeMethod(current(), "navigate",
                                          Q_ARG(QVariant, tree.filePath(folder))));
        QTRY_COMPARE(current()->property("path").toString(), tree.filePath(folder));
    };
    const auto openTab = [&](const char *folder) {
        QVERIFY(QMetaObject::invokeMethod(window, "addTab", Q_ARG(QVariant, tree.filePath(folder)),
                                          Q_ARG(QVariant, QVariant())));
    };

    // Browsing with one tab, then opening a second: the first is named for
    // where it is now, not where the window opened.
    go("one");
    openTab("two");
    QTRY_COMPARE(tabStripLabels(window->contentItem()), (QStringList{"one", "two"}));

    // A tab renames as it navigates.
    go("three");
    QTRY_COMPARE(tabStripLabels(window->contentItem()), (QStringList{"one", "three"}));

    // The tab left after closing the first keeps following its own location.
    QVERIFY(QMetaObject::invokeMethod(window, "closeTab", Q_ARG(QVariant, 0)));
    QTRY_COMPARE(window->property("tabCount").toInt(), 1);
    go("two");
    openTab("one");
    QTRY_COMPARE(tabStripLabels(window->contentItem()), (QStringList{"two", "one"}));
}

// The keyboard-first layer end to end: vim movement, the `/` filter with a
// regex, the built-in quick view following j/k, the info panel toggle, and
// classic mode handing letters back to type-ahead.
void TestQmlViews::vimKeysFilterAndQuickView()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|qml:.*Error|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    QVERIFY(QDir().mkpath(tree.filePath("folder")));
    tree.writeFile("alpha.txt");
    tree.writeFile("beta.md");
    tree.writeFile("gamma.log");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QVERIFY(tab);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 4);
    window->requestActivate();
    tab->forceActiveFocus();
    QTRY_VERIFY(tab->hasActiveFocus());
    auto *settings = engine.singletonInstance<Settings *>("Rook", "Settings");
    QVERIFY(settings);
    QCOMPARE(settings->keyboardMode(), QStringLiteral("vim"));
    const auto name = [&] { return window->property("currentName").toString(); };
    const auto type = [&](const QString &text) {
        for (const QChar c : text)
            QTest::keyClick(window, c.toLatin1());
    };

    // List view (in the grid, j/k move by whole rows). Folders first, then
    // names: folder, alpha.txt, beta.md, gamma.log.
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(window->property("viewMode").toString(), QStringLiteral("list"));
    tab->forceActiveFocus();
    QTest::keyClick(window, 'j');
    QTRY_COMPARE(name(), QStringLiteral("folder"));
    QTest::keyClick(window, 'j');
    QCOMPARE(name(), QStringLiteral("alpha.txt"));
    QTest::keyClick(window, 'G');
    QCOMPARE(name(), QStringLiteral("gamma.log"));
    QTest::keyClick(window, 'k');
    QCOMPARE(name(), QStringLiteral("beta.md"));
    QTest::keyClick(window, 'g');
    QCOMPARE(name(), QStringLiteral("folder"));
    // A letter is a command now, not type-ahead: `b` must not jump to beta.
    QCOMPARE(tab->property("selectionCount").toInt(), 1);

    // `/` filters as you type; a regex after re:; Enter keeps it.
    QTest::keyClick(window, '/');
    QTRY_VERIFY(tab->property("filterEditing").toBool());
    type(QStringLiteral("re:^(a|g)"));
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_VERIFY(!tab->property("filterEditing").toBool());
    QVERIFY(tab->hasActiveFocus());
    QCOMPARE(window->property("visibleCount").toInt(), 2);
    QCOMPARE(name(), QStringLiteral("alpha.txt"));
    // A broken pattern says so on the status line and hides everything.
    QTest::keyClick(window, '/');
    QTRY_VERIFY(tab->property("filterEditing").toBool());
    type(QStringLiteral("re:(")); // reopening selects the old text: this replaces it
    QTRY_COMPARE(window->property("visibleCount").toInt(), 0);
    QVERIFY(tab->property("statusText").toString().startsWith(QStringLiteral("Invalid pattern")));
    // Esc in the field clears the filter.
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 4);
    QCOMPARE(tab->property("filterText").toString(), QString());

    // Space: the built-in quick view, no previewer process; j steps on.
    QTest::keyClick(window, 'G');
    QCOMPARE(name(), QStringLiteral("gamma.log"));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->property("quickViewOpen").toBool());
    QCOMPARE(window->property("quickViewPath").toString(), tree.filePath("gamma.log"));
    QTest::keyClick(window, 'k');
    QTRY_COMPARE(window->property("quickViewPath").toString(), tree.filePath("beta.md"));
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->property("quickViewOpen").toBool());
    QTRY_VERIFY(tab->hasActiveFocus());
    // Picture-in-picture: p shrinks the preview to a corner and hands the
    // keys to the files; the card follows j/k; Esc closes it first.
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->property("quickViewOpen").toBool());
    QTest::keyClick(window, 'p');
    QTRY_VERIFY(window->property("quickViewPip").toBool());
    QTRY_VERIFY(tab->hasActiveFocus());
    QTest::keyClick(window, 'j');
    QTRY_COMPARE(window->property("quickViewPath").toString(), tree.filePath("gamma.log"));
    QCOMPARE(name(), QStringLiteral("gamma.log"));
    QTest::keyClick(window, 'P'); // vim P from the files: full size again
    QTRY_VERIFY(!window->property("quickViewPip").toBool());
    QTest::keyClick(window, 'p');
    QTRY_VERIFY(window->property("quickViewPip").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->property("quickViewOpen").toBool());
    QCOMPARE(tab->property("selectionCount").toInt(), 1); // that Esc was the card's

    // Esc with the preview gone clears the selection, one layer at a time.
    QTest::keyClick(window, Qt::Key_Escape);
    QCOMPARE(tab->property("selectionCount").toInt(), 0);

    // The folder kind: a listing plus a background size count.
    QTest::keyClick(window, 'g');
    QCOMPARE(name(), QStringLiteral("folder"));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->property("quickViewOpen").toBool());
    QTest::qWait(200);
    QTest::keyClick(window, Qt::Key_Space); // Space closes too
    QTRY_VERIFY(!window->property("quickViewOpen").toBool());

    // `i` toggles the info panel (remembered in Settings), which follows
    // the current item.
    QTest::keyClick(window, 'i');
    QTRY_VERIFY(settings->showInfoPanel());
    QTest::keyClick(window, 'j');
    QTest::qWait(200);
    QTest::keyClick(window, 'i');
    QTRY_VERIFY(!settings->showInfoPanel());

    // `.` shows hidden files; `s` cycles the sort.
    QTest::keyClick(window, '.');
    QTRY_VERIFY(window->property("showHidden").toBool());
    QTest::keyClick(window, '.');
    QTRY_VERIFY(!window->property("showHidden").toBool());
    QTest::keyClick(window, 's');
    QCOMPARE(window->property("sortKey").toInt(), 3); // ByModified
    QVERIFY(window->property("sortDescending").toBool());

    // Classic keys: letters are type-ahead again.
    settings->setKeyboardMode(QStringLiteral("classic"));
    QTest::keyClick(window, 'b');
    QTRY_COMPARE(name(), QStringLiteral("beta.md"));
}

// The optional renderers (Qt Pdf, Qt Multimedia) load and draw without QML
// errors, and Esc still closes the preview from inside the player.
void TestQmlViews::quickViewOptionalKinds()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    {
        QPdfWriter pdf(tree.filePath("a-doc.pdf"));
        QPainter painter(&pdf);
        painter.drawText(100, 100, QStringLiteral("rook"));
    }
    {
        // 0.2 s of 8-bit mono silence: the smallest valid WAV.
        QFile wav(tree.filePath("b-sound.wav"));
        QVERIFY(wav.open(QIODevice::WriteOnly));
        const QByteArray samples(1600, char(0x80));
        QDataStream out(&wav);
        out.setByteOrder(QDataStream::LittleEndian);
        wav.write("RIFF");
        out << quint32(36 + samples.size());
        wav.write("WAVEfmt ");
        out << quint32(16) << quint16(1) << quint16(1) << quint32(8000) << quint32(8000)
            << quint16(1) << quint16(8);
        wav.write("data");
        out << quint32(samples.size());
        wav.write(samples);
    }

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    window->requestActivate();
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    tab->forceActiveFocus();
    QTRY_VERIFY(tab->hasActiveFocus());

    QTest::keyClick(window, 'g');
    QCOMPARE(window->property("currentName").toString(), QStringLiteral("a-doc.pdf"));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->property("quickViewOpen").toBool());
    QTest::qWait(500);
    QTest::keyClick(window, 'j'); // on to the sound, inside the preview
    QTRY_COMPARE(window->property("quickViewPath").toString(), tree.filePath("b-sound.wav"));
    QTest::qWait(500);
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->property("quickViewOpen").toBool());
}

// Session restore: what a window saves (tabs, split panes, the current tab)
// comes back as it was, and a folder deleted in between is dropped rather
// than restored as an error page.
void TestQmlViews::sessionRoundTrip()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    for (const char *folder : {"one", "two", "three", "gone"})
        QVERIFY(QDir().mkpath(tree.filePath(folder)));

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    const auto windows = [&] {
        QList<QQuickWindow *> found;
        for (QObject *child : application.children()) {
            if (auto *window = qobject_cast<QQuickWindow *>(child); window && child->property("currentTab").isValid())
                found.append(window);
        }
        return found;
    };

    application.openWindow(tree.filePath("one"));
    QTRY_COMPARE(windows().size(), 1);
    QQuickWindow *window = windows().first();
    QVERIFY(QMetaObject::invokeMethod(window, "addTab", Q_ARG(QVariant, tree.filePath("two")),
                                      Q_ARG(QVariant, QVariant())));
    QTRY_COMPARE(window->property("tabCount").toInt(), 2);
    // Split the second tab; its other pane goes to "three".
    auto *slot = window->property("currentSlot").value<QObject *>();
    QVERIFY(QMetaObject::invokeMethod(slot, "restoreSplit", Q_ARG(QVariant, tree.filePath("three")),
                                      Q_ARG(QVariant, 0)));
    QTRY_VERIFY(window->property("splitOpen").toBool());
    QVERIFY(QMetaObject::invokeMethod(window, "addTab", Q_ARG(QVariant, tree.filePath("gone")),
                                      Q_ARG(QVariant, QVariant())));
    QTRY_COMPARE(window->property("tabCount").toInt(), 3);
    QVERIFY(QMetaObject::invokeMethod(window, "cycleTab", Q_ARG(QVariant, 2))); // back to "two"
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("two"));

    const QString json = application.sessionJson();
    QVERIFY(json.contains(QStringLiteral("three")));
    QVERIFY(!json.contains(QLatin1Char('\n')));

    window->close();
    QTRY_COMPARE(windows().size(), 0);
    QVERIFY(QDir(tree.filePath("gone")).removeRecursively());

    QVERIFY(application.restoreSession(json));
    QTRY_COMPARE(windows().size(), 1);
    window = windows().first();
    QTRY_COMPARE(window->property("tabCount").toInt(), 2); // "gone" dropped
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("two"));
    QTRY_VERIFY(window->property("splitOpen").toBool());

    // Nothing usable: nothing opens, and the caller falls back to a window.
    QVERIFY(!application.restoreSession(QStringLiteral("[{\"tabs\":[{\"path\":\"/no/such/dir\"}]}]")));
    QVERIFY(!application.restoreSession(QStringLiteral("not json")));
}

// Split view's F5 / F6: the selection copies / moves into the other pane's
// folder, through the same conflict handling as paste.
void TestQmlViews::splitPaneTransfers()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    QVERIFY(QDir().mkpath(tree.filePath("left")));
    QVERIFY(QDir().mkpath(tree.filePath("right")));
    tree.writeFile("left/a.txt");
    tree.writeFile("left/b.txt");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.filePath("right"));
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    window->requestActivate();
    auto *slot = window->property("currentSlot").value<QObject *>();
    QVERIFY(QMetaObject::invokeMethod(slot, "restoreSplit", Q_ARG(QVariant, tree.filePath("left")),
                                      Q_ARG(QVariant, 1)));
    QTRY_VERIFY(window->property("splitOpen").toBool());
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("left"));
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    tab->forceActiveFocus();

    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("a.txt"))));
    QTest::keyClick(window, Qt::Key_F5);
    QTRY_VERIFY(QFileInfo::exists(tree.filePath("right/a.txt")));
    QVERIFY(QFileInfo::exists(tree.filePath("left/a.txt")));

    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("b.txt"))));
    QTest::keyClick(window, Qt::Key_F6);
    QTRY_VERIFY(QFileInfo::exists(tree.filePath("right/b.txt")));
    QTRY_VERIFY(!QFileInfo::exists(tree.filePath("left/b.txt")));

    // A name already there asks first: the conflict dialog, not a clobber.
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("a.txt"))));
    QTest::keyClick(window, Qt::Key_F5);
    QTest::qWait(200);
    QVERIFY(window->property("pendingTransfer").isValid()
            && !window->property("pendingTransfer").value<QJSValue>().isNull());

    // Ctrl+F6 switches panes (F6 is move now).
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::keyClick(window, Qt::Key_F6, Qt::ControlModifier);
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("right"));
}

// Finder-style shortening: start + "…" + end, within the given lines, and
// untouched when the name already fits.
void TestQmlViews::longNamesKeepTheirEnds()
{
    Platform platform;
    const QString name = QStringLiteral("a very long holiday photo name from the summer trip to the coast (copy).jpg");
    QCOMPARE(platform.elideMiddle(QStringLiteral("short.txt"), 12, 200, 2), QStringLiteral("short.txt"));

    const QString two = platform.elideMiddle(name, 12, 90, 2);
    QVERIFY(two.contains(QChar(0x2026)));
    QVERIFY(two.startsWith(QStringLiteral("a very")));
    QVERIFY(two.endsWith(QStringLiteral(".jpg")));
    QVERIFY(two.size() < name.size());
    QVERIFY(two.size() > 10); // as much as fits, not a stub

    // More room keeps more; one line keeps less.
    QVERIFY(platform.elideMiddle(name, 12, 90, 3).size() > two.size());
    QVERIFY(platform.elideMiddle(name, 12, 90, 1).size() < two.size());
    // Degenerate input is returned as is.
    QCOMPARE(platform.elideMiddle(name, 12, 0, 2), name);
}

// A new tab (Ctrl+T, vim t) or a tab switch must hand the keyboard to the
// tab now showing — not leave it on the one StackLayout just hid.
void TestQmlViews::newTabTakesTheKeys()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    {
        QFile settings(config.filePath("ROOK_SETTINGS_FILE"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("defaultViewMode=list\n");
    }
    tree.writeFile("a.txt");
    tree.writeFile("b.txt");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    window->requestActivate();
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    const auto current = [&] {
        return qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    };
    current()->forceActiveFocus();
    QTRY_VERIFY(current()->hasActiveFocus());

    const auto checkKeysWork = [&] {
        QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
        QTRY_VERIFY(current()->hasActiveFocus());
        QTest::keyClick(window, 'j');
        QTRY_COMPARE(window->property("currentName").toString(), QStringLiteral("a.txt"));
        QTest::keyClick(window, 'j');
        QTRY_COMPARE(window->property("currentName").toString(), QStringLiteral("b.txt"));
    };

    QTest::keyClick(window, Qt::Key_T, Qt::ControlModifier); // Ctrl+T
    QTRY_COMPARE(window->property("tabCount").toInt(), 2);
    checkKeysWork();

    QTest::keyClick(window, 't'); // vim t
    QTRY_COMPARE(window->property("tabCount").toInt(), 3);
    checkKeysWork();

    QTest::keyClick(window, Qt::Key_Tab, Qt::ControlModifier); // switch
    checkKeysWork();

    QTest::keyClick(window, 'q'); // close: the tab left behind takes over
    QTRY_COMPARE(window->property("tabCount").toInt(), 2);
    QTRY_VERIFY(current()->hasActiveFocus());
    QTest::keyClick(window, 'k');
    QTRY_COMPARE(window->property("currentName").toString(), QStringLiteral("a.txt"));
}

// `?` opens every key with the search focused; typing narrows the rows,
// Esc clears the search first and closes on the second press.
void TestQmlViews::shortcutsSearch()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    tree.writeFile("a.txt");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    window->requestActivate();
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
    tab->forceActiveFocus();
    QTRY_VERIFY(tab->hasActiveFocus());

    auto *dialog = window->findChild<QObject *>("shortcutsDialog");
    QVERIFY(dialog);
    const auto rowCount = [&] {
        int rows = 0;
        for (const QVariant &group : dialog->property("groups").toList())
            rows += group.toMap().value("rows").toList().size();
        return rows;
    };
    const int all = rowCount();
    QVERIFY(all > 20);

    QTest::keyClick(window, '?');
    QTRY_VERIFY(dialog->property("opened").toBool());
    for (const char c : QByteArray("new tab"))
        QTest::keyClick(window, c);
    QTRY_VERIFY(rowCount() > 0);
    QVERIFY(rowCount() < 5);
    for (const QVariant &group : dialog->property("groups").toList()) {
        for (const QVariant &row : group.toMap().value("rows").toList()) {
            const QString text = row.toStringList().join(QLatin1Char(' ')).toLower()
                + QLatin1Char(' ') + group.toMap().value("name").toString().toLower();
            QVERIFY2(text.contains("new") && text.contains("tab"), qPrintable(text));
        }
    }

    QTest::keyClick(window, Qt::Key_Escape); // clears the search
    QTRY_COMPARE(rowCount(), all);
    QVERIFY(dialog->property("opened").toBool());
    QTest::keyClick(window, Qt::Key_Escape); // closes
    QTRY_VERIFY(!dialog->property("opened").toBool());
}

static int countByObjectName(QQuickItem *item, const char *name)
{
    int found = item->objectName() == QLatin1String(name) && item->isVisible() ? 1 : 0;
    for (QQuickItem *child : item->childItems())
        found += countByObjectName(child, name);
    return found;
}

// Columns (Finder) and Gallery: one column per ancestor plus the current
// folder and a preview; h goes up selecting where you came from, l goes in;
// the gallery steps with h/l and its preview follows. Space still previews.
void TestQmlViews::columnsAndGalleryViews()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    QVERIFY(QDir().mkpath(tree.filePath("a/b/c")));
    tree.writeFile("a/b/c/deep.txt");
    tree.writeFile("a/b/note.txt");
    tree.writeFile("a/b/photo.txt");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.filePath("a/b"));
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    window->requestActivate();
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 3); // c/, note, photo
    tab->forceActiveFocus();
    QTRY_VERIFY(tab->hasActiveFocus());
    const auto name = [&] { return window->property("currentName").toString(); };

    // ---- Columns ----
    QTest::keyClick(window, Qt::Key_3, Qt::ControlModifier);
    QTRY_COMPARE(window->property("viewMode").toString(), QStringLiteral("columns"));
    const int crumbs = platform.pathCrumbs(tree.filePath("a/b")).size();
    QTRY_COMPARE(countByObjectName(window->contentItem(), "ancestorColumn"), crumbs - 1);
    QCOMPARE(countByObjectName(window->contentItem(), "currentColumn"), 1);

    QTest::keyClick(window, 'j');
    QTRY_COMPARE(name(), QStringLiteral("c"));
    // The preview column shows the current item (a folder: its contents).
    QTRY_COMPARE(tab->property("viewPreviewLocation").toString(), tree.filePath("a/b/c"));
    QTest::keyClick(window, 'l'); // into c: one more column
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("a/b/c"));
    QTRY_COMPARE(countByObjectName(window->contentItem(), "ancestorColumn"), crumbs);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
    QTest::keyClick(window, 'j');
    QTRY_COMPARE(name(), QStringLiteral("deep.txt"));
    QTRY_COMPARE(tab->property("viewPreviewLocation").toString(), tree.filePath("a/b/c/deep.txt"));
    QTest::keyClick(window, 'h'); // back up: c is selected again
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("a/b"));
    QTRY_COMPARE(name(), QStringLiteral("c"));
    QCOMPARE(tab->property("selectionCount").toInt(), 1);
    QTest::keyClick(window, Qt::Key_Left); // ← is h too: up to a, b selected
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("a"));
    QTRY_COMPARE(name(), QStringLiteral("b"));
    QTest::keyClick(window, Qt::Key_Right); // → into b
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("a/b"));
    QTRY_COMPARE(window->property("visibleCount").toInt(), 3);

    // ---- Gallery ----
    QTest::keyClick(window, Qt::Key_4, Qt::ControlModifier);
    QTRY_COMPARE(window->property("viewMode").toString(), QStringLiteral("gallery"));
    QTRY_COMPARE(countByObjectName(window->contentItem(), "galleryStrip"), 1);
    QTest::keyClick(window, 'g');
    QTRY_COMPARE(name(), QStringLiteral("c"));
    QTest::keyClick(window, 'l');
    QTRY_COMPARE(name(), QStringLiteral("note.txt"));
    QTRY_COMPARE(tab->property("viewPreviewLocation").toString(), tree.filePath("a/b/note.txt"));
    QTest::keyClick(window, Qt::Key_Right);
    QTRY_COMPARE(name(), QStringLiteral("photo.txt"));
    QTest::keyClick(window, 'h');
    QTRY_COMPARE(name(), QStringLiteral("note.txt"));
    QTest::keyClick(window, Qt::Key_Space); // the quick view still opens
    QTRY_VERIFY(window->property("quickViewOpen").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->property("quickViewOpen").toBool());

    // Going up in the list view lands on the folder you left, too.
    QTest::keyClick(window, Qt::Key_1, Qt::ControlModifier);
    QTRY_COMPARE(window->property("viewMode").toString(), QStringLiteral("list"));
    QTest::keyClick(window, Qt::Key_Backspace);
    QTRY_COMPARE(window->property("currentPath").toString(), tree.filePath("a"));
    QTRY_COMPARE(name(), QStringLiteral("b"));
}

// A file dragged over the window: the label beside the pointer says what a
// drop does, a folder rested on springs open, and the drop lands where the
// pointer is — through real Qt drag events, as another app would send them.
void TestQmlViews::dragLabelAndSpringLoadedFolders()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    {
        QFile settings(config.filePath("ROOK_SETTINGS_FILE"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("defaultViewMode=list\nspringLoadDelay=0.75\n");
    }
    QVERIFY(QDir().mkpath(tree.filePath("view/inbox/deeper")));
    const QString source = tree.writeFile("outside/report.txt");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.filePath("view"));
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
    QQuickItem *inboxRow = nullptr;
    QTRY_VERIFY((inboxRow = findFileRow(window->contentItem(), tree.filePath("view/inbox"))));
    const QPoint overInbox = centreOf(inboxRow);

    QMimeData mime;
    mime.setUrls({ QUrl::fromLocalFile(source) });
    QDragEnterEvent enter(overInbox, Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, &mime,
                          Qt::LeftButton, Qt::NoModifier);
    QGuiApplication::sendEvent(window, &enter);
    QDragMoveEvent move(overInbox + QPoint(1, 0), Qt::CopyAction | Qt::MoveAction | Qt::LinkAction,
                        &mime, Qt::LeftButton, Qt::NoModifier);
    QGuiApplication::sendEvent(window, &move);

    auto *label = window->findChild<QObject *>("dragLabelText");
    QVERIFY(label);
    QTRY_VERIFY(window->findChild<QObject *>("dragLabel")->property("visible").toBool());
    // Same filesystem, no keys: a move, into the folder under the pointer.
    QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("Move to “inbox”")));

    // One of rook's own drags: a single card the window draws at the
    // pointer — [▣ 5 items | Move] — and no separate badge.
    auto *dragState = engine.singletonInstance<QObject *>("Rook", "DragState");
    QVERIFY(dragState);
    dragState->setProperty("cardText", QStringLiteral("5 items"));
    dragState->setProperty("ownDrag", true);
    auto *card = window->findChild<QQuickItem *>("dragCard");
    QVERIFY(card);
    QTRY_VERIFY(card->isVisible());
    QVERIFY(!window->findChild<QQuickItem *>("dragLabel")->isVisible());
    QCOMPARE(window->findChild<QObject *>("dragCardText")->property("text").toString(), QStringLiteral("5 items"));
    QCOMPARE(window->findChild<QObject *>("dragCardAction")->property("text").toString(), QStringLiteral("Move"));
    const QPointF pointer = window->contentItem()->mapFromScene(QPointF(overInbox + QPoint(1, 0)));
    QCOMPARE(card->x(), pointer.x() - 28);
    QCOMPARE(card->y(), pointer.y() - 28);
    // Trash is destructive: the action turns the error colour.
    QVERIFY(!dragState->property("destructive").toBool());
    dragState->setProperty("action", QStringLiteral("trash"));
    QVERIFY(dragState->property("destructive").toBool());
    QTRY_COMPARE(window->findChild<QObject *>("dragCardAction")->property("text").toString(), QStringLiteral("Trash"));
    QCOMPARE(window->findChild<QObject *>("dragCardAction")->property("color").value<QColor>(),
             card->property("border").value<QObject *>()->property("color").value<QColor>());
    QVERIFY(QMetaObject::invokeMethod(dragState, "refresh")); // back to what the pointer is over
    dragState->setProperty("ownDrag", false);

    // Resting on the folder opens it (spring-loaded), drag still going.
    QTRY_COMPARE_WITH_TIMEOUT(window->property("currentPath").toString(), tree.filePath("view/inbox"), 3000);

    // Drop on the background of the folder now showing.
    QTRY_COMPARE(window->property("visibleCount").toInt(), 1); // deeper/
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    const QPoint empty = tab->mapToScene(QPointF(tab->width() / 2, tab->height() - 40)).toPoint();
    QDragMoveEvent moveEmpty(empty, Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, &mime,
                             Qt::LeftButton, Qt::NoModifier);
    QGuiApplication::sendEvent(window, &moveEmpty);
    QTRY_VERIFY(label->property("text").toString().contains(QStringLiteral("“inbox”")));
    QDropEvent drop(empty, Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, &mime,
                    Qt::LeftButton, Qt::NoModifier);
    QGuiApplication::sendEvent(window, &drop);
    QTRY_VERIFY(QFileInfo::exists(tree.filePath("view/inbox/report.txt")));
    QTRY_VERIFY(!QFileInfo::exists(source)); // a move, as the label said
    QTRY_VERIFY(!window->findChild<QObject *>("dragLabel")->property("visible").toBool());
}

// The real gesture: press a row and move past the threshold. FileDrag must
// hand the drag to DragSource (a QML→C++ call that once failed type
// conversion, so no drag ever started) and hear it finish. Esc ends it.
void TestQmlViews::dragGestureStartsADrag()
{
    QTest::failOnWarning(QRegularExpression("Required property|Cannot assign|TypeError|ReferenceError|Could not convert|\\.qml:\\d+"));
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    {
        QFile settings(config.filePath("ROOK_SETTINGS_FILE"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("defaultViewMode=list\n");
    }
    const QString file = tree.writeFile("drag-me.txt");

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    QTRY_COMPARE(window->property("visibleCount").toInt(), 1);
    QQuickItem *row = nullptr;
    QTRY_VERIFY((row = findFileRow(window->contentItem(), file)));

    auto *dragSource = engine.singletonInstance<QObject *>("Rook", "DragSource");
    QVERIFY(dragSource);
    QSignalSpy finished(dragSource, SIGNAL(finished(int,int)));
    QSignalSpy activeChanged(dragSource, SIGNAL(activeChanged()));

    // Whatever drag loop the platform runs, Esc ends it.
    QTimer escape;
    escape.setInterval(50);
    QObject::connect(&escape, &QTimer::timeout, [window] {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QGuiApplication::sendEvent(window, &press);
    });
    escape.start();

    const int pressDelay = QGuiApplication::styleHints()->mouseDoubleClickInterval() + 1;
    const QPoint start = centreOf(row);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start, pressDelay);
    QTRY_VERIFY(findItem(row, "ready", true));
    for (int step = 1; step <= 6; ++step)
        QTest::mouseMove(window, start + QPoint(step * 8, step * 4), 20);
    QTRY_VERIFY(activeChanged.count() >= 1);   // DragSource took the drag
    QTRY_COMPARE(finished.count(), 1);         // …and it ended
    QVERIFY(!dragSource->property("active").toBool());
    escape.stop();
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(48, 24));
}

// Screenshots of the floating surfaces for a human to look at — dialogs,
// popovers, drop-downs, the quick view. Skipped unless
// ROOK_TEST_POLISH_SHOTS names a directory to write them to.
void TestQmlViews::polishScreenshots()
{
    const QString dir = qEnvironmentVariable("ROOK_TEST_POLISH_SHOTS");
    if (dir.isEmpty())
        QSKIP("set ROOK_TEST_POLISH_SHOTS to a directory to take them");
    TempTree tree;
    QTemporaryDir config;
    for (const char *env : {"ROOK_SETTINGS_FILE", "ROOK_STARRED_FILE",
                           "ROOK_SERVERS_FILE", "ROOK_BOOKMARKS_FILE"})
        qputenv(env, config.filePath(env).toUtf8());
    qputenv("ROOK_COLORS_FILE", config.filePath("missing/parent/colors.toml").toUtf8());
    {
        QFile settings(config.filePath("ROOK_SETTINGS_FILE"));
        QVERIFY(settings.open(QIODevice::WriteOnly));
        settings.write("defaultViewMode=list\n");
        QFile notes(tree.filePath("notes.md"));
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("# Notes\n\nSome **markdown** with a list:\n\n- one\n- two\n");
    }
    tree.writeFile("report.txt", 2048);

    QQmlApplicationEngine engine;
    engine.addImageProvider("fileicon", new IconImageProvider);
    engine.addImageProvider("thumbnail", new ThumbnailProvider);
    Application application(&engine);
    Platform platform;
    SystemTheme theme;
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "App", &application);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Platform", &platform);
    qmlRegisterSingletonInstance("Rook.Runtime", 1, 0, "Theme", &theme);
    application.openWindow(tree.path());
    QQuickWindow *window = nullptr;
    for (QObject *child : application.children()) {
        if (child->property("currentTab").isValid()) {
            window = qobject_cast<QQuickWindow *>(child);
            break;
        }
    }
    QVERIFY(window);
    auto *tab = qobject_cast<QQuickItem *>(window->property("currentTab").value<QObject *>());
    QTRY_COMPARE(window->property("visibleCount").toInt(), 2);
    tab->forceActiveFocus();
    const auto shoot = [&](const char *name) {
        QTest::qWait(350); // past the open motion
        QVERIFY(window->grabWindow().save(dir + "/" + name + ".png"));
    };

    auto *prefs = window->findChild<QObject *>("preferencesDialog");
    QVERIFY(QMetaObject::invokeMethod(prefs, "open"));
    shoot("preferences");
    QVERIFY(QMetaObject::invokeMethod(prefs, "close"));

    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("report.txt"))));
    QTest::keyClick(window, Qt::Key_I, Qt::ControlModifier);
    shoot("properties");
    QVERIFY(QMetaObject::invokeMethod(window->findChild<QObject *>("propertiesDialog"), "close"));

    QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
    auto *filters = window->findChild<QObject *>("filterPopover");
    QVERIFY(QMetaObject::invokeMethod(filters, "open"));
    QTest::qWait(300);
    auto *combo = window->findChild<QObject *>("rangeCombo");
    QVERIFY(QMetaObject::invokeMethod(combo->property("popup").value<QObject *>(), "open"));
    shoot("filter-popover");
    QVERIFY(QMetaObject::invokeMethod(filters, "close"));
    QTest::keyClick(window, Qt::Key_Escape);

    tab->forceActiveFocus();
    QVERIFY(QMetaObject::invokeMethod(tab, "selectOnly", Q_ARG(QVariant, QStringLiteral("notes.md"))));
    QTest::keyClick(window, Qt::Key_Space);
    QTRY_VERIFY(window->property("quickViewOpen").toBool());
    shoot("quickview");
    QTest::keyClick(window, 'p');
    shoot("quickview-pip");
    QTest::keyClick(window, Qt::Key_Escape);
}

#include "tst_qmlviews.moc"
