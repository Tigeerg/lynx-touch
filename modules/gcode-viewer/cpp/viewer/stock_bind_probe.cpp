// viewer/stock_bind_probe.cpp - проба привязки поля съёма к траектории в
// C++-контроллере (этапы 2-4 симуляции съёма).
//
// Проба грузит настоящий QML-модуль MockBackend и создаёт GcodeToolpath, затем
// зовёт load_gcode_async на реальной УП. Для пресетов med и high печатает узел,
// время фонового прохода, число узлов и размер поля, а для med ещё сверяет
// текстуру рисования (stockField) с опубликованным полем: размер, формат R32F и
// байты обязаны совпасть.
//
// Живой съём (этап 4). После полного поля программа перематывается на четверть
// пути: пока тик считается, состояние computing, а опубликованное поле обязано
// остаться прежним целым (этап 1: поле не снимается на время счёта). Затем тик
// доходит, поле - до четверти пути, и вперёд до конца: дайджест (FNV-1a по
// байтам) обязан совпасть с полным проходом - инкрементальный обход не имеет
// права отличаться от одного прохода. Отмена живого тика публикует не новое
// поле, а последнее целое; отмена первого прохода (поля ещё нет) не публикует
// ничего.
//
// Отказ (этап 2). УП с инструментом TYPE=LOLLIPOP: карта высот поднутрения не
// выражает, поэтому состояние refused, поле пусто (stockHasField ложь, узлов 0),
// а причина - строкой отказа в stderr.
//
// Точные слои реза (PLAN-STOCK.md, 3.1). Геометрии рисования стенки и контура
// (qmlbackend/gv_stockgeom, StockCutWallGeometry и StockCutContourGeometry)
// строятся на синтетическом поле-полуплоскости и сверяются с ожиданием: контур -
// 20 отрезков, стенка - 40 треугольников и 80 вершин, контур - 40 вершин.
//
// Запуск (Qt в PATH; платформа offscreen ставится сама):
// build\viewer\stock_bind_probe.exe [путь к файлу УП]
// Код возврата: 0 - все проверки прошли, 1 - нет.
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
#include <QVariantList>
#include <QVariantMap>

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

// Тот же порядок поиска зависимостей плагина, что у точки входа вьювера:
// каталог модуля и DLL OCCT идут в PATH до создания движка.
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

// Ожидание свойства контроллера с прокруткой очереди событий: сигналы Qt
// доставляются только в цикле, а проба живёт без app.exec.
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

// FNV-1a 64 по байтам поля: живой обход обязан дать тот же буфер, что и полный
// проход, байт в байт (та же сверка, что case stock_live в stock_sim_probe).
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

// Новый проход начинается только после разбора УП: прежнее состояние ready
// держится до onToolpathReady, поэтому сначала ждём computing.
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

// Состояние computing: либо полный проход, либо живой тик. Поле при этом уже
// опубликовано прежнее целое - это и проверяется.
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

// Текстура рисования обязана нести то же поле: шаг 3 берёт высоту вершинного
// шейдера из неё, и разошедшаяся копия нарисовала бы не то, что посчитано.
// Сверяются размер (число узлов), формат (R32F) и байты - копия контроллера
// против опубликованного stockDepth.
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

    // Пресеты: med (0.5 мм) и high (0.2 мм). Уровень читается при запуске
    // прохода, поэтому qputenv перед load_gcode_async достаточно.
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

    // Точка 1 и точка 4 сразу: перемотка на четверть пути. Пока тик считается,
    // на экране обязано остаться прежнее целое поле (состояние computing, байты
    // не изменились), а после тика - поле до четверти, побайтово отличное от
    // полного. Затем вперёд до конца: живой обход обязан дать ровно полный
    // буфер (дайджест равен), иначе инкрементальный съём врёт.
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

    // Отмена живого тика: новое поле не публикуется, последнее целое остаётся
    // как было (байты не изменились), состояние cancelled, задержка измерена.
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

    // Отмена первого прохода: прежнего поля у новой программы нет, поэтому
    // отменять нечего - на экране коробка, байтов нет.
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

    // Отказ: инструмент с поднутрением (лоллипоп) картой высот не выражается.
    // UI_TOOL проверяется целиком - разбор, этап, отказ; поля не публикуется,
    // состояние refused, причина - строкой отказа в stderr (её видно в выводе).
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

    // Точные слои реза (PLAN-STOCK.md, 3.1): новые геометрии qmlbackend/gv_stockgeom
    // (StockCutWallGeometry, StockCutContourGeometry) обязаны строить стенку и
    // контур по полю глубин. Проверка детерминированная: синтетическое поле -
    // полуплоскость (глубина 1 мм при x >= 5), поэтому контур - одна вертикальная
    // линия через 20 клеток строк, то есть ровно 20 отрезков. Стенка - 20 квадов
    // (40 треугольников, 80 вершин), контур - 40 вершин. Числа сверяются с
    // ожиданием, а не с «больше нуля»: так проверка красна, если геометрия
    // потеряет поле или перепутает формат. Габарит обеих - габарит поля.
    {
        const int nx = 21;
        const int ny = 21;
        const double cell = 0.5;
        const double cutMm = 0.05;
        QByteArray depth(nx * ny * int(sizeof(float)), '\0');
        float *values = reinterpret_cast<float *>(depth.data());
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix)
                values[iy * nx + ix] = (ix >= 10) ? 1.0f : 0.0f;   // x >= 5 мм
        }
        const QVariantList bounds{0.0, 10.0, 0.0, 10.0, -5.0, 0.0};
        auto makeGeometry = [&](const QString &type, QString *detail) {
            QQmlComponent component(&engine);
            component.setData(QStringLiteral("import MockBackend 1.0\n%1 {}\n").arg(type).toUtf8(),
                              QUrl(QStringLiteral("qrc:/stock_cut_geometry.qml")));
            QObject *object = component.create();
            if (object == nullptr) {
                *detail = component.errorString();
            } else {
                object->setProperty("boundList", bounds);
                object->setProperty("cellMm", cell);
                object->setProperty("countX", nx);
                object->setProperty("countY", ny);
                object->setProperty("cutMm", cutMm);
                object->setProperty("depthData", depth);
            }
            return object;
        };
        QString error;
        QObject *wall = makeGeometry(QStringLiteral("StockCutWallGeometry"), &error);
        QObject *contour = makeGeometry(QStringLiteral("StockCutContourGeometry"), &error);
        const int wallSegments = wall ? wall->property("segmentCount").toInt() : -1;
        const int contourSegments = contour ? contour->property("segmentCount").toInt() : -1;
        const int wallTris = wall ? wall->property("triangleCount").toInt() : -1;
        const int wallVerts = wall ? wall->property("vertexCount").toInt() : -1;
        const int contourVerts = contour ? contour->property("vertexCount").toInt() : -1;
        const bool boundOk = wall != nullptr
                && wall->property("boundList").toList() == bounds;
        const bool ok = wall != nullptr && contour != nullptr
                && wallSegments == ny - 1 && contourSegments == ny - 1
                && wallTris == 2 * (ny - 1) && wallVerts == 4 * (ny - 1)
                && contourVerts == 2 * (ny - 1) && boundOk;
        say(QStringLiteral("stock_bind: cut layers segments wall=%1 contour=%2 "
                           "tris=%3 verts=%4/%5 bounds=%6 ok=%7")
                .arg(wallSegments).arg(contourSegments).arg(wallTris)
                .arg(wallVerts).arg(contourVerts).arg(boundOk ? 1 : 0).arg(ok ? 1 : 0));
        if (!ok) {
            ++failures;
            if (!error.isEmpty())
                warn(QStringLiteral("stock_bind: FAIL геометрии слоёв реза: %1").arg(error));
        }
    }

    if (failures == 0) {
        say(QStringLiteral("RESULT: OK"));
        return 0;
    }
    say(QStringLiteral("RESULT: FAIL (%1)").arg(failures));
    return 1;
}
