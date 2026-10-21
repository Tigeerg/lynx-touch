/* keyprobe - probe of the viewer hotkeys on different layouts.
 *
 * Why: the defect "hotkeys are language-sensitive". The probe loads the live viewer
 * QML (GcodeViewer3D.qml) into a QQuickView and feeds keys the way the Windows
 * platform plugin does: a QKeyEvent with nativeScanCode, nativeVirtualKey and text
 * computed by the layout (MapVirtualKeyEx/ToUnicodeEx). The sign of firing is not a
 * log but the viewer's properties: each hotkey must toggle its own (P - play,
 * S - stock, M - part view, K - part skin without edges, G - NC lines, L - all
 * code, T - stages, E - edges, D - tangents, F - fps, B - box instead of the
 * removal field, C - ingot palette, V - stock translucent, R - stock remainder
 * view: exact -> smooth -> rounded; for R and M the position cycle is checked too:
 * as many presses as positions must return the view to the initial one, and the
 * intermediate ones must differ).
 * The viewer no longer has key X: the former x-ray is merged into M (transparent
 * skin), and the probe guards that X does not touch the part view.
 * Then it checks the link of the view, not of the switch: the part view depends only
 * on partMode - neither the stock, nor the removal field, nor the quality level
 * change it (a solid skin must stay opaque), and the stock wall side gap is zero -
 * the wall stands on the field bounds (shellGapSideMm).
 *
 * How this differs from a live SendInput probe (tools/keyprobe.ps1): the live probe
 * needs a foreground window in an unlocked session, and the system itself does the
 * scan-code to VK and character conversion; in a locked session the presses are
 * lost. Here the probe computes the same numbers from the layout itself, and the
 * event fields are filled as in a real event; the event is delivered to the window by
 * a QQuickWindow::keyPressEvent call - the same one the platform plugin uses - bypassing
 * the "window in focus" check. The focus check was measured separately, by a live
 * probe: on a live viewer Keys.onPressed receives events (focus=true), that is, the
 * delivery path is the same.
 *
 * Run (Qt in PATH; no image is needed, the offscreen platform is set itself):
 * build\viewer\keyprobe.exe data\cad-testdata\Test_model.ngc [ru,en]
 * Return code: 0 - all keys fired, 1 - not, 2 - nothing to check.
 */
#include <QChar>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
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
#include <QThread>
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
    // CNC_VIEWER_QML - a substitute for the viewer file: it is used to run the probe
    // against a previous revision of the key handler (the check must be red before the
    // fix). The entry point (viewer/main.cpp) uses the same variable name.
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

// OCCT DLL directory: CNC_OCC_DIR (or the conda environment), the bin subdirectory.
// The same defaults as the viewer entry point (viewer/main.cpp) and the build.
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

// Directories into PATH: the QML plugin entry point resolves dependencies via PATH,
// not through add_dll_directory (breakdown - docs/PORT-CPP.md, "Qt runtime
// limitations").
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

// A probe key: the physical scan code and the viewer property it toggles.
struct ProbeKey
{
    const char *name;
    int scan;
    const char *property;
};

const ProbeKey KEYS[] = {
    { "P", 0x19, "playing" },
    { "S", 0x1F, "showStockVisible" },
    { "M", 0x32, "partMode" },
    { "K", 0x25, "showPartSkin" },
    { "G", 0x22, "showToolpath" },
    { "L", 0x26, "showAllCode" },
    { "T", 0x14, "stageMode" },
    { "E", 0x12, "showBrepEdges" },
    { "D", 0x20, "showTangentLines" },
    { "F", 0x21, "showFps" },
    { "B", 0x30, "stockPlainBox" },
    { "C", 0x2E, "stockPreset" },
    { "V", 0x2F, "stockGhost" },
    { "R", 0x13, "stockView" },
};

// Layout and key number by scan code: what the system reports about a physical press.
// The layout is per thread, so both the VK and the character are taken from it.
struct LayoutKey
{
    int scan = 0;
    int vk = 0;
    int key = 0;       // the code QML sees as event.key
    QChar text;        // the character QML sees as event.text (empty for service keys)
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
    // Scan code to VK and character by the layout: the layout is set explicitly (hkl),
    // not taken from the thread state, so the probe does not depend on which layout is
    // currently selected on the machine.
    const UINT vk = MapVirtualKeyExW(static_cast<UINT>(scan), MAPVK_VSC_TO_VK, hkl);
    if (!vk)
        return out;
    BYTE state[256] = { 0 };
    wchar_t text[8] = { 0 };
    const int length = ToUnicodeEx(vk, static_cast<UINT>(scan), state, text, 8, 0, hkl);
    int key = 0;
    if (length > 0) {
        out.text = QChar(static_cast<char16_t>(text[0]));
        // The platform plugin reports the uppercase character code as event.key (on the
        // Russian layout, for key S that is Key_Ы), and the character itself in
        // event.text: measured on a live viewer, key 1067 with text 1099.
        key = out.text.toUpper().unicode();
    } else {
        // No character (a service key): Qt takes the code from the VK.
        key = static_cast<int>(vk);
    }
    out.vk = static_cast<int>(vk);
    out.key = key;
    out.resolved = true;
#endif
    return out;
}

// QQuickWindow::keyPressEvent/keyReleaseEvent are declared protected: we open them up
// to feed the event with the same call the platform plugin uses.
class ProbeView : public QQuickView
{
public:
    using QQuickView::keyPressEvent;
    using QQuickView::keyReleaseEvent;
};

// Press and release: the event is fed with the same signs as a real one (key code,
// scan code, VK, character, modifiers).
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

    // Not all keys need the NC, but P (play) and T (stages) are silent without it: the
    // probe repeats the live-run path, and it loads the program.
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

    // Sign log: the viewer diagnostics (GcodeViewer3D.qml, keyLogRaw) print the
    // measured key/cp/vk/sc - they show exactly what the handler receives.
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

            // There must be no extra triggers: a letter with Ctrl or Alt is an
            // application command (and the layout switch Alt+Shift), not a viewer
            // hotkey. It is checked by the same press with the modifier sign.
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

            // A key with more than two positions (R - stock remainder view: exact,
            // smooth, rounded; M - part view: solid, transparent, hidden) is also
            // checked by a cycle: as many presses as positions must return the property
            // to the initial value, and the intermediate ones must differ. Otherwise
            // "fired" would only mean "something changed", and a stuck position would
            // pass the check. The list of position names is the viewer's own property,
            // not a second counter here: stockViewNames and partModeNames.
            const QByteArray positionsProperty =
                    (property == QByteArray("stockView")) ? QByteArray("stockViewNames")
                  : (property == QByteArray("partMode")) ? QByteArray("partModeNames")
                                                          : QByteArray();
            if (!positionsProperty.isEmpty()) {
                const int start = root->property(property.constData()).toInt();
                const int positions = root->property(positionsProperty.constData()).toList().size();
                for (int step = 1; step <= positions; ++step) {
                    const int from = root->property(property.constData()).toInt();
                    press(view, resolved, Qt::NoModifier);
                    const int to = root->property(property.constData()).toInt();
                    const bool distinct = (step == positions) ? (to == start) : (to != from);
                    say(QStringLiteral("keyprobe: [%1] %2 круг %3/%4 sc=0x%5 key=%6 cp=%7 vk=0x%8 "
                                       "%9: %10 -> %11 %12")
                            .arg(tag)
                            .arg(QString::fromLatin1(key.name))
                            .arg(step)
                            .arg(positions)
                            .arg(resolved.scan, 2, 16, QLatin1Char('0'))
                            .arg(resolved.key)
                            .arg(resolved.text.isNull() ? 0 : resolved.text.unicode())
                            .arg(resolved.vk, 2, 16, QLatin1Char('0'))
                            .arg(QString::fromLatin1(key.property))
                            .arg(from)
                            .arg(to)
                            .arg(distinct ? QStringLiteral("положение своё")
                                          : QStringLiteral("положение НЕ своё")));
                    total++;
                    if (distinct)
                        fired++;
                    else
                        warn(QStringLiteral("keyprobe: [%1] %2: круг положений не сошёлся "
                                            "(%3 -> %4 при %5 положениях)")
                                 .arg(tag, QString::fromLatin1(key.name))
                                 .arg(from).arg(to).arg(positions));
                }
            }
        }
        say(QStringLiteral("keyprobe: [%1] сочетания Ctrl/Alt: %2 нажатий, сработало лишних %3")
                .arg(tag).arg(quiet).arg(quietFailed));
        quiet = 0;
        quietFailed = 0;
    }

    // The part view is one switch and only it. The probe sets a solid part and iterates
    // the stock views, the field and the quality level, checking the skin density: a
    // solid skin must stay opaque (partSkinOpacity 1.0) and be drawn as a solid model
    // (partSkinSolid) with any of them. Before the fix the check is red: the skin went
    // ghost (0.15) with a ready field and returned to solid on key S, that is, the
    // stock decided the part view, not the view (the operator reported: "the part is
    // transparent in any view").
    // Separately checked are the two other positions of the same switch (transparent,
    // hidden) and that the former x-ray key X no longer touches the part view: two
    // switches of one view merged into one (partMode).
    if (backend != nullptr) {
        QString state = backend->property("stockSimState").toString();
        QElapsedTimer wait;
        wait.start();
        while (wait.elapsed() < 20000
               && state != QLatin1String("ready") && state != QLatin1String("refused")
               && state != QLatin1String("idle") && state != QLatin1String("cancelled")) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QThread::msleep(1);
            state = backend->property("stockSimState").toString();
        }
        const bool hasField = backend->property("stockHasField").toBool();
        say(QStringLiteral("keyprobe: вид детали - поле съёма %1 (%2)")
                .arg(hasField ? QStringLiteral("готово") : QStringLiteral("не готово"), state));

        // One check case: what was compared, what came out and whether it matched.
        auto checkLook = [&total, &fired](const QString &name, const QString &got, bool ok) {
            ++total;
            if (ok)
                ++fired;
            else
                warn(QStringLiteral("keyprobe: FAIL %1: %2").arg(name, got));
            say(QStringLiteral("keyprobe: [вид] %1: %2 - %3").arg(name, got)
                    .arg(ok ? QStringLiteral("верно") : QStringLiteral("НЕ верно")));
        };
        const auto solidLook = [&root]() {
            return QStringLiteral("плотность кожи %1, сплошная модель %2")
                    .arg(root->property("partSkinOpacity").toDouble())
                    .arg(root->property("partSkinSolid").toBool() ? QStringLiteral("видна")
                                                                  : QStringLiteral("не видна"));
        };
        const auto solidOk = [&root]() {
            return root->property("partSkinOpacity").toDouble() == 1.0
                    && root->property("partSkinSolid").toBool();
        };

        // A solid part with a ready field and a visible stock - the very case in which
        // the skin used to go ghost.
        root->setProperty("partMode", 0);
        root->setProperty("showPartSkin", true);
        root->setProperty("showStockVisible", true);
        root->setProperty("stockPlainBox", false);
        root->setProperty("quality", QStringLiteral("med"));
        checkLook(QStringLiteral("сплошная при готовом поле"), solidLook(), solidOk());

        // Key S hides the stock (and the removal field with it): the part view does not
        // change from it - this is the reporter's path, not just a property.
        const LayoutKey sKey = resolveKey(0x1F, QStringLiteral("00000409"));   // S
        press(view, sKey, Qt::NoModifier);
        const bool stockHidden = !root->property("showStockVisible").toBool();
        checkLook(QStringLiteral("сплошная при скрытом габарите (S)"), solidLook(),
                  stockHidden && solidOk());
        press(view, sKey, Qt::NoModifier);           // put the stock back

        // Neither a box instead of the field nor the safe level touches the part view:
        // the rule has one input - partMode.
        root->setProperty("stockPlainBox", true);
        checkLook(QStringLiteral("сплошная при коробке вместо поля"), solidLook(), solidOk());
        root->setProperty("stockPlainBox", false);
        root->setProperty("quality", QStringLiteral("safe"));
        checkLook(QStringLiteral("сплошная на уровне safe"), solidLook(), solidOk());
        root->setProperty("quality", QStringLiteral("med"));

        // The two other positions of the same switch.
        root->setProperty("partMode", 1);
        const double clearAlpha = root->property("partSkinOpacity").toDouble();
        checkLook(QStringLiteral("прозрачная деталь"),
                  QStringLiteral("плотность кожи %1, сплошная модель %2, прозрачная %3")
                          .arg(clearAlpha)
                          .arg(root->property("partSkinSolid").toBool() ? QStringLiteral("видна")
                                                                       : QStringLiteral("не видна"))
                          .arg(root->property("partSkinClear").toBool() ? QStringLiteral("видна")
                                                                        : QStringLiteral("не видна")),
                  clearAlpha > 0.0 && clearAlpha < 1.0
                          && !root->property("partSkinSolid").toBool()
                          && root->property("partSkinClear").toBool());
        root->setProperty("partMode", 2);
        checkLook(QStringLiteral("скрытая деталь"),
                  QStringLiteral("узел детали %1")
                          .arg(root->property("showPartVisible").toBool() ? QStringLiteral("виден")
                                                                         : QStringLiteral("скрыт")),
                  !root->property("showPartVisible").toBool());

        // The former x-ray key X: it no longer touches the part view (the press is
        // silent), otherwise one view would be decided by two keys - that very
        // confusion.
        root->setProperty("partMode", 0);
        const double before = root->property("partSkinOpacity").toDouble();
        const int modeBefore = root->property("partMode").toInt();
        const LayoutKey xKey = resolveKey(0x2D, QStringLiteral("00000409"));   // X
        press(view, xKey, Qt::NoModifier);
        checkLook(QStringLiteral("прежняя клавиша X вида не трогает"),
                  QStringLiteral("плотность кожи %1, вид %2")
                          .arg(root->property("partSkinOpacity").toDouble())
                          .arg(root->property("partMode").toInt()),
                  root->property("partSkinOpacity").toDouble() == before
                          && root->property("partMode").toInt() == modeBefore);

        // The stock wall stands on the field bounds: the side gap is zero
        // (GcodeViewer3D.qml, shellGapSideMm). A positive gap shifted the wall outward
        // and left a shelf along the top edge, through which the background and the
        // ingot interior were visible. The check is red before the fix.
        const double sideGap = root->property("shellGapSideMm").toDouble();
        ++total;
        if (sideGap == 0.0)
            ++fired;
        else
            warn(QStringLiteral("keyprobe: FAIL боковой зазор стенок заготовки не ноль: %1 мм")
                     .arg(sideGap));
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
    // The probe shows no separate window: the event is fed straight into the viewer
    // window, so the offscreen platform is enough and does not interfere with work at
    // the machine. The platform is chosen before the application is created, hence the
    // environment write here.
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
