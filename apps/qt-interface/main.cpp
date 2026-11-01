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
#include <QQmlEngine>
#include <QQuickView>
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
    app.setApplicationVersion(QStringLiteral("0.6.20"));

    const QString root = repoRoot();
    const QString qmlFile = root + QStringLiteral("/apps/qt-interface/main.qml");
    if (!QFileInfo::exists(qmlFile)) {
        qWarning("lynx-panel: main.qml not found: %s", qPrintable(qmlFile));
        return 1;
    }

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
    const QSize canvas = touch ? QSize(864, 1536) : QSize(1024, 600);

    QQuickView view;
    QQmlEngine *engine = view.engine();
    engine->addImportPath(root + QStringLiteral("/build/qmlbackend"));
    engine->addImportPath(root + QStringLiteral("/build/edges"));
    // Both canvases are fixed designs: the window must not stretch, otherwise a layout that fits
    // the design would only look fitting at one arbitrary size.
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setMinimumSize(canvas);
    view.setMaximumSize(canvas);
    view.setSource(QUrl::fromLocalFile(qmlFile));
    if (view.status() == QQuickView::Error) {
        for (const QQmlError &error : view.errors())
            qWarning("lynx-panel: %s", qPrintable(error.toString()));
        return 1;
    }

    view.setTitle(touch ? QStringLiteral("Lynx Touch — сенсорная панель")
                        : QStringLiteral("Lynx Touch — панель оператора"));
    view.resize(canvas);
    view.show();
    return app.exec();
}
