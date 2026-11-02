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
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QResizeEvent>
#include <QScreen>
#include <QSize>
#include <QUrl>
#include <QtGlobal>

#ifdef Q_OS_WIN
#  include <windows.h>
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
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Lynx Touch"));
    app.setApplicationVersion(QStringLiteral("0.6.39"));

    const QString root = repoRoot();
    const QString mainFile = root + QStringLiteral("/apps/qt-interface/main.qml");
    if (!QFileInfo::exists(mainFile)) {
        qWarning("lynx-panel: main.qml not found: %s", qPrintable(mainFile));
        return 1;
    }
    // panel-host keeps the logical canvas and scales it uniformly into the resizable debug window.
    const QString qmlFile = root + QStringLiteral("/apps/qt-interface/panel-host.qml");

    // The QML plugin of the viewer controller and the CNC.Edges plugin are built into the repo.
    addPathEntry(root + QStringLiteral("/build/qmlbackend/MockBackend"));
    addPathEntry(root + QStringLiteral("/build/edges/CNC/Edges"));
    // The deployed package keeps the OCCT DLLs next to the executable, so a double click works
    // without CNC_OCC_DIR in the environment.
    addPathEntry(QCoreApplication::applicationDirPath() + QStringLiteral("/occt"));
    const QString occ = qEnvironmentVariable("CNC_OCC_DIR");
    if (!occ.isEmpty())
        addPathEntry(occ + QStringLiteral("/bin"));

    // The panel has two fixed canvases: the 1024x600 key panel and the 864x1536 portrait touch
    // panel. `--touch` (also read by main.qml) starts the touch variant.
    const bool touch = app.arguments().contains(QStringLiteral("--touch"));

    PanelView view;
    QQmlEngine *engine = view.engine();
    engine->addImportPath(root + QStringLiteral("/build/qmlbackend"));
    engine->addImportPath(root + QStringLiteral("/build/edges"));
    CanvasLock canvasLock(&view);
    view.rootContext()->setContextProperty(QStringLiteral("CanvasLock"), &canvasLock);
    // The host item fills the view (SizeRootObjectToView); the logical panel inside is scaled.
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    canvasLock.apply(touch ? 2 : 0);
    view.setSource(QUrl::fromLocalFile(qmlFile));
    if (view.status() == QQuickView::Error) {
        for (const QQmlError &error : view.errors())
            qWarning("lynx-panel: %s", qPrintable(error.toString()));
        return 1;
    }

    view.setTitle(touch ? QStringLiteral("Lynx Touch — сенсорная панель")
                        : QStringLiteral("Lynx Touch — панель оператора"));
    view.show();
    return app.exec();
}

#include "main.moc"
