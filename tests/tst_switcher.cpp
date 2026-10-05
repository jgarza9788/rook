#include "DefaultFileManager.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <sys/stat.h>

// DefaultFileManager against the real rook-switch script, in a throwaway
// XDG config/data home: nothing here may touch the desktop running the tests.
class TestSwitcher : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();
    void offersTheToggleMenuOnceOnOmarchy();
    void installsIntoAHandWrittenMenu();
    void leavesOtherDesktopsAlone();
    void switchesTheDefaultBothWays();
    void activationFallsBackWhenRookIsGone();
    void keepsSymlinkedConfigsSymlinked();
    void unavailableWithoutTheScript();

private:
    static bool settle(DefaultFileManager &manager);
    QString menuFile() const { return m_home->filePath("config/omarchy/extensions/omarchy-menu.jsonc"); }
    QString bindingsFile() const { return m_home->filePath("config/hypr/bindings.lua"); }
    QString serviceFile() const { return m_home->filePath("data/dbus-1/services/org.freedesktop.FileManager1.service"); }
    // Stubs on PATH; the quote and space prove the Exec= line's quoting.
    QString stubDir() const { return m_home->filePath("bin it's"); }
    QString stubLog() const { return m_home->filePath("stub.log"); }
    QByteArray runActivation(const QByteArray &activation) const;

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestSwitcher::initTestCase()
{
    if (QStandardPaths::findExecutable("xdg-mime").isEmpty()
        || QStandardPaths::findExecutable("perl").isEmpty())
        QSKIP("rook-switch needs xdg-mime and perl");
    // No Hyprland: the script would otherwise reload the live compositor.
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    qputenv("ROOK_SWITCH", ROOK_SWITCH_SCRIPT);
}

void TestSwitcher::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    QVERIFY(QDir().mkpath(m_home->filePath("config")));
    QVERIFY(QDir().mkpath(m_home->filePath("data/applications")));
    // The real entry's MimeType list, but an Exec that resolves anywhere:
    // xdg-mime ignores an entry whose binary is not on PATH.
    {
        QFile source(ROOK_DESKTOP_FILE);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray entry = source.readAll();
        QStringList lines = QString::fromUtf8(entry).split(QLatin1Char('\n'));
        for (QString &line : lines) {
            if (line.startsWith(QLatin1String("Exec=")))
                line = QStringLiteral("Exec=/bin/sh %U");
        }
        QFile copy(m_home->filePath("data/applications/rook.desktop"));
        QVERIFY(copy.open(QIODevice::WriteOnly));
        copy.write(lines.join(QLatin1Char('\n')).toUtf8());
    }
    // Stock Omarchy pins Nautilus for folders. Without that, xdg-mime falls
    // back to scanning desktop files and rook, the only one here, wins.
    {
        QFile mimeapps(m_home->filePath("config/mimeapps.list"));
        QVERIFY(mimeapps.open(QIODevice::WriteOnly));
        mimeapps.write("[Default Applications]\ninode/directory=org.gnome.Nautilus.desktop\n");
    }
    // xdg-mime skips a default whose desktop file is not installed.
    {
        QFile nautilus(m_home->filePath("data/applications/org.gnome.Nautilus.desktop"));
        QVERIFY(nautilus.open(QIODevice::WriteOnly));
        nautilus.write("[Desktop Entry]\nType=Application\nName=Files\nExec=/bin/sh\n"
                       "MimeType=inode/directory;\n");
    }
    // The D-Bus activation file names the rook binary; the script looks
    // beside itself, then on PATH, and a dev checkout has it in neither.
    // nautilus and busctl are stubbed too, so neither the fallback nor the
    // bus reload can reach the desktop running the tests.
    QVERIFY(QDir().mkpath(stubDir()));
    for (const char *name : { "rook", "nautilus", "busctl" }) {
        QFile stub(stubDir() + QLatin1Char('/') + QLatin1String(name));
        QVERIFY(stub.open(QIODevice::WriteOnly));
        stub.write("#!/bin/sh\necho \"" + QByteArray(name) + " $*\" >> \"$STUB_LOG\"\n");
        stub.setPermissions(stub.permissions() | QFileDevice::ExeOwner);
    }
    qputenv("STUB_LOG", stubLog().toUtf8());
    qputenv("PATH", (stubDir() + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
    qputenv("XDG_CONFIG_HOME", m_home->filePath("config").toUtf8());
    qputenv("XDG_DATA_HOME", m_home->filePath("data").toUtf8());
    qputenv("XDG_DATA_DIRS", m_home->filePath("data").toUtf8());
    qputenv("ROOK_SETTINGS_FILE", m_home->filePath("settings").toUtf8());
}

void TestSwitcher::cleanup()
{
    m_home.reset();
}

bool TestSwitcher::settle(DefaultFileManager &manager)
{
    // Commands queue; wait for the last to finish and its status to land.
    return QTest::qWaitFor([&] { return !manager.busy() && manager.known(); }, 15000);
}

void TestSwitcher::offersTheToggleMenuOnceOnOmarchy()
{
    QVERIFY(QDir().mkpath(m_home->filePath("config/omarchy")));
    Settings settings;
    DefaultFileManager manager;
    QVERIFY(manager.available());
    QVERIFY(manager.omarchy());
    QVERIFY(settle(manager));
    QVERIFY(!manager.menuInstalled());

    manager.offerToggleMenu(&settings);
    QVERIFY(settle(manager));
    QVERIFY2(manager.menuInstalled(), qPrintable(manager.lastError()));
    QVERIFY(settings.toggleMenuOffered());
    QFile menu(menuFile());
    QVERIFY(menu.open(QIODevice::ReadOnly));
    QVERIFY(menu.readAll().contains("rook-switch toggle"));
    menu.close();

    // Offering only adds the row; the default is still the person's call.
    QVERIFY(!manager.isDefault());
    QVERIFY(!QFile::exists(bindingsFile()));

    // Removed by hand (or from Preferences): a later launch leaves it gone.
    manager.setMenuInstalled(false);
    QVERIFY(settle(manager));
    QVERIFY(!manager.menuInstalled());
    Settings reread;
    DefaultFileManager relaunched;
    QVERIFY(settle(relaunched));
    relaunched.offerToggleMenu(&reread);
    QVERIFY(settle(relaunched));
    QVERIFY(!relaunched.menuInstalled());
}

void TestSwitcher::installsIntoAHandWrittenMenu()
{
    // A hand-written extension file rarely ends its last entry with a comma
    // (issue #26): the row must still go in, and the person's entries stay.
    QVERIFY(QDir().mkpath(m_home->filePath("config/omarchy/extensions")));
    {
        QFile menu(menuFile());
        QVERIFY(menu.open(QIODevice::WriteOnly));
        menu.write("{\n  \"some.entry\": {\"label\":\"A\"},\n  \"other.entry\": {\"label\":\"B\"}\n}\n");
    }
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    manager.setMenuInstalled(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.menuInstalled(), qPrintable(manager.lastError()));

    // Installing again replaces the block rather than adding a second one.
    manager.setMenuInstalled(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.menuInstalled(), qPrintable(manager.lastError()));

    QFile menu(menuFile());
    QVERIFY(menu.open(QIODevice::ReadOnly));
    const QByteArray contents = menu.readAll();
    QCOMPARE(contents.count("rook-switch toggle"), 1);
    QVERIFY(contents.contains("\"some.entry\": {\"label\":\"A\"},\n"));
    QVERIFY(contents.contains("\"other.entry\": {\"label\":\"B\"}\n}"));
}

void TestSwitcher::leavesOtherDesktopsAlone()
{
    Settings settings;
    DefaultFileManager manager;
    QVERIFY(!manager.omarchy());
    manager.offerToggleMenu(&settings);
    QVERIFY(settle(manager));
    QVERIFY(!manager.menuInstalled());
    QVERIFY(!QFile::exists(menuFile()));
    QVERIFY(!settings.toggleMenuOffered());
}

void TestSwitcher::switchesTheDefaultBothWays()
{
    QVERIFY(QDir().mkpath(m_home->filePath("config/hypr")));
    const QByteArray original = "-- user bindings\no.bind(\"SUPER + X\", \"Thing\", \"thing\")\n";
    {
        QFile bindings(bindingsFile());
        QVERIFY(bindings.open(QIODevice::WriteOnly));
        bindings.write(original);
    }
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    QVERIFY(!manager.isDefault());
    QSignalSpy status(&manager, &DefaultFileManager::statusChanged);

    manager.setDefault(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.isDefault(), qPrintable(manager.lastError()));
    QFile bindings(bindingsFile());
    QVERIFY(bindings.open(QIODevice::ReadOnly));
    QVERIFY(bindings.readAll().contains("rook-launch"));
    bindings.close();
    // "Show in folder" with no file manager running activates rook.
    QFile service(serviceFile());
    QVERIFY(service.open(QIODevice::ReadOnly));
    const QByteArray activation = service.readAll();
    service.close();
    QVERIFY(activation.contains("Name=org.freedesktop.FileManager1\n"));
    QCOMPARE(runActivation(activation), QByteArray("rook --service\n"));

    manager.setDefault(false);
    QVERIFY(settle(manager));
    QVERIFY(!manager.isDefault());
    QVERIFY(bindings.open(QIODevice::ReadOnly));
    QCOMPARE(bindings.readAll(), original); // restored byte for byte
    QVERIFY(!QFile::exists(serviceFile())); // Nautilus's own .service applies again
    QVERIFY(status.count() >= 2);
    QVERIFY(manager.lastError().isEmpty());
}

// Runs the Exec= line the way the bus would (shell word rules) and returns
// what the stubs logged.
QByteArray TestSwitcher::runActivation(const QByteArray &activation) const
{
    QFile::remove(stubLog());
    QByteArray exec;
    for (const QByteArray &line : activation.split('\n'))
        if (line.startsWith("Exec="))
            exec = line.mid(5);
    QProcess bus;
    bus.start(QStringLiteral("/bin/sh"),
              { QStringLiteral("-c"), QStringLiteral("eval \"set -- $1\"; exec \"$@\""),
                QStringLiteral("sh"), QString::fromUtf8(exec) });
    if (!bus.waitForFinished())
        return {};
    QFile log(stubLog());
    return log.open(QIODevice::ReadOnly) ? log.readAll() : QByteArray();
}

// Config kept in a dotfiles repo and symlinked into place: switching both
// ways, and adding and removing the menu row, must edit the real file through
// the link — never replace the link with a copy — and keep its mode.
void TestSwitcher::keepsSymlinkedConfigsSymlinked()
{
    const QString dotfiles = m_home->filePath("dotfiles");
    QVERIFY(QDir().mkpath(dotfiles));
    QVERIFY(QDir().mkpath(m_home->filePath("config/hypr")));
    QVERIFY(QDir().mkpath(m_home->filePath("config/omarchy/extensions")));
    const QByteArray bindings = "-- mine\no.bind(\"SUPER + X\", \"Thing\", \"thing\")\n";
    const QByteArray menu = "{\n  \"some.entry\": {\"label\":\"A\"}\n}\n";
    const auto plant = [&](const QString &name, const QByteArray &contents,
                           const QString &linkPath) {
        const QString real = dotfiles + QLatin1Char('/') + name;
        QFile file(real);
        if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size())
            return false;
        file.close();
        return ::chmod(QFile::encodeName(real).constData(), 0640) == 0
            && QFile::link(real, linkPath);
    };
    QVERIFY(plant("bindings.lua", bindings, bindingsFile()));
    QVERIFY(plant("omarchy-menu.jsonc", menu, menuFile()));

    const auto stillLinked = [&](const QString &linkPath, const QString &name) {
        const QFileInfo info(linkPath);
        struct stat st;
        const QString real = dotfiles + QLatin1Char('/') + name;
        return info.isSymLink() && info.symLinkTarget() == real
            && ::stat(QFile::encodeName(real).constData(), &st) == 0
            && (st.st_mode & 0777) == 0640;
    };
    const auto contents = [&](const QString &name) {
        QFile file(dotfiles + QLatin1Char('/') + name);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };

    DefaultFileManager manager;
    QVERIFY(settle(manager));
    manager.setDefault(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.isDefault(), qPrintable(manager.lastError()));
    QVERIFY(stillLinked(bindingsFile(), "bindings.lua"));
    QVERIFY(contents("bindings.lua").contains("rook-launch"));

    manager.setDefault(false);
    QVERIFY(settle(manager));
    QVERIFY(!manager.isDefault());
    QVERIFY2(stillLinked(bindingsFile(), "bindings.lua"), "switching back replaced the symlink");
    QCOMPARE(contents("bindings.lua"), bindings);

    manager.setMenuInstalled(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.menuInstalled(), qPrintable(manager.lastError()));
    QVERIFY(stillLinked(menuFile(), "omarchy-menu.jsonc"));
    QVERIFY(contents("omarchy-menu.jsonc").contains("rook-switch toggle"));

    manager.setMenuInstalled(false);
    QVERIFY(settle(manager));
    QVERIFY(!manager.menuInstalled());
    QVERIFY2(stillLinked(menuFile(), "omarchy-menu.jsonc"), "remove-menu replaced the symlink");
    QCOMPARE(contents("omarchy-menu.jsonc"), menu);
}

// Uninstalled without switching back: the activation file must not keep
// shadowing Nautilus with a binary that is gone.
void TestSwitcher::activationFallsBackWhenRookIsGone()
{
    DefaultFileManager manager;
    QVERIFY(settle(manager));
    manager.setDefault(true);
    QVERIFY(settle(manager));
    QVERIFY2(manager.isDefault(), qPrintable(manager.lastError()));
    QFile service(serviceFile());
    QVERIFY(service.open(QIODevice::ReadOnly));
    const QByteArray activation = service.readAll();
    service.close();

    QVERIFY(QFile::remove(stubDir() + QLatin1String("/rook")));
    const QByteArray ran = runActivation(activation);
    QVERIFY(!QFile::exists(serviceFile()));
    QVERIFY2(ran.contains("busctl --user call org.freedesktop.DBus"), ran.constData());
    QVERIFY2(ran.endsWith("nautilus --gapplication-service\n"), ran.constData());
}

void TestSwitcher::unavailableWithoutTheScript()
{
    qputenv("ROOK_SWITCH", "/nonexistent/rook-switch");
    QVERIFY(QDir().mkpath(m_home->filePath("config/omarchy")));
    Settings settings;
    DefaultFileManager manager;
    QVERIFY(!manager.available());
    manager.offerToggleMenu(&settings);
    manager.setDefault(true);
    QVERIFY(!manager.busy());
    QVERIFY(!settings.toggleMenuOffered());
    QVERIFY(!QFile::exists(menuFile()));
    qputenv("ROOK_SWITCH", ROOK_SWITCH_SCRIPT);
}

QTEST_MAIN(TestSwitcher)
#include "tst_switcher.moc"
