/* gcodeviewer - C++-точка входа вьювера УП: вьювер запускается без Python.
 *
 * Вьювер целиком на C++: 3D-поверхность - modules/gcode-viewer/GcodeViewer3D.qml
 * (корень - Item), контроллер, геометрии, разбор УП, STEP/STL и коллизии приходят
 * из QML-плагина MockBackend (cpp/qmlbackend). Питоновский бэкенд в пути
 * исполнения не участвует: здесь его нет вовсе, и подменять его нечем.
 *
 * Что делает точка входа:
 * * находит каталог QML-плагина (CNC_CPP_QML_DIR, иначе <репо>/build/qmlbackend)
 * и каталог DLL OCCT (CNC_OCC_DIR, иначе из CONDA_PREFIX) и кладёт оба в PATH
 * до создания движка: точка входа Qt-плагина разрешает зависимости по PATH, а
 * не через add_dll_directory (разбор - PORT-CPP.md, «Ограничения рантайма Qt»).
 * На Linux PATH и LD_LIBRARY_PATH не трогает: Qt и OCCT загрузчик находит по
 * rpath сборки, который CMake заполняет сам (BUILD_RPATH, cpp/CMakeLists.txt);
 * * отдаёт движку путь импорта плагина и грузит GcodeViewer3D.qml в QQuickView:
 * окно создаёт QQuickView, потому что корень вьювера - Item, а не Window, и
 * сам файл вьювера не правится;
 * * берёт у корня объект gcodeToolpath и грузит УП из аргумента. В offscreen
 * (QT_QPA_PLATFORM=offscreen) загрузка синхронная и без цикла событий - по ней
 * сверяют число сегментов; в живом окне - load_gcode_async и печать по
 * сигналам контракта (toolpathLoaded, loadFinished).
 * * перед показом окна отпускает консоль, заведённую для самого процесса
 * (detachOwnConsole): её окно - второе окно верхнего уровня у вьювера, и оно
 * отбирает передний план. Число созданных вьюверов и окон процесса печатается
 * (reportWindows), поэтому «окно одно» - проверка, а не обещание.
 *
 * Ключ --smoke - короткая проверка старта: грузит QML вьювера offscreen и
 * печатает число корневых объектов, тип и размер корня, затем RESULT: OK
 * (parse-level). Проверка неглубокая - "вьювер стартует", геометрия не меряется.
 * Обычный запуск (без ключа) не меняется.
 *
 * Ключ --frame <файл.png> снимает кадр: строит сцену, ждёт нарисованный кадр,
 * берёт его у окна (QQuickWindow::grabWindow) и пишет PNG. Печатает размер
 * картинки и число пикселей не-фона (фон - пиксель левого верхнего угла): по ним
 * видно, попала ли в кадр геометрия. Картинка - доказательство приёмки; числа
 * геометрии доказывают аналитические случаи пробников (stocksurface, stockshell,
 * stock_bind), а не счёт пикселей.
 *
 * Платформа кадра - та, что задана снаружи; пустая даёт живую. Offscreen на
 * Windows сцену Quick3D не рисует (замер: в кадре только элементы интерфейса,
 * 3D пуст), поэтому картинку для приёмки снимают на живой платформе, а
 * QT_QPA_PLATFORM=offscreen оставлен прогону без экрана: он снимает кадр и
 * выходит с кодом 0, сцены в кадре нет.
 *
 * Плагина нет - внятная ошибка и ненулевой код возврата: отката на Python нет.
 *
 * Запуск (Qt обязан быть в PATH до старта процесса: exe линкуется с Qt6Core.dll;
 * каталог плагина и DLL OCCT точка входа добавляет себе сама):
 * set PATH=C:\Qt\6.12.0\msvc2022_64\bin;%PATH%
 * build\viewer\gcodeviewer.exe data\cad-testdata\Test_model.ngc
 * Offscreen: set QT_QPA_PLATFORM=offscreen
 * Дым-прогон: build\viewer\gcodeviewer.exe --smoke
 * Кадр: build\viewer\gcodeviewer.exe --frame frame.png data\cad-testdata\Test_model.ngc
 * (QT_QPA_PLATFORM=offscreen - прогон без экрана: кадр снимается, сцены в нём нет)
 * Linux (Qt и OCCT находит rpath сборки, PATH и LD_LIBRARY_PATH не нужны):
 * build/viewer/gcodeviewer data/cad-testdata/Test_model.ngc
 */
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

#include "core/quality.h"   // gv::presetFromEnv: уровень CNC_QUALITY тем же чтением, что ядро

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

// Вьюверов у точки входа три ветки (дым-прогон, съёмка кадра, живой запуск), и
// в каждой окно одно: корень GcodeViewer3D.qml - Item, поэтому окно заводит
// QQuickView, а сам файл вьювера не правится. Окно - наследник QQuickView со
// счётчиком: число вьюверов печатается на старте (reportWindows), поэтому
// вторая ветка с окном была бы видна числом, а не на глаз.
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

// Окна верхнего уровня процесса: сколько всего и сколько видимых. Меряется
// перечислением окон этого процесса (EnumWindows с отбором по pid), а не
// пересказом. Видимое окно у вьювера одно; остальные (замер: шесть всего) -
// служебные окна Qt и ввода (рамка _q_titlebar, наблюдатели темы и экрана,
// MSCTFIME UI, IME). До правки (0.55.0, запуск с новой консолью) их выходило
// семь, а видимых два: окно вьювера (класс Qt6120QWindowIcon) и окно консоли
// (ConsoleWindowClass), которое система заводит приложению консольной
// подсистемы. Консоль забирала передний план, и нажатия пробы клавиш по
// SendInput до окна вьювера не доходили (tools/keyprobe.ps1, «SendInput ... до
// окна вьювера не доходил»).
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

// Консоль, заведённая ДЛЯ самого процесса, отпускается: её окно и есть второе
// окно верхнего уровня, оно же отбирает передний план у вьювера (замер до
// правки: два видимых окна, см. WindowCount). Консоль родителя (терминала,
// откуда вьювер запустили) не трогают: её окно завёл не вьювер, и именно в неё
// печатается ход загрузки. Перенаправление (конвейер, файл) консолью не
// является, поэтому отпускание консоли вывода не лишает. Цена названа прямо:
// при запуске двойным щелчком окно консоли успевает появиться и гаснет в первые
// миллисекунды - сам вьювер его не показывает.
void detachOwnConsole()
{
#ifdef Q_OS_WIN
    DWORD attached[4] = { 0, 0, 0, 0 };
    // Список процессов консоли: один (наш) - консоль заведена для процесса,
    // больше - консоль общая с родителем.
    if (GetConsoleProcessList(attached, 4) != 1)
        return;
    FreeConsole();
#endif
}

// Число вьюверов и окон верхнего уровня: печатается на старте живого окна и
// съёмки кадра, чтобы «окно одно» было проверкой. В offscreen-прогоне окно не
// показывается, и эта строка не печатается - окна там нет вовсе.
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

// Показ окна вьювера: сначала отпускается своя консоль (разбор - у
// detachOwnConsole), потом окно показывается, потом печатается число окон.
void showViewerWindow(ViewerWindow &view, const QString &branch)
{
    detachOwnConsole();
    view.show();
    reportWindows(branch);
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
// то же, что у сборки (scripts/build-viewer.ps1) и у питоновского шима.
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
// каталоги не дублируются. На Linux PATH не трогают: там и Qt, и OCCT находит
// загрузчик по rpath сборки (BUILD_RPATH, cpp/CMakeLists.txt), и LD_LIBRARY_PATH
// не нужен.
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

// Переключатели чистого вида - у съёмки кадра и у живого окна одни и те же:
// снятие линий УП (CNC_NO_TOOLPATH) и кожи детали (CNC_NO_PART_SKIN). Это те же
// ручки, что клавиши G и K вьювера (GcodeViewer3D.qml): у съёмки кадра клавиш
// нет вовсе, поэтому кадр для сравнения вида снимают переменной. Без переменных
// поведение прежнее - и линии, и кожа видны.
void applyCleanViewEnv(QQuickItem *root_item)
{
    root_item->setProperty("showToolpath", !qEnvironmentVariableIsSet("CNC_NO_TOOLPATH"));
    root_item->setProperty("showPartSkin", !qEnvironmentVariableIsSet("CNC_NO_PART_SKIN"));
}

// CNC_STOCK_VIEW: вид остатка заготовки до старта - та же ручка, что клавиша R
// (GcodeViewer3D.qml, stockView). Четыре вида: exact (точный по узлам поля),
// smooth (гладкий: дроблённый меш, высота билинейная - вид по умолчанию),
// rounded (скруглённый: глубина узла - среднее по окрестности, прежний тумблер
// CNC_STOCK_ROUND) и contour (точный контур: к верху добавляются стенка по
// контуру marching squares и сам контур полилинией). CNC_STOCK_ROUND=1 остаётся
// как сокращение для rounded: переменная была в контракте запуска, и её поведение
// сохранено. Непонятная строка - отказ с причиной в stderr: молча показать не тот
// вид нельзя, кадры сравнивают по видам.
void applyStockViewEnv(QQuickItem *root_item)
{
    const char *names[] = { "exact", "smooth", "rounded", "contour" };
    if (qEnvironmentVariableIsSet("CNC_STOCK_ROUND")) {
        root_item->setProperty("stockView", 2);
    }
    const QString text = qEnvironmentVariable("CNC_STOCK_VIEW").trimmed().toLower();
    if (text.isEmpty())
        return;
    for (int i = 0; i < 4; ++i) {
        if (text == QLatin1String(names[i])) {
            root_item->setProperty("stockView", i);
            return;
        }
    }
    warn(QStringLiteral("gcodeviewer: CNC_STOCK_VIEW: вид не разобран: %1 "
                        "(ожидается exact, smooth, rounded или contour)")
                 .arg(text));
}

// Цвета палитры остатка материала до старта. Пара "переменная -> свойство" одной
// таблицей: свойство у обоих цветов однотипное, а имя переменной - часть
// контракта запуска (README, GcodeViewer3D.qml).
struct StockColorEnv {
    const char *variable;
    const char *property;
};

// CNC_STOCK_RAW и CNC_STOCK_CUT ставят цвета палитры по "#RRGGBB" (или
// "#AARRGGBB") - те же две ручки, что пресеты вьювера и клавиша C
// (GcodeViewer3D.qml, stockPresets): переменная задаёт цвет пары до старта,
// пресет остаётся тем, что был. CNC_STOCK_CONTOUR - цвет точного контура реза
// (вид contour): он вне палитры, потому что линия меры, а не материал. Непонятная
// строка - отказ с причиной в stderr: молча красить слиток в чёрный нельзя, по
// его цвету читают, где металл снят.
void applyStockColorEnv(QQuickItem *root_item)
{
    const StockColorEnv vars[] = {
        { "CNC_STOCK_RAW", "stockRawColor" },
        { "CNC_STOCK_CUT", "stockCutColor" },
        { "CNC_STOCK_CONTOUR", "stockContourColor" },
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

// CNC_STOCK_GHOST: заготовка полупрозрачной до старта - та же ручка, что клавиша
// V (GcodeViewer3D.qml, stockGhost). Стекло теперь умолчание, поэтому переменная
// не включает вид, а снимает его: пустая переменная или 1/true/on оставляют
// стекло, а 0/false/off/no ставят сплошной до старта - так кадры сравнивают
// виды, как их зовёт оператор, и молчаливой подмены вида нет.
void applyStockGhostEnv(QQuickItem *root_item)
{
    if (!qEnvironmentVariableIsSet("CNC_STOCK_GHOST"))
        return;                                // умолчание вьювера: стекло
    const QString text = qEnvironmentVariable("CNC_STOCK_GHOST").trimmed().toLower();
    const bool solid = text == QLatin1String("0") || text == QLatin1String("false")
            || text == QLatin1String("off") || text == QLatin1String("no");
    root_item->setProperty("stockGhost", !solid);
}

// CNC_PART_MODE: вид детали до старта - та же ручка, что клавиша M
// (GcodeViewer3D.qml, partMode), и единственная: solid - сплошная кожа
// (умолчание), transparent - прозрачная, hidden - деталь скрыта целиком.
// Прозрачная кожа и есть прежний рентген-вид (клавиши X у вьювера больше нет):
// два переключателя одного вида не оставляли понять, какой вид сейчас и чем он
// отличается от соседнего. Непонятная строка - отказ с причиной в stderr: молча
// показать не тот вид нельзя, кадры сравнивают по видам.
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

// CNC_FRAME_CAMERA: камера съёмки кадра явными числами - "yaw,pitch,dist" плюс
// необязательные "panX,panY". Ими снимают кадр одного и того же вида с другой
// точки; прогон без переменной берёт углы вьювера и дистанцию по габариту
// траектории (те же числа, что ставит вьювер). Разбор строгий: неполная или
// нечисловая строка - отказ с причиной в stderr, а не кадр с другим масштабом
// (кадры сравнивают между собой, и молчаливая подмена здесь дороже отказа).
// Возвращает ложь, если строка была задана, но не разобрана.
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

// Камера съёмки кадра: углы, дистанция и панорама ставятся здесь, а не тем, что
// успел сделать вьювер к моменту снятия. Привязка дистанции к габариту
// траектории висит в QML на сигнале pathChanged (GcodeViewer3D.qml:
// dist = max(90, radius * 3 + 60)), поэтому кадр, снятый до этого сигнала и
// после него, показывает разный масштаб - замер 0.53.0: габарит инструмента
// уезжал в шесть разных положений из шести прогонов. Здесь те же числа ставятся
// один раз, уже после загрузки УП и готовности поля: углы читаются у вьювера
// (их не двигает ничто, кроме мыши, а у съёмки мыши нет), дистанция берётся той
// же формулой, что у вьювера, а панорама обнуляется. Затем CNC_FRAME_CAMERA
// перекрывает поставленное - она задана нарочно, и перекрывать её нечем. Силуэт
// инструмента и оправки пересчитывается последним: он строится по глазу камеры,
// и кадр, снятый до пересчёта, показал бы силуэт прежней точки.
// Ложь возврата - CNC_FRAME_CAMERA задана, но не разобрана: кадр тогда снимать
// нельзя, масштаб был бы не тот, что просили.
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

// Дым-прогон (--smoke): загрузить QML вьювера без экрана и доложить разбор.
// Проверка неглубокая: "вьювер вообще стартует", геометрия не меряется. Размер
// корня берётся в том же окне 1024x600, что у обычного запуска.
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

// Съёмка кадра (--frame <файл.png>): построить сцену, дождаться нарисованного
// кадра, забрать его у окна и записать PNG. Числа на выходе (размер картинки и
// число пикселей не-фона) говорят, что в кадре есть геометрия, но доказательство
// геометрии - не они, а аналитические случаи пробников (stocksurface, stockshell,
// stock_bind). Картинка нужна как доказательство приёмки (CLAUDE.md: визуальный
// результат смотрят картинкой).
//
// Платформа берётся снаружи: пустая - живая (на ней Quick3D и рисует сцену),
// offscreen - прогон без экрана (кадр снимается, сцены в нём нет).
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

    // Те же свойства сцены, что у обычного запуска: уровень качества, коробка
    // вместо поля съёма (CNC_STOCK_BOX), переключатели чистого вида
    // (CNC_NO_TOOLPATH, CNC_NO_PART_SKIN, CNC_STOCK_GHOST, CNC_PART_MODE), вид
    // остатка (CNC_STOCK_VIEW, CNC_STOCK_ROUND) и цвета палитры слитка
    // (CNC_STOCK_RAW, CNC_STOCK_CUT). Иначе кадр показывал бы не то, что видит
    // оператор.
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
        // Загрузка синхронная (тот же путь, что у offscreen-прогона): кадр снимают
        // после неё, а не по сигналам фонового потока.
        int segments = 0;
        QMetaObject::invokeMethod(backend, "load_gcode", Q_RETURN_ARG(int, segments),
                                  Q_ARG(QString, program));
        say(QStringLiteral("gcodeviewer: loaded g-code from: %1 segments: %2")
                .arg(program).arg(segments));
    }

    // Поле съёма считается фоном (QThreadPool), поэтому кадр снимается после
    // того, как оно готово: иначе в кадре была бы коробка заготовки, а не
    // остаток материала. Потолок ожидания - 20 с на медленной машине и крупном
    // узле; состояние и признак поля печатаются, чтобы это было видно, а не
    // подразумевалось.
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

    // Камера кадра - детерминированная (разбор в applyFrameCamera): двумя
    // прогонами одной команды сверяют не только картинку, но и эти числа.
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

    // Ждём нарисованные кадры: grabWindow до первого кадра отдаёт пустую картинку.
    // Сигнал frameSwapped отмечает готовый кадр; потолок ожидания - на случай, что
    // сцена не нарисовалась вовсе (тогда кадр всё равно сохраняется и это видно по
    // числу пикселей не-фона).
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

    // Фон - пиксель левого верхнего угла: сцену вьювера он не занимает (там сетка
    // и небо), поэтому разница с ним и есть нарисованная геометрия.
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
    // --smoke и --frame ищут в argv до создания приложения: платформа выбирается
    // раньше него.
    bool smoke = false;
    bool frame = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--smoke") == 0)
            smoke = true;
        if (qstrcmp(argv[i], "--frame") == 0)
            frame = true;
    }
    // Дым-прогон ничего не показывает: без экрана ему достаточно offscreen, а
    // заданную снаружи платформу не трогают (keyprobe.cpp делает так же).
    // Съёмке кадра платформу НЕ навязывают: offscreen-растеризатор Quick3D на
    // Windows сцены не рисует (замер: кадр 1024x600 содержит только элементы
    // интерфейса, 3D пуст), поэтому живая платформа даёт настоящую картинку, а
    // QT_QPA_PLATFORM=offscreen снаружи по-прежнему снимает кадр и выходит с
    // кодом 0 - годен для прогона без экрана.
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
            // Значение идёт следующим аргументом: без него снимать некуда.
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

    if (smoke)
        return runSmoke(viewer_qml, backend_import);
    if (frame)
        return runFrame(viewer_qml, backend_import, program, frame_path);

    ViewerWindow view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1024, 600);              // тот же размер окна, что у съёмки кадра
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

    // Уровень качества из CNC_QUALITY: им корень вьювера задаёт рендер-ручки
    // (GcodeViewer3D.qml, свойство quality). Чтение то же, что у ядра
    // (gv::presetFromEnv): пустое или неизвестное значение даёт med - прежнюю
    // картинку, поэтому без переменной поведение не меняется.
    root_item->setProperty("quality", QString::fromLatin1(gv::presetFromEnv().level));

    // CNC_STOCK_BOX: прежняя коробка вместо поверхности съёма - ею снимают кадр
    // для сравнения (та же ручка, что клавиша B). По умолчанию коробки нет:
    // заготовку рисует поле высот.
    root_item->setProperty("stockPlainBox", qEnvironmentVariableIsSet("CNC_STOCK_BOX"));
    applyCleanViewEnv(root_item);
    applyStockViewEnv(root_item);

    // CNC_STOCK_RAW/CNC_STOCK_CUT: цвета палитры слитка до старта (те же две
    // ручки, что пресеты и клавиша C).
    applyStockColorEnv(root_item);

    // Заготовка полупрозрачной (V) и вид детали (M) до старта.
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

    // Живое окно: разбор УП и модель идут на рабочем потоке, оверлей загрузки
    // ведёт корень вьювера (у питоновской точки входа так же).
    root_item->setProperty("loading", true);
    auto *reporter = new LoadReporter(program, root_item, backend, &app);
    Q_UNUSED(reporter);
    showViewerWindow(view, QStringLiteral("живое окно"));
    QMetaObject::invokeMethod(backend, "load_gcode_async", Q_ARG(QString, program));
    return app.exec();
}

#include "main.moc"
