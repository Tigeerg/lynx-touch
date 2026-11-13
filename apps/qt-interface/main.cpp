/* main.cpp - the standalone Qt panel for debugging on Windows.
 *
 * The real controller will load the panel with a QtVCP loader later; until then this executable
 * starts the same `main.qml` as a normal desktop window, without a console. It adds the QML import
 * paths of the built viewer modules (MockBackend and CNC.Edges) and the plugin directories to PATH,
 * so the 3D controller and the viewer plugin resolve exactly as they do under qmlscene.
 *
 * The repository root is derived from the executable: <repo>/build/qt-interface/lynx-panel.exe.
 * Run `scripts/build-qt-interface.ps1` to build it and `scripts/run-qt-interface.ps1` to start it.
 */
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QResizeEvent>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <QtGlobal>

#include "backend/backend_factory.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <cstdio>
#endif

namespace {

QString repoRoot()
{
    // The exe sits in <repo>/build/qt-interface; the source is <repo>/apps/qt-interface.
    const QDir here(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(here.absoluteFilePath(QStringLiteral("../..")));
}

// The logical canvas keeps its aspect ratio, but the debug window may be scaled down so the
// portrait touch panel fits a developer monitor. panel-host.qml applies the uniform scale to the
// QML item; this view only rejects a changed aspect ratio.
class PanelView : public QQuickView
{
public:
    void setAspect(const QSize &logical)
    {
        if (logical.isValid() && !logical.isEmpty())
            m_aspect = logical;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        if (!m_inResize && m_aspect.isValid() && !m_aspect.isEmpty() && event->size().height() > 0) {
            const int w = event->size().width();
            const int h = event->size().height();
            const int wantedH = qRound(double(w) * m_aspect.height() / m_aspect.width());
            if (wantedH != h) {
                m_inResize = true;
                resize(w, wantedH);
                m_inResize = false;
            }
        }
        QQuickView::resizeEvent(event);
    }

private:
    QSize m_aspect{1024, 600};
    bool m_inResize = false;
};

// QML calls CanvasLock.apply(mode) when [DISPLAY] UI_MODE changes. The window opens at the
// largest scaled size that still fits the available screen (the touch canvas is 864x1536).
class CanvasLock : public QObject
{
    Q_OBJECT
public:
    explicit CanvasLock(PanelView *view) : m_view(view) {}

    Q_INVOKABLE void apply(int mode)
    {
        const QSize logical = mode == 2 ? QSize(864, 1536) : QSize(1024, 600);
        m_view->setAspect(logical);
        m_view->setMinimumSize(QSize(320, 240));
        m_view->setMaximumSize(QSize(16777215, 16777215));
        QSize avail(1280, 720);
        if (QScreen *screen = QGuiApplication::primaryScreen())
            avail = screen->availableGeometry().size();
        const double sx = double(qMax(320, avail.width() - 80)) / logical.width();
        const double sy = double(qMax(240, avail.height() - 120)) / logical.height();
        const double scale = qMin(1.0, qMin(sx, sy));
        m_view->resize(QSize(qRound(logical.width() * scale), qRound(logical.height() * scale)));
    }

private:
    PanelView *m_view = nullptr;
};

void addPathEntry(const QString &dir)
{
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
#ifdef Q_OS_WIN
    const QString path = qEnvironmentVariable("PATH");
    qputenv("PATH", (dir + QLatin1Char(';') + path).toLocal8Bit());
#else
    Q_UNUSED(dir);
#endif
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    // lynx-panel is a GUI-subsystem process, so it is not attached to the console that started it
    // and a startup error would be invisible. Reattach when there is a parent console, so
    // `--backend=linuxcnc` prints its reason when run from `cmd`. Offscreen/CI runs have no console
    // and keep the Qt log only.
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        freopen_s(&stream, "CONOUT$", "w", stderr);
    }
#endif
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Lynx Touch"));
    app.setApplicationVersion(QStringLiteral("0.9.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lynx Touch — панель оператора ЧПУ"));
    parser.addHelpOption();
    const QCommandLineOption touchOption(QStringLiteral("touch"),
                                         QStringLiteral("Открыть сенсорный холст 864x1536"));
    const QCommandLineOption backendOption(QStringLiteral("backend"),
                                           QStringLiteral("Источник данных: sim или linuxcnc"),
                                           QStringLiteral("name"), QStringLiteral("sim"));
    const QCommandLineOption fixtureOption(QStringLiteral("fixture"),
                                           QStringLiteral("JSON-фикстура симулятора"),
                                           QStringLiteral("path"));
    const QCommandLineOption clockOption(QStringLiteral("freeze-clock"),
                                         QStringLiteral("Остановить часы на HH:mm:ss"),
                                         QStringLiteral("time"));
    const QCommandLineOption screenOption(
        QStringLiteral("screen"),
        QStringLiteral("Стартовый экран: pos, prog, offset, tool, alarm, service"),
        QStringLiteral("name"));
    const QCommandLineOption workplaceOption(
        QStringLiteral("workplace"),
        QStringLiteral("Стартовое рабочее место: SETUP, TEST, RUN"), QStringLiteral("name"));
    const QCommandLineOption themeOption(
        QStringLiteral("theme"),
        QStringLiteral("Тема 0..5 (Минимал светлая/тёмная, Светлая, Тёмная, Графит, Мята)"),
        QStringLiteral("number"));
    const QCommandLineOption shotOption(QStringLiteral("shot"),
                                        QStringLiteral("Снять окно в PNG после загрузки"),
                                        QStringLiteral("file"));
    const QCommandLineOption shotDelayOption(QStringLiteral("shot-delay"),
                                             QStringLiteral("Задержка снимка, мс"),
                                             QStringLiteral("ms"), QStringLiteral("800"));
    parser.addOption(touchOption);
    parser.addOption(backendOption);
    parser.addOption(fixtureOption);
    parser.addOption(clockOption);
    parser.addOption(screenOption);
    parser.addOption(workplaceOption);
    parser.addOption(themeOption);
    parser.addOption(shotOption);
    parser.addOption(shotDelayOption);
    parser.process(app);

    const QStringList screens{QStringLiteral("pos"), QStringLiteral("prog"),
                              QStringLiteral("offset"), QStringLiteral("tool"),
                              QStringLiteral("alarm"), QStringLiteral("service")};
    const QStringList workplaces{QStringLiteral("SETUP"), QStringLiteral("TEST"),
                                 QStringLiteral("RUN")};
    const QString requestedScreen = parser.value(screenOption);
    const QString requestedWorkplace = parser.value(workplaceOption);
    if (!requestedScreen.isEmpty() && !screens.contains(requestedScreen)) {
        qWarning("lynx-panel: unknown screen %s", qPrintable(requestedScreen));
        return 1;
    }
    if (!requestedWorkplace.isEmpty() && !workplaces.contains(requestedWorkplace)) {
        qWarning("lynx-panel: unknown workplace %s", qPrintable(requestedWorkplace));
        return 1;
    }
    int requestedTheme = -1;
    if (parser.isSet(themeOption)) {
        bool ok = false;
        requestedTheme = parser.value(themeOption).toInt(&ok);
        if (!ok || requestedTheme < 0 || requestedTheme > 5) {
            qWarning("lynx-panel: theme must be 0..5");
            return 1;
        }
    }
    bool delayOk = false;
    int shotDelay = parser.value(shotDelayOption).toInt(&delayOk);
    if (!delayOk)
        shotDelay = 800;
    shotDelay = qBound(0, shotDelay, 10000);

    const QString root = repoRoot();
    const QString mainFile = root + QStringLiteral("/apps/qt-interface/main.qml");
    if (!QFileInfo::exists(mainFile)) {
        qWarning("lynx-panel: main.qml not found: %s", qPrintable(mainFile));
        return 1;
    }
    // A screenshot loads main.qml directly: the root then has no Loader and the state can be set
    // before the frame. The normal launch keeps panel-host and its uniform scaling.
    const bool shotMode = parser.isSet(shotOption);
    const QString qmlFile =
        shotMode ? mainFile : root + QStringLiteral("/apps/qt-interface/panel-host.qml");

    // The QML plugin of the viewer controller and the CNC.Edges plugin are built into the repo.
    addPathEntry(root + QStringLiteral("/build/qmlbackend/MockBackend"));
    addPathEntry(root + QStringLiteral("/build/edges/CNC/Edges"));
    // The deployed package keeps the OCCT DLLs next to the executable, so a double click works
    // without CNC_OCC_DIR in the environment.
    addPathEntry(QCoreApplication::applicationDirPath() + QStringLiteral("/occt"));
    const QString occ = qEnvironmentVariable("CNC_OCC_DIR");
    if (!occ.isEmpty())
        addPathEntry(occ + QStringLiteral("/bin"));

    QString fixture = parser.value(fixtureOption);
    if (fixture.isEmpty())
        fixture = root + QStringLiteral("/apps/qt-interface/backend/fixtures/auto-idle.json");
    else if (QDir::isRelativePath(fixture))
        fixture = root + QLatin1Char('/') + fixture;
    QString backendError;
    MachineBackend *machine = createBackend(parser.value(backendOption), fixture,
                                            parser.value(clockOption), &app, &backendError);
    if (!machine) {
        qWarning("lynx-panel: backend: %s", qPrintable(backendError));
        return 1;
    }

    // The panel has two fixed canvases: the 1024x600 key panel and the 864x1536 portrait touch
    // panel. `--touch` (also read by main.qml) starts the touch variant.
    const bool touch = parser.isSet(touchOption);

    PanelView view;
    QQmlEngine *engine = view.engine();
    engine->addImportPath(root + QStringLiteral("/build/qmlbackend"));
    engine->addImportPath(root + QStringLiteral("/build/edges"));
    CanvasLock canvasLock(&view);
    if (!shotMode)
        view.rootContext()->setContextProperty(QStringLiteral("CanvasLock"), &canvasLock);
    view.rootContext()->setContextProperty(QStringLiteral("machine"), machine);
    // The host item fills the view (SizeRootObjectToView) and scales the logical panel inside.
    // A screenshot is taken of the panel item itself: the root then keeps its own 1024x600 or
    // 864x1536 canvas even on a smaller developer screen.
    if (shotMode) {
        view.setResizeMode(QQuickView::SizeViewToRootObject);
    } else {
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        canvasLock.apply(touch ? 2 : 0);
    }
    view.setSource(QUrl::fromLocalFile(qmlFile));
    if (view.status() == QQuickView::Error) {
        for (const QQmlError &error : view.errors())
            qWarning("lynx-panel: %s", qPrintable(error.toString()));
        return 1;
    }

    view.setTitle(touch ? QStringLiteral("Lynx Touch — сенсорная панель")
                        : QStringLiteral("Lynx Touch — панель оператора"));
    if (shotMode && view.rootObject()) {
        QObject *panel = view.rootObject();
        if (!requestedWorkplace.isEmpty()) {
            panel->setProperty("workplace", requestedWorkplace);
            panel->setProperty("machineMode",
                               requestedWorkplace == QStringLiteral("SETUP")
                                   ? QStringLiteral("JOG")
                                   : QStringLiteral("AUTO"));
        }
        // The theme and the interface mode live in the same service map: insert once, so the second
        // key does not overwrite the first.
        if (requestedTheme >= 0 || touch) {
            QVariantMap values = panel->property("svcValues").toMap();
            if (requestedTheme >= 0)
                values.insert(QStringLiteral("system.0"), requestedTheme);
            if (touch)
                values.insert(QStringLiteral("system.9"), 2);
            panel->setProperty("svcValues", values);
        }
        if (!requestedScreen.isEmpty())
            panel->setProperty("activeScreen", requestedScreen);
        // The clock comes from the frozen backend state, so two shots of one fixture match.
        panel->setProperty("frozenClock", machine->clock());
        // A screenshot is a reference of the panel, not of the boot animation.
        panel->setProperty("splashVisible", false);
        panel->setProperty("splashLeaving", false);
        panel->setProperty("splashGone", true);
    }
    view.show();

    if (shotMode) {
        const QString shot = QFileInfo(parser.value(shotOption)).absoluteFilePath();
        QTimer::singleShot(shotDelay, &app, [&view, &app, shot]() {
            QQuickItem *root = view.rootObject();
            if (!root) {
                qWarning("lynx-panel: no root item for screenshot");
                app.quit();
                return;
            }
            const auto grab = root->grabToImage();
            QObject::connect(grab.data(), &QQuickItemGrabResult::ready, &app,
                             [grab, shot, &app]() {
                                 if (!grab->saveToFile(shot))
                                     qWarning("lynx-panel: cannot save screenshot %s",
                                              qPrintable(shot));
                                 app.quit();
                             });
        });
    }
    return app.exec();
}

#include "main.moc"
