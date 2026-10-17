/* gcodeviewer - C++-точка входа вьювера УП: вьювер запускается без Python.
 *
 * Вьювер целиком на C++: 3D-поверхность - modules/gcode-viewer/GcodeViewer3D.qml
 * (корень - Item), контроллер, геометрии, разбор УП, STEP/STL и коллизии приходят
 * из QML-плагина MockBackend (cpp/qmlbackend). Питоновский бэкенд в пути
 * исполнения не участвует: здесь его нет вовсе, и подменять его нечем.
 *
 * Что делает точка входа:
 *   * находит каталог QML-плагина (CNC_CPP_QML_DIR, иначе <репо>/build/qmlbackend)
 *     и каталог DLL OCCT (CNC_OCC_DIR, иначе из CONDA_PREFIX) и кладёт оба в PATH
 *     до создания движка: точка входа Qt-плагина разрешает зависимости по PATH, а
 *     не через add_dll_directory (разбор - PORT-CPP.md, «Ограничения рантайма Qt»);
 *   * отдаёт движку путь импорта плагина и грузит GcodeViewer3D.qml в QQuickView:
 *     окно создаёт QQuickView, потому что корень вьювера - Item, а не Window, и
 *     сам файл вьювера не правится;
 *   * берёт у корня объект gcodeToolpath и грузит УП из аргумента. В offscreen
 *     (QT_QPA_PLATFORM=offscreen) загрузка синхронная и без цикла событий - по ней
 *     сверяют число сегментов; в живом окне - load_gcode_async и печать по
 *     сигналам контракта (toolpathLoaded, loadFinished).
 *
 * Плагина нет - внятная ошибка и ненулевой код возврата: отката на Python нет.
 *
 * Запуск (Qt обязан быть в PATH до старта процесса: exe линкуется с Qt6Core.dll;
 * каталог плагина и DLL OCCT точка входа добавляет себе сама):
 *   set PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%
 *   build\viewer\gcodeviewer.exe data\cad-testdata\Test_model.ngc
 * Offscreen: set QT_QPA_PLATFORM=offscreen
 */
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QList>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QTextStream>
#include <QUrl>

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

// Вывод строк в UTF-8; консоль переводится в UTF-8 в main. Технические строки
// (пути, числа, "loaded OK") повторяют формулировки питоновских точек входа: по
// ним сверяют прогон.
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

// Предупреждения движка QML - отдельной строкой, как у питоновских точек входа:
// живой прогон обязан быть без "[QML warning]".
void reportQmlWarnings(const QList<QQmlError> &errors)
{
    for (const QQmlError &error : errors)
        warn(QStringLiteral("[QML warning] ") + error.toString());
}

// Корень репозитория: exe лежит в <репо>/build/viewer (цель gcodeviewer), значит
// два уровня вверх. Догадка проверяется файлом вьювера: сборка с другим каталогом
// сборки не должна уводить поиск в чужое дерево - тогда берётся корень, записанный
// при сборке.
QString repoRoot()
{
    const QString guess =
        QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
    if (QFileInfo::exists(guess + QStringLiteral("/modules/gcode-viewer/GcodeViewer3D.qml")))
        return guess;
    return QString::fromUtf8(GV_REPO_ROOT);
}

// Файл вьювера: CNC_VIEWER_QML, иначе GcodeViewer3D.qml корня репозитория, иначе
// путь, записанный при сборке.
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

// Каталог DLL OCCT: CNC_OCC_DIR (или окружение conda), подкаталог bin. Умолчание
// то же, что у сборки (scripts/build-viewer.ps1) и у питоновского шима gv_cpp.py.
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

// Каталоги в PATH: поиск зависимостей плагина идёт по PATH, а уже бывшие там
// каталоги не дублируются.
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

// Печать хода загрузки в живом окне. Сигналы контракта приходят из QML-плагина,
// который движок грузит сам: тип контроллера точке входа не виден, поэтому
// принимает их отдельный объект со слотами, а связь задаётся по именам сигналов.
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
    SetConsoleOutputCP(CP_UTF8);          // русский текст виден в консоли распорядителя
#endif
    QGuiApplication app(argc, argv);

    QString program;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("-h") || arg == QLatin1String("--help")) {
            say(QStringLiteral("запуск: gcodeviewer [путь к файлу УП]"));
            return 0;
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

    // Плагина нет - внятная ошибка и ненулевой код: питоновского бэкенда в
    // рантайме нет, подменять нечем.
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

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1024, 600);              // размер окна питоновской точки входа
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

    // Диагностика клавиатуры: CNC_KEY_DEBUG=1 включает в корне вьювера печать
    // признаков нажатия (GcodeViewer3D.qml, keyLogRaw/keyLogState) - ею измеряют
    // доставку клавиш на разных раскладках. По умолчанию вывод молчит.
    root_item->setProperty("keyDebug", qEnvironmentVariableIsSet("CNC_KEY_DEBUG"));

    if (program.isEmpty()) {
        say(QStringLiteral("gcodeviewer: УП не задана: вьювер пуст "
                           "(аргумент - путь к файлу УП)"));
        view.show();
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

    // Живое окно: разбор УП и модель идут на рабочем потоке, оверлей загрузки
    // ведёт корень вьювера (у питоновской точки входа так же).
    root_item->setProperty("loading", true);
    auto *reporter = new LoadReporter(program, root_item, backend, &app);
    Q_UNUSED(reporter);
    view.show();
    QMetaObject::invokeMethod(backend, "load_gcode_async", Q_ARG(QString, program));
    return app.exec();
}

#include "main.moc"
