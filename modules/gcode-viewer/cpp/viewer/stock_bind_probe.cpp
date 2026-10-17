// viewer/stock_bind_probe.cpp - проба привязки поля съёма к траектории в
// C++-контроллере (этап 2 симуляции съёма).
//
// Проба грузит настоящий QML-модуль MockBackend и создаёт GcodeToolpath, затем
// зовёт load_gcode_async на реальной УП. Для пресетов med и high печатает узел,
// время фонового прохода, число узлов и размер поля, а для med ещё сверяет
// текстуру рисования (stockField) с опубликованным полем: размер, формат R32F и
// байты обязаны совпасть. Затем запускает ещё один проход, ждёт начала счёта и
// отменяет его: печатается задержка от запроса отмены до остановки рабочего
// потока и проверяется, что отменённый проход ничего не опубликовал (поле
// пусто, состояние cancelled).
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

#include <functional>

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
    if (!startLoad(controller, program) || !waitComputing(controller, 120000)
        || !waitReady(controller, 120000)) {
        ++failures;
    } else {
        reportRun(controller, "high");
    }

    // Отмена: дождаться, пока проход реально начнёт считать, и отменить его.
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
            const int bytes = controller->property("stockDepth").toByteArray().size();
            if (stopped) {
                say(QStringLiteral("stock_bind: cancel_ms=%1 state=%2 depth_bytes=%3")
                        .arg(cancel_ms, 0, 'f', 1)
                        .arg(stateOf(controller))
                        .arg(bytes));
            }
            if (!stopped || bytes != 0)
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
