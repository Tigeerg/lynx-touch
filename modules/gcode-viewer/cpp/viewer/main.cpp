/* gcodeviewer - C++ entry point of the NC viewer: the viewer runs without Python.
 *
 * The viewer is entirely C++: the 3D surface is modules/gcode-viewer/GcodeViewer3D.qml
 * (root - Item), the controller, geometries, NC parsing, STEP/STL and collisions
 * come from the MockBackend QML plugin (cpp/qmlbackend). The reference backend is
 * not part of the execution path: it is not here at all, and there is nothing to
 * substitute for it.
 *
 * What the entry point does:
 * * finds the QML plugin directory (CNC_CPP_QML_DIR, otherwise
 * <repo>/build/qmlbackend) and the OCCT DLL directory (CNC_OCC_DIR, otherwise from
 * CONDA_PREFIX) and puts both into PATH before creating the engine: the Qt plugin
 * entry point resolves dependencies via PATH, not through add_dll_directory
 * (breakdown - docs/PORT-CPP.md, "Qt runtime limitations").
 * On Linux it does not touch PATH and LD_LIBRARY_PATH: the loader finds Qt and
 * OCCT via the build rpath that CMake fills in itself (BUILD_RPATH,
 * cpp/CMakeLists.txt);
 * * gives the engine the plugin import path and loads GcodeViewer3D.qml into a
 * QQuickView: the window is created by QQuickView because the viewer root is an
 * Item, not a Window, and the viewer file itself is not edited;
 * * takes the gcodeToolpath object from the root and loads the NC from the
 * argument. In offscreen (QT_QPA_PLATFORM=offscreen) loading is synchronous and
 * without an event loop - the segment count is checked by it; in a live window -
 * load_gcode_async and printing by contract signals (toolpathLoaded,
 * loadFinished).
 * * before showing the window it detaches the console created for the process
 * itself (detachOwnConsole): its window is the second top-level window of the
 * viewer, and it takes the foreground. The number of viewers and process windows
 * created is printed (reportWindows), so "one window" is a check, not a promise.
 *
 * The --smoke key is a short start check: it loads the viewer QML offscreen and
 * prints the number of root objects, the root type and size, then RESULT: OK
 * (parse-level). The check is shallow - "the viewer starts", geometry is not
 * measured. A normal run (without the key) is unchanged.
 *
 * The --frame <file.png> key captures a frame: it builds the scene, waits for a
 * drawn frame, takes it from the window (QQuickWindow::grabWindow) and writes a
 * PNG. It prints the image size and the number of non-background pixels (the
 * background is the top-left pixel): they show whether geometry made it into the
 * frame. The image is acceptance evidence; the geometry is proven by the analytic
 * probe cases (stocksurface, stockshell, stock_bind), not by a pixel count.
 *
 * The frame platform is the one set outside; an empty one gives the live platform.
 * Offscreen on Windows does not draw the Quick3D scene (measured: the frame has
 * only interface elements, the 3D is empty), so the acceptance image is captured
 * on the live platform, while QT_QPA_PLATFORM=offscreen is left for the headless
 * run: it captures a frame and exits with code 0, with no scene in the frame.
 *
 * Without the plugin - a clear error and a non-zero return code: there is no
 * fallback to the reference.
 *
 * Run (Qt must be in PATH before the process starts: the exe links with
 * Qt6Core.dll; the plugin directory and OCCT DLLs the entry point adds itself):
 * set PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%
 * build\viewer\gcodeviewer.exe data\cad-testdata\Test_model.ngc
 * Offscreen: set QT_QPA_PLATFORM=offscreen
 * Smoke run: build\viewer\gcodeviewer.exe --smoke
 * Frame: build\viewer\gcodeviewer.exe --frame frame.png data\cad-testdata\Test_model.ngc
 * (QT_QPA_PLATFORM=offscreen - a headless run: the frame is captured, there is no scene in it)
 * Linux (Qt and OCCT are found via the build rpath, PATH and LD_LIBRARY_PATH are
 * not needed):
 * build/viewer/gcodeviewer data/cad-testdata/Test_model.ngc
 */
#include <cstdio>

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QList>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QQuickWindow>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVector3D>

#include <algorithm>

#include "core/quality.h"   // gv::presetFromEnv: the CNC_QUALITY level by the same read as the core

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#ifndef GV_REPO_ROOT
#  define GV_REPO_ROOT ""
#endif
#ifndef GV_VIEWER_QML
#  define GV_VIEWER_QML ""
#endif

namespace {

// Line output in UTF-8; the console is switched to UTF-8 in main. The technical
// lines (paths, numbers, "loaded OK") repeat the wording of the reference entry
// points: the run is checked by them.
void say(const QString &text)
{
    QTextStream stream(stdout);
    stream << text << '\n';
}

void warn(const QString &text)
{
    QTextStream stream(stderr);
    stream << text << '\n';
}

// QML engine warnings - as a separate line, like the reference entry points: a live
// run must have no "[QML warning]".
void reportQmlWarnings(const QList<QQmlError> &errors)
{
    for (const QQmlError &error : errors)
        warn(QStringLiteral("[QML warning] ") + error.toString());
}

// The entry point has three viewer branches (smoke run, frame capture, live run),
// and each has one window: the root of GcodeViewer3D.qml is an Item, so the window
// is created by QQuickView, and the viewer file itself is not edited. The window is
// a QQuickView subclass with a counter: the number of viewers is printed at start
// (reportWindows), so a second branch with a window would be visible as a number,
// not by eye.
int &createdViews()
{
    static int created = 0;
    return created;
}

class ViewerWindow : public QQuickView
{
public:
    ViewerWindow() { ++createdViews(); }
};

// Top-level windows of the process: how many in total and how many visible.
// Measured by enumerating this process's windows (EnumWindows filtered by pid), not
// by retelling. The viewer has one visible window; the rest (measured: six in
// total) are Qt and input service windows (_q_titlebar frame, theme and screen
// observers, MSCTFIME UI, IME). Before the fix (0.55.0, launched with a new
// console) there were seven, two of them visible: the viewer window (class
// Qt6120QWindowIcon) and the console window (ConsoleWindowClass), which the system
// creates for a console-subsystem application. The console took the foreground, and
// key-probe presses via SendInput did not reach the viewer window
// (tools/keyprobe.ps1, "SendInput ... did not reach the viewer window").
struct WindowCount
{
    int total = 0;
    int visible = 0;
};

#ifdef Q_OS_WIN
BOOL CALLBACK countProcessWindows(HWND hwnd, LPARAM param)
{
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != GetCurrentProcessId())
        return TRUE;
    WindowCount *count = reinterpret_cast<WindowCount *>(param);
    ++count->total;
    if (IsWindowVisible(hwnd))
        ++count->visible;
    return TRUE;
}
#endif

WindowCount processWindows()
{
    WindowCount count;
#ifdef Q_OS_WIN
    EnumWindows(&countProcessWindows, reinterpret_cast<LPARAM>(&count));
#endif
    return count;
}

// The console created FOR the process itself is detached: its window is the second
// top-level window, and it takes the foreground from the viewer (measured before the
// fix: two visible windows, see WindowCount). The parent's console (the terminal the
// viewer was launched from) is not touched: its window was not created by the
// viewer, and it is where the loading progress is printed. Redirection (a pipe, a
// file) is not a console, so detaching does not lose the output. The price is named
// outright: when launched by double-click the console window has time to appear and
// goes out in the first milliseconds - the viewer itself does not show it.
void detachOwnConsole()
{
#ifdef Q_OS_WIN
    DWORD attached[4] = { 0, 0, 0, 0 };
    // The console process list: one (ours) - the console was created for the
    // process, more - the console is shared with the parent.
    if (GetConsoleProcessList(attached, 4) != 1)
        return;
    FreeConsole();
#endif
}

// A GUI-subsystem process gets no console of its own: when it is started from a
// terminal the output goes to the console of the parent, when it is started by a
// double click there is no console and the output is dropped. Redirected handles (a
// pipe, a file) are left alone: --smoke and --frame are read through them by the
// guard and the probes.
void attachParentConsole()
{
#ifdef Q_OS_WIN
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) == FILE_TYPE_DISK)
        return;  // stdout is a file: the caller wants the text there
    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) == FILE_TYPE_PIPE)
        return;  // stdout is a pipe: the caller reads it, a console is not needed
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;
    FILE *stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
#endif
}

// The number of viewers and top-level windows: printed at the start of a live window
// and of frame capture, so that "one window" is a check. In an offscreen run the
// window is not shown, and this line is not printed - there is no window at all.
void reportWindows(const QString &branch)
{
#ifdef Q_OS_WIN
    const WindowCount windows = processWindows();
    say(QStringLiteral("gcodeviewer: %1: вьюверов создано %2, окон верхнего уровня "
                       "у процесса %3 (видимых %4)")
            .arg(branch).arg(createdViews()).arg(windows.total).arg(windows.visible));
#else
    say(QStringLiteral("gcodeviewer: %1: вьюверов создано %2")
            .arg(branch).arg(createdViews()));
#endif
}

// Showing the viewer window: first the own console is detached (breakdown at
// detachOwnConsole), then the window is shown, then the window count is printed.
void showViewerWindow(ViewerWindow &view, const QString &branch)
{
    detachOwnConsole();
    view.show();
    reportWindows(branch);
}

// Repository root: the exe lies in <repo>/build/viewer (target gcodeviewer), so two
// levels up. The guess is verified by the viewer file: a build with another build
// directory must not lead the search into a foreign tree - then the root recorded at
// build time is taken.
QString repoRoot()
{
    const QString guess =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    if (QFileInfo::exists(guess + QStringLiteral("/modules/gcode-viewer/GcodeViewer3D.qml")))
        return guess;
    return QString::fromUtf8(GV_REPO_ROOT);
}

// The viewer file: CNC_VIEWER_QML, otherwise the repository root's GcodeViewer3D.qml,
// otherwise the path recorded at build time.
QString viewerQmlPath()
{
    const QString override = qEnvironmentVariable("CNC_VIEWER_QML");
    if (!override.isEmpty())
        return override;
    const QString from_root = repoRoot() + QStringLiteral("/modules/gcode-viewer/GcodeViewer3D.qml");
    if (QFileInfo::exists(from_root))
        return from_root;
    return QString::fromUtf8(GV_VIEWER_QML);
}

// OCCT DLL directory: CNC_OCC_DIR (or the conda environment), the bin subdirectory.
// The default is the same as the build (scripts/build-viewer.ps1) and the former
// Python shim.
QString occtBinDir()
{
    QString occ = qEnvironmentVariable("CNC_OCC_DIR");
    if (occ.isEmpty()) {
        const QString prefix = qEnvironmentVariable("CONDA_PREFIX");
        if (!prefix.isEmpty())
            occ = prefix + QStringLiteral("/Library");
    }
    if (occ.isEmpty())
        occ = QStringLiteral("C:/Users/Tiger/anaconda3/envs/gcode-cpp/Library");
    return occ + QStringLiteral("/bin");
}

// Directories into PATH: the plugin's dependency search goes via PATH, and
// directories already there are not duplicated. On Linux PATH is not touched: there
// the loader finds both Qt and OCCT via the build rpath (BUILD_RPATH,
// cpp/CMakeLists.txt), and LD_LIBRARY_PATH is not needed.
void prependPath(const QStringList &dirs)
{
#ifdef Q_OS_LINUX
    Q_UNUSED(dirs);
#else
    const QChar separator = QDir::listSeparator();
    QStringList current = qEnvironmentVariable("PATH").split(separator, Qt::SkipEmptyParts);
    QStringList add;
    for (const QString &dir : dirs) {
        if (dir.isEmpty() || !QFileInfo(dir).isDir())
            continue;
        const QString absolute = QDir(dir).absolutePath();
        bool known = false;
        for (const QString &existing : current) {
            if (QDir::cleanPath(existing).compare(absolute, Qt::CaseInsensitive) == 0)
                known = true;
        }
        if (!known)
            add.append(absolute);
    }
    if (add.isEmpty())
        return;
    add += current;
    qputenv("PATH", add.join(separator).toLocal8Bit());
#endif
}

// Clean-view switches - frame capture and the live window share the same ones:
// hiding the NC lines (CNC_NO_TOOLPATH) and the part skin (CNC_NO_PART_SKIN). These
// are the same handles as the viewer keys G and K (GcodeViewer3D.qml): frame capture
// has no keys at all, so a frame for a view comparison is captured by a variable.
// Without the variables the behaviour is the previous one - both the lines and the
// skin are visible.
void applyCleanViewEnv(QQuickItem *root_item)
{
    root_item->setProperty("showToolpath", !qEnvironmentVariableIsSet("CNC_NO_TOOLPATH"));
    root_item->setProperty("showPartSkin", !qEnvironmentVariableIsSet("CNC_NO_PART_SKIN"));
}

// CNC_STOCK_VIEW: the stock remainder view before start - the same handle as key R
// (GcodeViewer3D.qml, stockView). Three views: exact (exact by field nodes), smooth
// (smooth: a subdivided mesh, bilinear height - the default view) and rounded
// (rounded: a node's depth is the average over its neighbourhood, the former
// CNC_STOCK_ROUND toggle). CNC_STOCK_ROUND=1 stays as a shorthand for rounded: the
// variable was in the launch contract, and its behaviour is preserved. An unclear
// string is a refusal with the reason in stderr: silently showing the wrong view is
// not allowed, frames are compared by view.
void applyStockViewEnv(QQuickItem *root_item)
{
    const char *names[] = { "exact", "smooth", "rounded" };
    if (qEnvironmentVariableIsSet("CNC_STOCK_ROUND")) {
        root_item->setProperty("stockView", 2);
    }
    const QString text = qEnvironmentVariable("CNC_STOCK_VIEW").trimmed().toLower();
    if (text.isEmpty())
        return;
    for (int i = 0; i < 3; ++i) {
        if (text == QLatin1String(names[i])) {
            root_item->setProperty("stockView", i);
            return;
        }
    }
    warn(QStringLiteral("gcodeviewer: CNC_STOCK_VIEW: вид не разобран: %1 "
                        "(ожидается exact, smooth или rounded)")
                 .arg(text));
}

// Material remainder palette colours before start. The "variable -> property" pair
// as one table: both colours have a property of the same kind, and the variable name
// is part of the launch contract (README, GcodeViewer3D.qml).
struct StockColorEnv {
    const char *variable;
    const char *property;
};

// CNC_STOCK_RAW and CNC_STOCK_CUT set palette colours by "#RRGGBB" (or
// "#AARRGGBB") - the same two handles as the viewer presets and key C
// (GcodeViewer3D.qml, stockPresets): the variable sets the pair's colour before
// start, the preset stays as it was. An unclear string is a refusal with the reason
// in stderr: silently painting the ingot black is not allowed, its colour is read to
// see where metal is removed.
void applyStockColorEnv(QQuickItem *root_item)
{
    const StockColorEnv vars[] = {
        { "CNC_STOCK_RAW", "stockRawColor" },
        { "CNC_STOCK_CUT", "stockCutColor" },
    };
    for (const StockColorEnv &var : vars) {
        const QString text = qEnvironmentVariable(var.variable);
        if (text.isEmpty())
            continue;
        const QColor color(text);
        if (!color.isValid()) {
            warn(QStringLiteral("gcodeviewer: %1: цвет не разобран: %2 "
                                "(ожидается #RRGGBB)")
                         .arg(QString::fromLatin1(var.variable), text));
            continue;
        }
        root_item->setProperty(var.property, color);
    }
}

// CNC_STOCK_GHOST: the stock translucent before start - the same handle as key V
// (GcodeViewer3D.qml, stockGhost). The glass is the default now, so the variable
// does not enable the view but disables it: an empty variable or 1/true/on keeps the
// glass, while 0/false/off/no sets solid before start - that is how frames compare
// views as the operator calls them, and there is no silent view substitution.
void applyStockGhostEnv(QQuickItem *root_item)
{
    if (!qEnvironmentVariableIsSet("CNC_STOCK_GHOST"))
        return;                                // the viewer default: glass
    const QString text = qEnvironmentVariable("CNC_STOCK_GHOST").trimmed().toLower();
    const bool solid = text == QLatin1String("0") || text == QLatin1String("false")
            || text == QLatin1String("off") || text == QLatin1String("no");
    root_item->setProperty("stockGhost", !solid);
}

// CNC_PART_MODE: the part view before start - the same handle as key M
// (GcodeViewer3D.qml, partMode), and the only one: solid - an opaque skin (default),
// transparent - translucent, hidden - the part is hidden entirely. The translucent
// skin is the former x-ray view (the viewer no longer has key X): two switches of one
// view did not let one tell which view is current and how it differs from the
// neighbouring one. An unclear string is a refusal with the reason in stderr:
// silently showing the wrong view is not allowed, frames are compared by view.
void applyPartModeEnv(QQuickItem *root_item)
{
    const QString text = qEnvironmentVariable("CNC_PART_MODE").trimmed().toLower();
    if (text.isEmpty())
        return;
    const char *names[] = { "solid", "transparent", "hidden" };
    for (int i = 0; i < 3; ++i) {
        if (text == QLatin1String(names[i])) {
            root_item->setProperty("partMode", i);
            return;
        }
    }
    warn(QStringLiteral("gcodeviewer: CNC_PART_MODE: вид не разобран: %1 "
                        "(ожидается solid, transparent или hidden)")
                 .arg(text));
}

// CNC_FRAME_CAMERA: the frame capture camera with explicit numbers -
// "yaw,pitch,dist" plus optional "panX,panY". They capture a frame of the same view
// from another point; a run without the variable takes the viewer's angles and the
// distance by the path bounds (the same numbers the viewer sets). The parse is
// strict: an incomplete or non-numeric string is a refusal with the reason in stderr,
// not a frame with another scale (frames are compared with each other, and a silent
// substitution here is more expensive than a refusal). Returns false if the string
// was set but not parsed.
bool applyFrameCameraEnv(QQuickItem *root_item)
{
    const QString text = qEnvironmentVariable("CNC_FRAME_CAMERA").trimmed();
    if (text.isEmpty())
        return true;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.size() < 3 || parts.size() > 5) {
        warn(QStringLiteral("gcodeviewer: CNC_FRAME_CAMERA: не разобрана: %1 "
                            "(ожидается yaw,pitch,dist[,panX,panY])").arg(text));
        return false;
    }
    double values[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
    const char *names[] = { "yaw", "pitch", "dist", "panX", "panY" };
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        values[i] = parts.at(i).trimmed().toDouble(&ok);
        if (!ok) {
            warn(QStringLiteral("gcodeviewer: CNC_FRAME_CAMERA: %1 не число: %2")
                     .arg(QString::fromLatin1(names[i]), parts.at(i)));
            return false;
        }
    }
    for (int i = 0; i < parts.size(); ++i)
        root_item->setProperty(names[i], values[i]);
    return true;
}

// Frame capture camera: the angles, distance and panorama are set here, not by
// whatever the viewer managed to do by the capture moment. The distance binding to
// the path bounds hangs in QML on the pathChanged signal (GcodeViewer3D.qml:
// dist = max(90, radius * 3 + 60)), so a frame captured before and after that signal
// shows a different scale - measured in 0.53.0: the tool bounds landed in six
// different positions out of six runs. Here the same numbers are set once, after the
// NC is loaded and the field is ready: the angles are read from the viewer (nothing
// but the mouse moves them, and capture has no mouse), the distance is taken by the
// same formula as the viewer's, and the panorama is zeroed. Then CNC_FRAME_CAMERA
// overrides what was set - it is set on purpose, and there is nothing to override it
// with. The tool and holder silhouette is recomputed last: it is built from the
// camera eye, and a frame captured before the recompute would show the previous
// point's silhouette.
// A false return - CNC_FRAME_CAMERA is set but not parsed: the frame must not be
// captured then, the scale would not be the one requested.
bool applyFrameCamera(QQuickItem *root_item, QObject *backend)
{
    const double radius = backend != nullptr ? backend->property("radius").toDouble() : 0.0;
    root_item->setProperty("panX", 0.0);
    root_item->setProperty("panY", 0.0);
    root_item->setProperty("dist", std::max(90.0, radius * 3.0 + 60.0));
    if (!applyFrameCameraEnv(root_item))
        return false;
    QMetaObject::invokeMethod(root_item, "refreshTangents");
    return true;
}

// Smoke run (--smoke): load the viewer QML headless and report the parse. The check
// is shallow: "the viewer starts at all", geometry is not measured. The root size is
// taken in the same 1024x600 window as a normal run.
int runSmoke(const QString &viewer_qml, const QString &backend_import)
{
    QList<QQmlError> collected;
    ViewerWindow view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1024, 600);
    view.engine()->addImportPath(backend_import);
    QObject::connect(view.engine(), &QQmlEngine::warnings,
                     [&collected](const QList<QQmlError> &errors) { collected += errors; });
    view.setSource(QUrl::fromLocalFile(viewer_qml));

    QQuickItem *root_item = view.rootObject();
    say(QStringLiteral("Root object count: %1").arg(root_item != nullptr ? 1 : 0));
    if (root_item != nullptr) {
        say(QStringLiteral("Root type: %1")
                .arg(QString::fromLatin1(root_item->metaObject()->className())));
        say(QStringLiteral("Root size: %1x%2").arg(root_item->width()).arg(root_item->height()));
    }
    for (const QQmlError &error : collected)
        say(QStringLiteral("[QML warning] ") + error.toString());

    if (root_item == nullptr) {
        for (const QQmlError &error : view.errors())
            warn(QStringLiteral("gcodeviewer: ") + error.toString());
        say(QStringLiteral("RESULT: FAILED"));
        return 1;
    }
    say(QStringLiteral("RESULT: OK (parse-level)"));
    return 0;
}

// Frame capture (--frame <file.png>): build the scene, wait for a drawn frame, take
// it from the window and write a PNG. The output numbers (image size and the number
// of non-background pixels) say that the frame has geometry, but the geometry is
// proven not by them but by the analytic probe cases (stocksurface, stockshell,
// stock_bind). The image is needed as acceptance evidence (CLAUDE.md: a visual result
// is viewed as an image).
//
// The platform is taken from outside: empty - the live one (Quick3D draws the scene
// on it), offscreen - a headless run (the frame is captured, there is no scene in it).
int runFrame(const QString &viewer_qml, const QString &backend_import,
             const QString &program, const QString &frame_path)
{
    ViewerWindow view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1024, 600);
    view.engine()->addImportPath(backend_import);
    QObject::connect(view.engine(), &QQmlEngine::warnings, &reportQmlWarnings);
    view.setSource(QUrl::fromLocalFile(viewer_qml));
    if (view.status() != QQuickView::Ready) {
        warn(QStringLiteral("gcodeviewer: FAIL QML не загрузился: %1").arg(viewer_qml));
        for (const QQmlError &error : view.errors())
            warn(QStringLiteral("gcodeviewer: ") + error.toString());
        return 1;
    }
    QQuickItem *root_item = view.rootObject();
    if (root_item == nullptr) {
        warn(QStringLiteral("gcodeviewer: FAIL корневой объект QML пуст"));
        return 1;
    }
    QObject *backend = root_item->findChild<QObject *>(QStringLiteral("gcodeToolpath"));
    if (backend == nullptr) {
        warn(QStringLiteral("gcodeviewer: FAIL контроллер gcodeToolpath не найден в QML"));
        return 1;
    }

    // The same scene properties as a normal run: the quality level, a box instead of
    // the removal field (CNC_STOCK_BOX), the clean-view switches (CNC_NO_TOOLPATH,
    // CNC_NO_PART_SKIN, CNC_STOCK_GHOST, CNC_PART_MODE), the remainder view
    // (CNC_STOCK_VIEW, CNC_STOCK_ROUND) and the ingot palette colours
    // (CNC_STOCK_RAW, CNC_STOCK_CUT). Otherwise the frame would show something other
    // than what the operator sees.
    root_item->setProperty("quality", QString::fromLatin1(gv::presetFromEnv().level));
    root_item->setProperty("stockPlainBox", qEnvironmentVariableIsSet("CNC_STOCK_BOX"));
    applyCleanViewEnv(root_item);
    applyStockViewEnv(root_item);
    applyStockColorEnv(root_item);
    applyStockGhostEnv(root_item);
    applyPartModeEnv(root_item);

    showViewerWindow(view, QStringLiteral("--frame"));
    if (!program.isEmpty()) {
        if (!QFileInfo::exists(program)) {
            warn(QStringLiteral("gcodeviewer: FAIL файл УП не найден: %1").arg(program));
            return 3;
        }
        // Loading is synchronous (the same path as the offscreen run): the frame is
        // captured after it, not by background-thread signals.
        int segments = 0;
        QMetaObject::invokeMethod(backend, "load_gcode", Q_RETURN_ARG(int, segments),
                                  Q_ARG(QString, program));
        say(QStringLiteral("gcodeviewer: loaded g-code from: %1 segments: %2")
                .arg(program).arg(segments));
    }

    // The removal field is computed in the background (QThreadPool), so the frame is
    // captured after it is ready: otherwise the frame would show the stock box instead
    // of the material remainder. The wait ceiling is 20 s on a slow machine and a
    // coarse node; the state and the field flag are printed so this is visible, not
    // implied.
    if (backend != nullptr && !program.isEmpty()) {
        QElapsedTimer wait;
        wait.start();
        QString state = backend->property("stockSimState").toString();
        while (wait.elapsed() < 20000
               && state != QLatin1String("ready")
               && state != QLatin1String("refused")
               && state != QLatin1String("idle")
               && state != QLatin1String("cancelled")) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
            state = backend->property("stockSimState").toString();
        }
        say(QStringLiteral("gcodeviewer: stockSimState=%1 after %2 ms field=%3")
                .arg(state)
                .arg(wait.elapsed())
                .arg(backend->property("stockHasField").toBool() ? QStringLiteral("yes")
                                                                 : QStringLiteral("no")));
    }

    // The frame camera is deterministic (breakdown in applyFrameCamera): two runs of
    // one command are compared not only by the image but also by these numbers.
    if (!applyFrameCamera(root_item, backend))
        return 2;
    {
        const QVector3D pivot = root_item->property("pivot").value<QVector3D>();
        say(QStringLiteral("gcodeviewer: camera yaw=%1 pitch=%2 dist=%3 pan=(%4,%5) "
                           "pivot=(%6,%7,%8)")
                .arg(root_item->property("yaw").toDouble(), 0, 'f', 3)
                .arg(root_item->property("pitch").toDouble(), 0, 'f', 3)
                .arg(root_item->property("dist").toDouble(), 0, 'f', 3)
                .arg(root_item->property("panX").toDouble(), 0, 'f', 3)
                .arg(root_item->property("panY").toDouble(), 0, 'f', 3)
                .arg(pivot.x(), 0, 'f', 3)
                .arg(pivot.y(), 0, 'f', 3)
                .arg(pivot.z(), 0, 'f', 3));
    }

    // Wait for drawn frames: grabWindow before the first frame gives an empty image.
    // The frameSwapped signal marks a ready frame; the wait ceiling is in case the
    // scene was not drawn at all (then the frame is still saved, and this is visible
    // by the number of non-background pixels).
    int frames = 0;
    QEventLoop loop;
    QObject::connect(&view, &QQuickWindow::frameSwapped, [&frames, &loop] {
        if (++frames >= 3)
            loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();

    const QImage image = view.grabWindow();
    if (image.isNull()) {
        warn(QStringLiteral("gcodeviewer: FAIL кадр не снят (пустое изображение)"));
        return 1;
    }
    if (!image.save(frame_path, "PNG")) {
        warn(QStringLiteral("gcodeviewer: FAIL кадр не записан: %1").arg(frame_path));
        return 1;
    }

    // The background is the top-left pixel: the viewer scene does not occupy it
    // (there is the grid and the sky), so the difference from it is the drawn
    // geometry.
    const QRgb background = image.pixel(0, 0);
    qint64 painted = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (line[x] != background)
                ++painted;
        }
    }
    say(QStringLiteral("gcodeviewer: frame %1x%2 frames_swapped=%3 -> %4")
            .arg(image.width()).arg(image.height()).arg(frames).arg(frame_path));
    say(QStringLiteral("gcodeviewer: non-background pixels: %1 of %2")
            .arg(painted).arg(qint64(image.width()) * image.height()));
    if (painted == 0) {
        warn(QStringLiteral("gcodeviewer: кадр пуст (фон целиком): сцену никто не "
                            "нарисовал - проверьте растеризатор платформы"));
    }
    return 0;
}

// Printing the loading progress in a live window. The contract signals come from the
// QML plugin that the engine loads itself: the entry point cannot see the controller
// type, so a separate object with slots receives them, and the connection is set by
// signal names.
class LoadReporter : public QObject
{
    Q_OBJECT
public:
    LoadReporter(QString program, QQuickItem *root, QObject *backend, QObject *parent)
        : QObject(parent), program_(std::move(program)), root_(root), backend_(backend)
    {
        connect(backend, SIGNAL(toolpathLoaded(int)), this, SLOT(onToolpathLoaded(int)));
        connect(backend, SIGNAL(loadFinished()), this, SLOT(onLoadFinished()));
    }

public slots:
    void onToolpathLoaded(int segments)
    {
        say(QStringLiteral("gcodeviewer: loaded g-code from: %1 segments: %2")
                .arg(program_).arg(segments));
    }

    void onLoadFinished()
    {
        say(QStringLiteral("gcodeviewer: modelPath: %1")
                .arg(backend_->property("modelPath").toString()));
        root_->setProperty("loading", false);
    }

private:
    QString program_;
    QQuickItem *root_;
    QObject *backend_;
};

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    attachParentConsole();
    SetConsoleOutputCP(CP_UTF8);          // Russian text is visible in the manager's console
#endif
    // --smoke and --frame are looked for in argv before the application is created:
    // the platform is chosen before it.
    bool smoke = false;
    bool frame = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--smoke") == 0)
            smoke = true;
        if (qstrcmp(argv[i], "--frame") == 0)
            frame = true;
    }
    // The smoke run shows nothing: offscreen is enough for it, and a platform set
    // outside is not touched (keyprobe.cpp does the same). Frame capture is NOT forced
    // onto a platform: the offscreen Quick3D rasteriser on Windows does not draw the
    // scene (measured: a 1024x600 frame contains only interface elements, the 3D is
    // empty), so the live platform gives a real image, while QT_QPA_PLATFORM=offscreen
    // from outside still captures a frame and exits with code 0 - good for a headless
    // run.
    if (smoke && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    QString program;
    QString frame_path;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
            say(QStringLiteral("запуск: gcodeviewer [--smoke] [--frame <файл.png>] "
                               "[путь к файлу УП]"));
            return 0;
        }
        if (arg == QLatin1String("--frame")) {
            // The value comes as the next argument: without it there is nowhere to
            // capture.
            if (i + 1 >= argc) {
                warn(QStringLiteral("gcodeviewer: FAIL у --frame нет пути к файлу кадра"));
                return 2;
            }
            frame_path = QString::fromLocal8Bit(argv[++i]);
            continue;
        }
        if (arg.startsWith(QLatin1Char('-')))
            continue;
        program = arg;
    }

    const QString viewer_qml = viewerQmlPath();
    const QString backend_import = qEnvironmentVariable("CNC_CPP_QML_DIR").isEmpty()
        ? repoRoot() + QStringLiteral("/build/qmlbackend")
        : qEnvironmentVariable("CNC_CPP_QML_DIR");
    const QString backend_module = backend_import + QStringLiteral("/MockBackend");

    // No plugin - a clear error and a non-zero code: there is no reference backend at
    // runtime, and there is nothing to substitute.
    if (!QFileInfo::exists(backend_module + QStringLiteral("/qmldir"))) {
        warn(QStringLiteral("gcodeviewer: FAIL QML-модуль MockBackend не найден: %1/qmldir")
                 .arg(backend_module));
        warn(QStringLiteral("gcodeviewer: соберите плагин: powershell -NoProfile "
                            "-ExecutionPolicy Bypass -File scripts/build-viewer.ps1"));
        warn(QStringLiteral("gcodeviewer: питоновского бэкенда в рантайме нет: "
                            "без собранного плагина вьювер не запускается"));
        return 2;
    }
    if (!QFileInfo::exists(viewer_qml)) {
        warn(QStringLiteral("gcodeviewer: FAIL файл вьювера не найден: %1").arg(viewer_qml));
        return 2;
    }

    prependPath({backend_module, occtBinDir()});

    if (smoke)
        return runSmoke(viewer_qml, backend_import);
    if (frame)
        return runFrame(viewer_qml, backend_import, program, frame_path);

    ViewerWindow view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1024, 600);              // the same window size as frame capture
    view.engine()->addImportPath(backend_import);
    QObject::connect(view.engine(), &QQmlEngine::warnings, &reportQmlWarnings);
    view.setSource(QUrl::fromLocalFile(viewer_qml));
    if (view.status() != QQuickView::Ready) {
        warn(QStringLiteral("gcodeviewer: FAIL QML не загрузился: %1").arg(viewer_qml));
        for (const QQmlError &error : view.errors())
            warn(QStringLiteral("gcodeviewer: ") + error.toString());
        return 1;
    }

    QQuickItem *root_item = view.rootObject();
    if (root_item == nullptr) {
        warn(QStringLiteral("gcodeviewer: FAIL корневой объект QML пуст"));
        return 1;
    }
    QObject *backend = root_item->findChild<QObject *>(QStringLiteral("gcodeToolpath"));
    if (backend == nullptr) {
        warn(QStringLiteral("gcodeviewer: FAIL контроллер gcodeToolpath не найден в QML"));
        return 1;
    }
    say(QStringLiteral("gcodeviewer: viewer version: %1")
            .arg(root_item->property("viewerVersion").toString()));

    // Keyboard diagnostics: CNC_KEY_DEBUG=1 enables printing key-press signs in the
    // viewer root (GcodeViewer3D.qml, keyLogRaw/keyLogState) - it measures key
    // delivery on different layouts. By default the output is silent.
    root_item->setProperty("keyDebug", qEnvironmentVariableIsSet("CNC_KEY_DEBUG"));

    // The quality level from CNC_QUALITY: the viewer root sets render handles with it
    // (GcodeViewer3D.qml, property quality). The read is the same as the core's
    // (gv::presetFromEnv): an empty or unknown value gives med - the previous image,
    // so without the variable the behaviour does not change.
    root_item->setProperty("quality", QString::fromLatin1(gv::presetFromEnv().level));

    // CNC_STOCK_BOX: the former box instead of the removal surface - it captures a
    // frame for comparison (the same handle as key B). By default there is no box:
    // the stock is drawn by the height field.
    root_item->setProperty("stockPlainBox", qEnvironmentVariableIsSet("CNC_STOCK_BOX"));
    applyCleanViewEnv(root_item);
    applyStockViewEnv(root_item);

    // CNC_STOCK_RAW/CNC_STOCK_CUT: ingot palette colours before start (the same two
    // handles as the presets and key C).
    applyStockColorEnv(root_item);

    // Translucent stock (V) and part view (M) before start.
    applyStockGhostEnv(root_item);
    applyPartModeEnv(root_item);

    if (program.isEmpty()) {
        say(QStringLiteral("gcodeviewer: УП не задана: вьювер пуст "
                           "(аргумент - путь к файлу УП)"));
        showViewerWindow(view, QStringLiteral("живое окно"));
        return app.exec();
    }
    if (!QFileInfo::exists(program)) {
        warn(QStringLiteral("gcodeviewer: FAIL файл УП не найден: %1").arg(program));
        return 3;
    }

    if (app.platformName() == QLatin1String("offscreen")) {
        int segments = 0;
        QMetaObject::invokeMethod(backend, "load_gcode", Q_RETURN_ARG(int, segments),
                                  Q_ARG(QString, program));
        say(QStringLiteral("gcodeviewer: loaded g-code from: %1 segments: %2")
                .arg(program).arg(segments));
        say(QStringLiteral("gcodeviewer: modelPath: %1")
                .arg(backend->property("modelPath").toString()));
        say(QStringLiteral("gcodeviewer: offscreen mode - loaded OK."));
        return 0;
    }

    // Live window: NC parsing and the model run on a worker thread, and the loading
    // overlay is driven by the viewer root (the same as at the reference entry point).
    root_item->setProperty("loading", true);
    auto *reporter = new LoadReporter(program, root_item, backend, &app);
    Q_UNUSED(reporter);
    showViewerWindow(view, QStringLiteral("живое окно"));
    QMetaObject::invokeMethod(backend, "load_gcode_async", Q_ARG(QString, program));
    return app.exec();
}

#include "main.moc"
