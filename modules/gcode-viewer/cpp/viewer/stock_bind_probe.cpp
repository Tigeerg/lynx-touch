// viewer/stock_bind_probe.cpp - probe of the binding of the removal field to the
// path in the C++ controller (stages 2-4 of the removal simulation).
//
// The probe loads the real QML module MockBackend and creates a GcodeToolpath, then
// calls load_gcode_async on a real NC program. For the med and high presets it
// prints the node, the time of the background pass, the number of nodes and the
// size of the field, and for med it also compares the drawing texture (stockField)
// with the published field: the size, the R32F format and the bytes must match.
//
// Live removal (stage 4). After the full field the program is rewound to a quarter
// of the path: while the tick is computed the state is computing, and the published
// field must stay the previous whole one (stage 1: the field is not dropped while
// computing). Then the tick finishes, the field reaches a quarter of the path, and
// forward to the end: the digest (FNV-1a over the bytes) must match the full pass -
// the incremental walk has no right to differ from a single pass. Cancelling a live
// tick publishes not a new field but the last whole one; cancelling the first pass
// (there is no field yet) publishes nothing.
//
// Refusal (stage 2). An NC program with a TYPE=LOLLIPOP tool: a height map does not
// express the undercut, so the state is refused, the field is empty (stockHasField
// false, 0 nodes), and the reason is a refusal line on stderr.
//
// Usage (Qt in PATH; the offscreen platform is set automatically):
// build\viewer\stock_bind_probe.exe [path to the NC program file]
// Return code: 0 - all checks passed, 1 - not.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuick3DTextureData>
#include <QSize>
#include <QString>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include <functional>
#include <cstdint>

#ifndef GV_REPO_ROOT
#  define GV_REPO_ROOT ""
#endif

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

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

QString repoRoot()
{
    const QString guess =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    if (QFileInfo::exists(guess + QStringLiteral("/modules/gcode-viewer/GcodeViewer3D.qml")))
        return guess;
    return QString::fromUtf8(GV_REPO_ROOT);
}

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

// The same plugin dependency search order as in the viewer entry point:
// the module directory and the OCCT DLLs go into PATH before the engine is created.
void prependPath(const QStringList &dirs)
{
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
}

// Waiting for a controller property with event queue pumping: Qt signals are
// delivered only in a loop, and the probe lives without app.exec.
bool waitUntil(QObject *object, const std::function<bool()> &done, int timeoutMs,
               const QString &what)
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > timeoutMs) {
            warn(QStringLiteral("stock_bind: timeout %1 ms: %2")
                     .arg(timeoutMs).arg(what));
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return true;
}

QString stateOf(QObject *controller)
{
    return controller->property("stockSimState").toString();
}

// FNV-1a 64 over the field bytes: the live walk must give the same buffer as the
// full pass, byte for byte (the same check as case stock_live in stock_sim_probe).
std::uint64_t bytesDigest(const QByteArray &bytes)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (char byte : bytes) {
        hash ^= static_cast<unsigned char>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool startLoad(QObject *controller, const QString &path)
{
    return QMetaObject::invokeMethod(controller, "load_gcode_async",
                                     Q_ARG(QString, path));
}

// A new pass starts only after the NC program is parsed: the previous ready state
// is held until onToolpathReady, so we first wait for computing.
bool waitComputing(QObject *controller, int timeoutMs)
{
    return waitUntil(controller,
                     [controller] { return stateOf(controller) == QLatin1String("computing"); },
                     timeoutMs, QStringLiteral("state computing"));
}

bool waitReady(QObject *controller, int timeoutMs)
{
    return waitUntil(controller,
                     [controller] { return stateOf(controller) == QLatin1String("ready"); },
                     timeoutMs, QStringLiteral("state ready"));
}

// The computing state: either a full pass or a live tick. The field is already
// published as the previous whole one - that is what is checked.
bool waitState(QObject *controller, const char *state, int timeoutMs)
{
    return waitUntil(controller,
                     [controller, state] {
                         return stateOf(controller) == QLatin1String(state);
                     },
                     timeoutMs, QString::fromLatin1(state));
}

bool follow(QObject *controller, double pathDistMm)
{
    return QMetaObject::invokeMethod(controller, "stockFollow",
                                     Q_ARG(double, pathDistMm));
}

void reportRun(QObject *controller, const char *quality)
{
    const double ms = controller->property("stockSimTimeMs").toDouble();
    const double cell = controller->property("stockCellMm").toDouble();
    const int nx = controller->property("stockCountX").toInt();
    const int ny = controller->property("stockCountY").toInt();
    const int bytes = controller->property("stockDepth").toByteArray().size();
    const double progress = controller->property("stockSimProgress").toDouble();
    say(QStringLiteral("stock_bind: quality=%1 cell_mm=%2 time_ms=%3 nodes=%4x%5 "
                       "bytes=%6 progress=%7")
            .arg(QString::fromLatin1(quality))
            .arg(cell, 0, 'f', 1)
            .arg(ms, 0, 'f', 1)
            .arg(nx)
            .arg(ny)
            .arg(bytes)
            .arg(progress, 0, 'f', 2));
}

// The drawing texture must carry the same field: stage 3 takes the vertex shader
// height from it, and a diverged copy would draw something other than what was
// computed. The size (number of nodes), the format (R32F) and the bytes are
// compared - the controller copy against the published stockDepth.
bool textureMatchesField(QObject *controller, QString *detail)
{
    auto *data = qobject_cast<QQuick3DTextureData *>(
            controller->property("stockField").value<QObject *>());
    if (data == nullptr) {
        *detail = QStringLiteral("stockField не TextureData");
        return false;
    }
    const QByteArray depth = controller->property("stockDepth").toByteArray();
    const QSize size = data->size();
    const bool matches = data->format() == QQuick3DTextureData::R32F
            && size.width() == controller->property("stockCountX").toInt()
            && size.height() == controller->property("stockCountY").toInt()
            && !depth.isEmpty()
            && data->textureData() == depth;
    *detail = QStringLiteral("%1x%2 format=%3 bytes=%4 depth_bytes=%5")
            .arg(size.width())
            .arg(size.height())
            .arg(int(data->format()))
            .arg(data->textureData().size())
            .arg(depth.size());
    return matches;
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const QString root = repoRoot();
    const QString module_dir = qEnvironmentVariable("CNC_CPP_QML_DIR").isEmpty()
        ? root + QStringLiteral("/build/qmlbackend")
        : qEnvironmentVariable("CNC_CPP_QML_DIR");
    const QString program = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : root + QStringLiteral("/data/cad-testdata/Test_model.ngc");
    if (!QFileInfo::exists(module_dir + QStringLiteral("/MockBackend/qmldir"))) {
        warn(QStringLiteral("stock_bind: FAIL модуль MockBackend не найден: %1").arg(module_dir));
        return 1;
    }
    if (!QFileInfo::exists(program)) {
        warn(QStringLiteral("stock_bind: FAIL файл УП не найден: %1").arg(program));
        return 1;
    }
    prependPath({module_dir + QStringLiteral("/MockBackend"), occtBinDir()});

    QQmlEngine engine;
    engine.addImportPath(module_dir);
    QQmlComponent component(&engine);
    component.setData("import QtQml\nimport MockBackend 1.0\nGcodeToolpath {}\n",
                      QUrl(QStringLiteral("qrc:/stock_bind_probe.qml")));
    QObject *controller = component.create();
    if (controller == nullptr) {
        warn(QStringLiteral("stock_bind: FAIL GcodeToolpath не создан: %1")
                 .arg(component.errorString()));
        return 1;
    }

    int failures = 0;

    // Presets: med (0.5 mm) and high (0.2 mm). The level is read when the pass
    // starts, so qputenv before load_gcode_async is enough.
    qputenv("CNC_QUALITY", "med");
    if (!startLoad(controller, program) || !waitComputing(controller, 120000)
        || !waitReady(controller, 120000)) {
        ++failures;
    } else {
        reportRun(controller, "med");
        QString detail;
        const bool textureOk = textureMatchesField(controller, &detail);
        say(QStringLiteral("stock_bind: field %1 matches=%2")
                .arg(detail).arg(textureOk ? 1 : 0));
        if (!textureOk) {
            ++failures;
        }
    }

    qputenv("CNC_QUALITY", "high");
    QByteArray fullField;
    std::uint64_t fullDigest = 0;
    double totalMm = 0.0;
    if (!startLoad(controller, program) || !waitComputing(controller, 120000)
        || !waitReady(controller, 120000)) {
        ++failures;
    } else {
        reportRun(controller, "high");
        fullField = controller->property("stockDepth").toByteArray();
        fullDigest = bytesDigest(fullField);
        totalMm = controller->property("moveLength").toDouble();
        say(QStringLiteral("stock_bind: full digest=%1 bytes=%2 total_mm=%3")
                .arg(static_cast<qulonglong>(fullDigest), 16, 16, QLatin1Char('0'))
                .arg(fullField.size())
                .arg(totalMm, 0, 'f', 1));
    }

    // Point 1 and point 4 at once: a rewind to a quarter of the path. While the tick
    // is computed, the previous whole field must stay on screen (the computing state,
    // the bytes unchanged), and after the tick - the field up to a quarter, byte-wise
    // different from the full one. Then forward to the end: the live walk must give
    // exactly the full buffer (the digest is equal), otherwise the incremental
    // removal lies.
    if (totalMm > 0.0 && !fullField.isEmpty()) {
        const double quarter = totalMm * 0.25;
        if (!follow(controller, quarter)) {
            warn(QStringLiteral("stock_bind: FAIL слот stockFollow не найден"));
            ++failures;
        } else {
            const bool computing = waitState(controller, "computing", 5000);
            const QByteArray duringRewind = controller->property("stockDepth").toByteArray();
            const bool kept = duringRewind == fullField;
            say(QStringLiteral("stock_bind: rewind computing=%1 last_field_kept=%2 "
                               "bytes=%3 state=%4")
                    .arg(computing ? 1 : 0)
                    .arg(kept ? 1 : 0)
                    .arg(duringRewind.size())
                    .arg(stateOf(controller)));
            if (!computing || !kept) {
                ++failures;
            }
            if (!waitReady(controller, 120000)) {
                ++failures;
            } else {
                const QByteArray prefix = controller->property("stockDepth").toByteArray();
                const std::uint64_t prefixDigest = bytesDigest(prefix);
                const double progress = controller->property("stockSimProgress").toDouble();
                const int prefixBytes = prefix.size();
                const bool rewound = prefixDigest != fullDigest && !prefix.isEmpty();
                say(QStringLiteral("stock_bind: rewind progress=%1 digest=%2 bytes=%3 "
                                   "changed=%4")
                        .arg(progress, 0, 'f', 2)
                        .arg(static_cast<qulonglong>(prefixDigest), 16, 16, QLatin1Char('0'))
                        .arg(prefixBytes)
                        .arg(rewound ? 1 : 0));
                if (!rewound || progress < 0.2 || progress > 0.3) {
                    ++failures;
                }
                if (!follow(controller, totalMm) || !waitReady(controller, 120000)) {
                    ++failures;
                } else {
                    const QByteArray forward =
                        controller->property("stockDepth").toByteArray();
                    const std::uint64_t forwardDigest = bytesDigest(forward);
                    const double tickMs =
                        controller->property("stockSimTickMs").toDouble();
                    const bool same = forwardDigest == fullDigest
                            && forward.size() == fullField.size();
                    say(QStringLiteral("stock_bind: forward digest=%1 full=%2 equal=%3 "
                                       "tick_ms=%4")
                            .arg(static_cast<qulonglong>(forwardDigest), 16, 16, QLatin1Char('0'))
                            .arg(static_cast<qulonglong>(fullDigest), 16, 16, QLatin1Char('0'))
                            .arg(same ? 1 : 0)
                            .arg(tickMs, 0, 'f', 3));
                    if (!same || tickMs <= 0.0) {
                        ++failures;
                    }
                }
            }
        }
    }

    // Cancelling a live tick: no new field is published, the last whole one stays as
    // it was (the bytes are unchanged), the state is cancelled, the latency is measured.
    if (totalMm > 0.0 && !fullField.isEmpty()) {
        if (!follow(controller, totalMm * 0.5)
            || stateOf(controller) != QLatin1String("computing")
            || !QMetaObject::invokeMethod(controller, "cancelStockSim")) {
            warn(QStringLiteral("stock_bind: FAIL отмена тика не началась"));
            ++failures;
        } else {
            const bool stopped = waitUntil(
                controller,
                [controller] {
                    return stateOf(controller) == QLatin1String("cancelled")
                        && controller->property("stockSimCancelMs").toDouble() >= 0.0;
                },
                30000, QStringLiteral("cancelled tick"));
            const QByteArray after = controller->property("stockDepth").toByteArray();
            const double cancelMs = controller->property("stockSimCancelMs").toDouble();
            const bool kept = after == fullField;
            say(QStringLiteral("stock_bind: cancel tick state=%1 depth_bytes=%2 kept=%3 "
                               "cancel_ms=%4")
                    .arg(stateOf(controller))
                    .arg(after.size())
                    .arg(kept ? 1 : 0)
                    .arg(cancelMs, 0, 'f', 1));
            if (!stopped || !kept || cancelMs < 0.0) {
                ++failures;
            }
        }
    }

    // Cancelling the first pass: the new program has no previous field, so there is
    // nothing to cancel - the box is on screen, there are no bytes.
    qputenv("CNC_QUALITY", "med");
    if (!startLoad(controller, program)) {
        ++failures;
    } else {
        const bool started = waitUntil(
            controller,
            [controller] {
                return stateOf(controller) == QLatin1String("computing")
                    && controller->property("stockSimProgress").toDouble() >= 0.01;
            },
            120000, QStringLiteral("state computing with progress"));
        if (!started) {
            ++failures;
        } else if (!QMetaObject::invokeMethod(controller, "cancelStockSim")) {
            warn(QStringLiteral("stock_bind: FAIL слот cancelStockSim не найден"));
            ++failures;
        } else {
            const bool stopped = waitUntil(
                controller,
                [controller] {
                    return stateOf(controller) == QLatin1String("cancelled")
                        && controller->property("stockSimCancelMs").toDouble() >= 0.0;
                },
                30000, QStringLiteral("cancelled with cancel_ms"));
            const double cancel_ms = controller->property("stockSimCancelMs").toDouble();
            const QByteArray depth = controller->property("stockDepth").toByteArray();
            const bool hasField = controller->property("stockHasField").toBool();
            if (stopped) {
                say(QStringLiteral("stock_bind: cancel first pass cancel_ms=%1 state=%2 "
                                   "depth_bytes=%3 has_field=%4")
                        .arg(cancel_ms, 0, 'f', 1)
                        .arg(stateOf(controller))
                        .arg(depth.size())
                        .arg(hasField ? 1 : 0));
            }
            if (!stopped || !depth.isEmpty() || hasField) {
                ++failures;
            }
        }
    }

    // Refusal: a tool with an undercut (lollipop) is not expressible by a height map.
    // UI_TOOL is checked as a whole - parse, stage, refusal; no field is published,
    // the state is refused, the reason is a refusal line on stderr (it is visible in
    // the output).
    {
        const QString undercutProgram = QStringLiteral(
            "(UI_STOCK: XMIN=-20. XMAX=20. YMIN=-20. YMAX=20. ZMIN=-10. ZMAX=0.)\n"
            "(UI_TOOL: T=7 TYPE=LOLLIPOP D=10. CR=0. TIPD=0. FL=5. SL=5. BL=20. OL=20. NF=2)\n"
            "G21 G90\n"
            "G0 X0 Y0 Z5\n"
            "G1 Z-1 F100\n"
            "G1 X10 F100\n");
        int segments = 0;
        const bool loaded = QMetaObject::invokeMethod(
            controller, "load_string", Q_RETURN_ARG(int, segments),
            Q_ARG(QString, undercutProgram));
        const bool refused = loaded && waitState(controller, "refused", 30000);
        const QByteArray depth = controller->property("stockDepth").toByteArray();
        const bool hasField = controller->property("stockHasField").toBool();
        const int nodes = controller->property("stockCountX").toInt()
                * controller->property("stockCountY").toInt();
        say(QStringLiteral("stock_bind: undercut loaded=%1 segments=%2 state=%3 "
                           "depth_bytes=%4 has_field=%5 nodes=%6")
                .arg(loaded ? 1 : 0)
                .arg(segments)
                .arg(stateOf(controller))
                .arg(depth.size())
                .arg(hasField ? 1 : 0)
                .arg(nodes));
        if (!refused || !depth.isEmpty() || hasField || nodes != 0) {
            ++failures;
        }
    }

    if (failures == 0) {
        say(QStringLiteral("RESULT: OK"));
        return 0;
    }
    say(QStringLiteral("RESULT: FAIL (%1)").arg(failures));
    return 1;
}
