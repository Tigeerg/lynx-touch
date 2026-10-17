/* keyprobe - проба горячих клавиш вьювера на разных раскладках.
 *
 * Зачем: дефект «горячие клавиши требовательны к языку». Проба грузит живой QML
 * вьювера (GcodeViewer3D.qml) в QQuickView и подаёт клавиши так, как их подаёт
 * платформенный плагин Windows: QKeyEvent с nativeScanCode, nativeVirtualKey и
 * текстом, посчитанными по раскладке (MapVirtualKeyEx/ToUnicodeEx). Признак
 * срабатывания - не журнал, а свойства вьювера: каждая горячая клавиша обязана
 * переключить своё (P - пуск, S - заготовка, M - деталь, L - весь код, T - этапы,
 * E - рёбра, D - касательные, X - рентген, F - fps, B - коробка вместо поля
 * съёма).
 *
 * Чем это отличается от живой пробы с SendInput (tools/keyprobe.ps1): живая проба
 * требует переднего окна в незапертой сессии, а перевод скан-кода в VK и символ
 * делает сама система; в закрытой сессии нажатия теряются. Здесь те же числа считает
 * сама проба по раскладке, а поля события заполняются как у настоящего события;
 * событие доставляется в окно вызовом QQuickWindow::keyPressEvent - тем же, каким
 * его отдаёт платформенный плагин, минуя проверку «окно в фокусе». Проверка фокуса
 * измерена отдельно, живой пробой: на живом вьювере Keys.onPressed получает события
 * (focus=true), то есть путь доставки тот же.
 *
 * Запуск (Qt в PATH; картинка не нужна, платформа offscreen ставится сама):
 *   build\viewer\keyprobe.exe data\cad-testdata\Test_model.ngc [ru,en]
 * Код возврата: 0 - все клавиши сработали, 1 - нет, 2 - нечего проверять.
 */
#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMetaObject>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickView>
#include <QStringList>
#include <QTextStream>
#include <QUrl>
#include <QVariant>

#ifndef GV_REPO_ROOT
#  define GV_REPO_ROOT ""
#endif
#ifndef GV_VIEWER_QML
#  define GV_VIEWER_QML ""
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

QString viewerQmlPath()
{
    // CNC_VIEWER_QML - подмена файла вьювера: ею пробник гоняют на прежней редакции
    // обработчика клавиш (проверка обязана быть красной до фикса). То же имя
    // переменной у точки входа (viewer/main.cpp).
    const QString override = qEnvironmentVariable("CNC_VIEWER_QML");
    if (!override.isEmpty())
        return override;
    const QString from_root = repoRoot() + QStringLiteral("/modules/gcode-viewer/GcodeViewer3D.qml");
    if (QFileInfo::exists(from_root))
        return from_root;
    return QString::fromUtf8(GV_VIEWER_QML);
}

QString backendImportPath()
{
    const QString override = qEnvironmentVariable("CNC_CPP_QML_DIR");
    return override.isEmpty() ? repoRoot() + QStringLiteral("/build/qmlbackend") : override;
}

// Каталог DLL OCCT: CNC_OCC_DIR (или окружение conda), подкаталог bin. Те же
// умолчания, что у точки входа вьювера (viewer/main.cpp) и у сборки.
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

// Каталоги в PATH: точка входа QML-плагина разрешает зависимости по PATH, а не через
// add_dll_directory (разбор - PORT-CPP.md, «Ограничения рантайма Qt»).
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

// Клавиша пробы: физический скан-код и свойство вьювера, которое она переключает.
struct ProbeKey
{
    const char *name;
    int scan;
    const char *property;
};

const ProbeKey KEYS[] = {
    { "P", 0x19, "playing" },
    { "S", 0x1F, "showStockVisible" },
    { "M", 0x32, "showPartVisible" },
    { "L", 0x26, "showAllCode" },
    { "T", 0x14, "stageMode" },
    { "E", 0x12, "showBrepEdges" },
    { "D", 0x20, "showTangentLines" },
    { "X", 0x2D, "xray" },
    { "F", 0x21, "showFps" },
    { "B", 0x30, "stockPlainBox" },
};

// Раскладка и номер клавиши по скан-коду: то, что система сообщает о физическом
// нажатии. Раскладка одна на поток, поэтому и VK, и символ берутся по ней.
struct LayoutKey
{
    int scan = 0;
    int vk = 0;
    int key = 0;       // код, который QML видит как event.key
    QChar text;        // символ, который QML видит как event.text (пусто у служебных)
    bool resolved = false;
};

LayoutKey resolveKey(int scan, const QString &klid)
{
    LayoutKey out;
    out.scan = scan;
#ifdef Q_OS_WIN
    const HKL hkl = LoadKeyboardLayoutW(reinterpret_cast<const wchar_t *>(klid.utf16()), KLF_ACTIVATE);
    if (!hkl)
        return out;
    // Скан-код в VK и символ по раскладке: раскладка задана явно (hkl), а не взята
    // из состояния потока, чтобы проба не зависела от того, какая раскладка сейчас
    // выбрана у машины.
    const UINT vk = MapVirtualKeyExW(static_cast<UINT>(scan), MAPVK_VSC_TO_VK, hkl);
    if (!vk)
        return out;
    BYTE state[256] = { 0 };
    wchar_t text[8] = { 0 };
    const int length = ToUnicodeEx(vk, static_cast<UINT>(scan), state, text, 8, 0, hkl);
    int key = 0;
    if (length > 0) {
        out.text = QChar(static_cast<char16_t>(text[0]));
        // Платформенный плагин сообщает как event.key код символа в верхнем регистре
        // (на русской раскладке для клавиши S это Key_Ы), а сам символ отдаёт
        // в event.text: измерено на живом вьювере, key 1067 при text 1099.
        key = out.text.toUpper().unicode();
    } else {
        // Символа нет (служебная клавиша): Qt берёт код из VK.
        key = static_cast<int>(vk);
    }
    out.vk = static_cast<int>(vk);
    out.key = key;
    out.resolved = true;
#endif
    return out;
}

// QQuickWindow::keyPressEvent/keyReleaseEvent объявлены защищёнными: открываем их,
// чтобы подать событие тем же вызовом, каким его подаёт платформенный плагин.
class ProbeView : public QQuickView
{
public:
    using QQuickView::keyPressEvent;
    using QQuickView::keyReleaseEvent;
};

// Нажатие и отпускание: событие подаётся с теми же признаками, что у настоящего
// (код клавиши, скан-код, VK, символ, сочетание).
void press(ProbeView &view, const LayoutKey &key, Qt::KeyboardModifiers modifiers)
{
    QKeyEvent down(QEvent::KeyPress, key.key, modifiers,
                   static_cast<quint32>(key.scan), static_cast<quint32>(key.vk), 0,
                   QString(key.text));
    view.keyPressEvent(&down);
    QKeyEvent up(QEvent::KeyRelease, key.key, modifiers,
                 static_cast<quint32>(key.scan), static_cast<quint32>(key.vk), 0,
                 QString(key.text));
    view.keyReleaseEvent(&up);
}

int run(const QString &program, const QStringList &layouts)
{
    const QString backend_import = backendImportPath();
    const QString backend_module = backend_import + QStringLiteral("/MockBackend");
    if (!QFileInfo::exists(backend_module + QStringLiteral("/qmldir"))) {
        warn(QStringLiteral("keyprobe: FAIL QML-модуль MockBackend не найден: %1/qmldir")
                 .arg(backend_module));
        return 2;
    }
    prependPath({ backend_module, occtBinDir() });

    ProbeView view;
    view.engine()->addImportPath(backend_import);
    view.setSource(QUrl::fromLocalFile(viewerQmlPath()));
    if (view.status() != QQuickView::Ready) {
        warn(QStringLiteral("keyprobe: FAIL QML не загрузился: %1").arg(viewerQmlPath()));
        for (const QQmlError &error : view.errors())
            warn(QStringLiteral("keyprobe: ") + error.toString());
        return 2;
    }
    QQuickItem *root = view.rootObject();
    if (root == nullptr) {
        warn(QStringLiteral("keyprobe: FAIL корневой объект QML пуст"));
        return 2;
    }
    QObject *backend = root->findChild<QObject *>(QStringLiteral("gcodeToolpath"));
    if (backend == nullptr) {
        warn(QStringLiteral("keyprobe: FAIL контроллер gcodeToolpath не найден в QML"));
        return 2;
    }

    // УП нужна не всем клавишам, но P (пуск) и T (этапы) без неё молчат: проба
    // повторяет путь живого запуска, а он грузит программу.
    if (!program.isEmpty()) {
        if (!QFileInfo::exists(program)) {
            warn(QStringLiteral("keyprobe: FAIL файл УП не найден: %1").arg(program));
            return 2;
        }
        int segments = 0;
        QMetaObject::invokeMethod(backend, "load_gcode", Q_RETURN_ARG(int, segments),
                                  Q_ARG(QString, program));
        say(QStringLiteral("keyprobe: УП %1, сегментов: %2").arg(program).arg(segments));
    }

    // Журнал признаков: диагностика вьювера (GcodeViewer3D.qml, keyLogRaw) печатает
    // измеренные key/cp/vk/sc - ими и видно, что именно получает обработчик.
    root->setProperty("keyDebug", true);
    root->forceActiveFocus();
    say(QStringLiteral("keyprobe: focus=%1").arg(root->property("activeFocus").toBool()));

    int fired = 0;
    int total = 0;
    int quiet = 0;
    int quietFailed = 0;
    for (const QString &tag : layouts) {
        const QString klid = (tag == QLatin1String("ru")) ? QStringLiteral("00000419")
                                                          : QStringLiteral("00000409");
        for (const ProbeKey &key : KEYS) {
            const LayoutKey resolved = resolveKey(key.scan, klid);
            if (!resolved.resolved) {
                warn(QStringLiteral("keyprobe: FAIL раскладка %1 недоступна").arg(tag));
                return 2;
            }
            const QByteArray property(key.property);
            const QVariant before = root->property(property.constData());
            press(view, resolved, Qt::NoModifier);
            const QVariant after = root->property(property.constData());
            const bool changed = (before != after);
            total++;
            if (changed)
                fired++;
            say(QStringLiteral("keyprobe: [%1] %2 sc=0x%3 key=%4 cp=%5 vk=0x%6 %7: %8 -> %9 %10")
                    .arg(tag)
                    .arg(QString::fromLatin1(key.name))
                    .arg(resolved.scan, 2, 16, QLatin1Char('0'))
                    .arg(resolved.key)
                    .arg(resolved.text.isNull() ? 0 : resolved.text.unicode())
                    .arg(resolved.vk, 2, 16, QLatin1Char('0'))
                    .arg(QString::fromLatin1(key.property))
                    .arg(before.toString())
                    .arg(after.toString())
                    .arg(changed ? QStringLiteral("сработала") : QStringLiteral("НЕ сработала")));

            // Лишних срабатываний быть не должно: сочетание буквы с Ctrl или Alt -
            // команда приложения (и переключатель раскладки Alt+Shift), а не горячая
            // клавиша вьювера. Проверяется тем же нажатием с признаком сочетания.
            for (Qt::KeyboardModifier modifier : { Qt::ControlModifier, Qt::AltModifier }) {
                const QVariant steady = root->property(property.constData());
                press(view, resolved, modifier);
                quiet++;
                if (root->property(property.constData()) != steady) {
                    quietFailed++;
                    warn(QStringLiteral("keyprobe: [%1] %2 с сочетанием %3 переключила %4")
                             .arg(tag, QString::fromLatin1(key.name))
                             .arg(modifier == Qt::ControlModifier ? QStringLiteral("Ctrl")
                                                                  : QStringLiteral("Alt"))
                             .arg(QString::fromLatin1(key.property)));
                }
            }
        }
        say(QStringLiteral("keyprobe: [%1] сочетания Ctrl/Alt: %2 нажатий, сработало лишних %3")
                .arg(tag).arg(quiet).arg(quietFailed));
        quiet = 0;
        quietFailed = 0;
    }

    if (fired == total && total > 0) {
        say(QStringLiteral("RESULT: OK (%1 из %2)").arg(fired).arg(total));
        return 0;
    }
    say(QStringLiteral("RESULT: FAIL (сработало %1 из %2)").arg(fired).arg(total));
    return 1;
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    // Отдельного окна проба не показывает: событие подаётся прямо в окно вьювера,
    // поэтому платформа offscreen достаточна и не мешает работе за машиной. Платформа
    // выбирается до создания приложения, отсюда и запись окружения здесь.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QString program;
    QStringList layouts;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QLatin1Char('-')))
            continue;
        if (arg == QLatin1String("ru") || arg == QLatin1String("en")) {
            layouts.append(arg);
            continue;
        }
        program = arg;
    }
    if (layouts.isEmpty()) {
        layouts << QStringLiteral("ru") << QStringLiteral("en");
    }
    if (program.isEmpty()) {
        const QString fallback = repoRoot() + QStringLiteral("/data/cad-testdata/Test_model.ngc");
        if (QFileInfo::exists(fallback))
            program = fallback;
    }
    return run(program, layouts);
}
