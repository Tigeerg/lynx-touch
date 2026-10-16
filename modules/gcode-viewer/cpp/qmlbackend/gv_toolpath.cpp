// cpp/qmlbackend/gv_toolpath.cpp - контроллер вьювера на C++ (этап 5.4а).
//
// Разбор УП, подготовка траектории и сборка буферов считает ядро gvcore
// (Qt-free), установка буферов - геометрии этапа 3 (gvqtgeom). Здесь только
// состояние контроллера и его контракт с QML: см. gv_toolpath.h.
#include "qmlbackend/gv_toolpath.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QRunnable>
#include <QSet>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/QQmlEngine>

#include <algorithm>
#include <cstdio>
#include <exception>

#include "core/gcode_parser.h"
#include "core/ui_tags.h"

// Категория журнала контроллера: отказ стадии виден в выводе вьювера.
Q_LOGGING_CATEGORY(gvController, "gv.controller")

namespace gvqml {

namespace {

// Отказ стадии: qCWarning плюс строка в stderr.
//
// Почему два канала: у Qt в сборке PySide6 вывод сообщений по умолчанию выключен -
// замер показал, что qWarning без установленного обработчика не печатает ничего ни
// в stdout, ни в stderr, а с qInstallMessageHandler доходит. Одного qCWarning мало:
// отказ C++-контроллера в процессе вьювера остался бы невидимым. Строка в stderr
// видна всегда, qCWarning - там, где обработчик сообщений установлен.
void reportRefusal(const QString &text)
{
    qCWarning(gvController).noquote() << text;
    const QByteArray utf8 = (QStringLiteral("gv.controller: ") + text).toUtf8();
    std::fwrite(utf8.constData(), 1, std::size_t(utf8.size()), stderr);
    std::fputc('\n', stderr);
}

// Ключи тега UI_TOOL в том порядке, что у питоновского ui_tags.parse_ui_tool:
// QML читает структуру по именам полей, поэтому имена не переводятся.
const char *const TOOL_FIELDS[] = {
    "number", "diameter", "corner_radius", "taper_deg", "tip_angle_deg",
    "tip_diameter", "flute_length", "shoulder_length", "body_length",
    "shank_diameter", "overall_length", "flutes",
};

// Поля UI_TOOL, которые питоновский разбор отдаёт целыми (int(num(...))).
bool isToolIntField(const QString &key)
{
    return key == QLatin1String("number") || key == QLatin1String("flutes");
}

// Разобранный тег UI_TOOL -> QVariantMap контракта QML ({} без тега: пустая
// карта - это "инструмент не описан", как пустой dict у эталона).
QVariantMap toolMap(const gv::ToolInfo &info)
{
    QVariantMap out;
    if (!info.present)
        return out;
    const double values[] = {
        double(info.number), info.diameter, info.corner_radius, info.taper_deg,
        info.tip_angle_deg, info.tip_diameter, info.flute_length,
        info.shoulder_length, info.body_length, info.shank_diameter,
        info.overall_length, double(info.flutes),
    };
    const int count = int(sizeof(TOOL_FIELDS) / sizeof(TOOL_FIELDS[0]));
    for (int i = 0; i < count; ++i) {
        const QString key = QString::fromLatin1(TOOL_FIELDS[i]);
        out.insert(key, isToolIntField(key) ? QVariant(int(values[i])) : QVariant(values[i]));
    }
    out.insert(QStringLiteral("type"), QString::fromUtf8(info.type));
    return out;
}

// Буфер ядра (std::vector) -> QByteArray: пустой вектор остаётся пустым массивом.
QByteArray toByteArray(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return QByteArray();
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}

// Чтение УП в текст: питоновский путь читает utf-8 с errors="ignore", в C++
// неполная последовательность заменяется на U+FFFD (недопустимые байты в УП не
// встречаются, поведение на них названо, а не угадано).
bool readTextFile(const QString &path, QString *text)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        reportRefusal(QStringLiteral("УП не прочитана: %1 (%2)").arg(path, file.errorString()));
        return false;
    }
    *text = QString::fromUtf8(file.readAll());
    return true;
}

// Сайдкар модели рядом с УП: STEP важнее STL (контур STEP - настоящие рёбра
// B-Rep, у STL он восстанавливается по двугранному углу).
QString findModel(const QString &filepath)
{
    const QFileInfo info(filepath);
    const QString stem = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
    const char *const step_ext[] = {".step", ".stp"};
    for (const char *ext : step_ext) {
        const QString candidate = stem + QLatin1String(ext);
        if (QFileInfo::exists(candidate))
            return QDir::toNativeSeparators(QFileInfo(candidate).absoluteFilePath());
    }
    QString stl = stem + QLatin1String(".stl");
    if (!QFileInfo::exists(stl)) {
        const char *const stl_ext[] = {".STL", ".Stl"};
        for (const char *ext : stl_ext) {
            const QString candidate = stem + QLatin1String(ext);
            if (QFileInfo::exists(candidate)) {
                stl = candidate;
                break;
            }
        }
    }
    if (!QFileInfo::exists(stl))
        return QString();
    // Разделители как у путей ОС: свойство читают и человек, и питоновский путь
    // (os.path.abspath у эталона отдаёт обратные слэши).
    return QDir::toNativeSeparators(QFileInfo(stl).absoluteFilePath());
}

// Тяжёлая часть загрузки: разбор УП и подготовка буферов. Свободная функция, а
// не метод: рабочий поток обязан считать её, не заглядывая в объект контроллера
// (объект может исчезнуть, пока загрузка идёт). Ни Qt-объектов, ни замков -
// ядро gvcore чистое.
std::shared_ptr<const gv::PathData> preparePath(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    const gv::ParseResult parsed =
        gv::parseGcode(std::string(utf8.constData(), std::size_t(utf8.size())));
    return std::make_shared<const gv::PathData>(
        gv::prepareOrdered(parsed.ordered, parsed.stages));
}

} // namespace

GcodeToolpath::GcodeToolpath(QObject *parent)
    : QObject(parent)
{
    // Геометрии создаются без родителя: родителем QQuick3DGeometry обязан быть
    // QQuick3DObject, а контроллер - обычный QObject. Владение остаётся у C++,
    // иначе QML освободил бы объект, который ещё показывает сцена.
    rapid_ = new ToolpathGeometry();
    feed_ = new ToolpathGeometry();
    mesh_ = new MeshGeometry();
    edges_ = new ToolpathGeometry();
    hlr_edges_ = new HLRLineGeometry();
    hlr_tangents_ = new HLRLineGeometry();
    tool_geo_ = new ToolGeometry();
    tool_outline_geo_ = new ToolGeometry();
    holder_geo_ = new ToolGeometry();
    holder_outline_geo_ = new ToolGeometry();
    tool_rings_geo_ = new ToolpathGeometry();
    holder_rings_geo_ = new ToolpathGeometry();
    const QVector<QObject *> owned = {
        rapid_, feed_, mesh_, edges_, hlr_edges_, hlr_tangents_, tool_geo_,
        tool_outline_geo_, holder_geo_, holder_outline_geo_, tool_rings_geo_,
        holder_rings_geo_,
    };
    for (QObject *object : owned)
        QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);

    // Результат рабочего потока приходит сигналом контракта; соединение прямое:
    // сигнал испускается уже на GUI-потоке (см. load_gcode_async).
    connect(this, &GcodeToolpath::toolpathReady, this, &GcodeToolpath::onToolpathReady);

    // Отложенно, как у эталона: дать QML закончить привязки, затем сообщить,
    // чтобы привязки пересчитались.
    QTimer::singleShot(0, this, &GcodeToolpath::ready);
}

// ---------------------------------------------------------------------------
// Свойства: данные траектории
// ---------------------------------------------------------------------------
QVector3D GcodeToolpath::center() const
{
    if (!data_)
        return QVector3D(0.0f, 0.0f, 0.0f);
    return QVector3D(float(data_->center[0]), float(data_->center[1]),
                     float(data_->center[2]));
}

int GcodeToolpath::toolIssueCount() const
{
    int count = 0;
    for (const QVariant &row : tool_check_) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("status")).toString() != QLatin1String("ok"))
            ++count;
    }
    return count;
}

double GcodeToolpath::partRadius() const
{
    // Габарита кожи нет: модель ещё не перенесена, поэтому 1.0 - то же значение,
    // что у эталона без меша (QML берёт его для толщины обводки).
    return 1.0;
}

QVector3D GcodeToolpath::toolPosition() const
{
    // Машинные (X, Y, Z) -> сцена (X, Z, -Y), как _to_3d у эталона.
    return QVector3D(float(tool_[0]), float(tool_[2]), float(-tool_[1]));
}

void GcodeToolpath::setToolX(double v)
{
    setTool(v, tool_[1], tool_[2]);
}

void GcodeToolpath::setToolY(double v)
{
    setTool(tool_[0], v, tool_[2]);
}

void GcodeToolpath::setToolZ(double v)
{
    setTool(tool_[0], tool_[1], v);
}

QQuick3DGeometry *GcodeToolpath::rapidGeometry() const { return rapid_; }
QQuick3DGeometry *GcodeToolpath::feedGeometry() const { return feed_; }
QQuick3DGeometry *GcodeToolpath::partGeometry() const { return mesh_; }
QQuick3DGeometry *GcodeToolpath::partEdges() const { return edges_; }
QQuick3DGeometry *GcodeToolpath::hlrEdges() const { return hlr_edges_; }
QQuick3DGeometry *GcodeToolpath::hlrTangents() const { return hlr_tangents_; }
QQuick3DGeometry *GcodeToolpath::toolGeometry() const { return tool_geo_; }
QQuick3DGeometry *GcodeToolpath::toolOutline() const { return tool_outline_geo_; }
QQuick3DGeometry *GcodeToolpath::holderGeometry() const { return holder_geo_; }
QQuick3DGeometry *GcodeToolpath::holderOutline() const { return holder_outline_geo_; }
QQuick3DGeometry *GcodeToolpath::toolRings() const { return tool_rings_geo_; }
QQuick3DGeometry *GcodeToolpath::holderRings() const { return holder_rings_geo_; }

// ---------------------------------------------------------------------------
// Слоты QML
// ---------------------------------------------------------------------------
void GcodeToolpath::setToolStage(int index)
{
    if (stage_tools_.isEmpty())
        return;
    const int i = std::max(0, std::min(int(stage_tools_.size()) - 1, index));
    const QVariantMap info = stage_tools_[i];
    if (!info.isEmpty() && info != tool_info_)
        applyToolInfo(info);
}

void GcodeToolpath::analyzeCollisions()
{
    // Скан оправки о заготовку (collision._scan_collisions) и разбор UI_STOCK
    // ещё не перенесены: заготовки в контроллере нет, поэтому результат пуст -
    // так же ведёт себя эталон без тега UI_STOCK (он тоже не заводит поток).
    collisions_.clear();
    emit collisionsChanged();
}

void GcodeToolpath::setMachineTools(const QVariantList &tools)
{
    machine_tools_ = tools;
    warnDeferred("tool-check",
                 QStringLiteral("станочная таблица инструмента принята, но сверка УП со "
                                "станком (toolCheck) ещё не перенесена: отчёт пуст"));
    runToolCheck();
}

void GcodeToolpath::setMachineLimits(const QVariantMap &limits)
{
    machine_limits_ = limits;
    warnDeferred("tool-check",
                 QStringLiteral("пределы осей приняты, но сверка УП со станком "
                                "(toolCheck) ещё не перенесена: отчёт пуст"));
    runToolCheck();
}

void GcodeToolpath::runToolCheck()
{
    // Отчёт collision._tool_check_report не перенесён: контракт тот же, список пуст.
    tool_check_.clear();
    emit toolCheckChanged();
}

void GcodeToolpath::refreshTangents(const QVector3D &view_point)
{
    // Глаз приходит в кадре сцены; в raw-кадр модели его переводит обратная
    // трансформа детали (та же, что у видимой геометрии).
    const QVector3D eye(view_point.x() - part_position_.x(),
                        view_point.y() - part_position_.y(),
                        view_point.z() - part_position_.z());
    eye_raw_ = part_rotation_.inverted().rotatedVector(eye);
    emit eyeChanged();
    // Сегменты силуэта считаются по топологии меша; она приходит вместе с
    // моделью, а модель в C++ ещё не перенесена. Питоновский эталон при
    // отсутствии меша выходит здесь же: ничего не публикуется и не отказывает.
}

int GcodeToolpath::load_gcode(const QString &filepath)
{
    QString text;
    if (!readTextFile(filepath, &text))
        return 0;
    const int count = load_string(text);
    setModelPath(findModel(filepath));
    // Модель рядом с УП в C++ ещё не читается (шаг 5.4б): modelPath указывает на
    // найденный сайдкар, а кожа и обводка остаются пустыми.
    return count;
}

bool GcodeToolpath::loadModel(const QString &filepath)
{
    warnDeferred("model",
                 QStringLiteral("загрузка модели (STEP/STL) в C++ ещё не перенесена: "
                                "partGeometry и partEdges пусты, файл %1 не читался")
                     .arg(filepath));
    return false;
}

int GcodeToolpath::load_gcode_async(const QString &filepath)
{
    QString text;
    if (!readTextFile(filepath, &text))
        return 0;
    // Токен делает устаревший результат невидимым: новая загрузка его отменяет.
    // Счётчик взят по указателю и передан рабочему потоку копией: он переживёт
    // контроллер, поэтому поток читает счётчик, а не объект.
    const std::shared_ptr<std::atomic<int>> token_ref = load_token_;
    const int token = ++(*token_ref);
    setLoading(true);
    const auto shared = std::make_shared<QString>(text);
    QThreadPool::globalInstance()->start(
        QRunnable::create([this, token, token_ref, filepath, shared] {
            std::shared_ptr<const gv::PathData> data;
            try {
                data = preparePath(*shared);
            } catch (const std::exception &exc) {
                reportRefusal(QStringLiteral("разбор УП не прошёл: %1 (%2)")
                                  .arg(filepath, QString::fromUtf8(exc.what())));
                // Отказ виден и в состоянии: оверлей загрузки не остаётся навсегда.
                QMetaObject::invokeMethod(this, [this] {
                    setLoading(false);
                    emit loadFinished();
                }, Qt::QueuedConnection);
                return;
            }
            if (token != token_ref->load())
                return;                   // загрузку обогнала новая - результат не нужен
            PreparedPath payload;
            payload.token = token;
            payload.text = *shared;
            payload.data = data;
            // Сайдкар модели разрешает рабочий поток, как у эталона (_worker_load):
            // файл он пока не читает (шаг 5.4б), но путь контроллер публикует.
            payload.model_path = findModel(filepath);
            // Сигнал испускается на GUI-потоке: вызов отменяется, если объекта
            // контроллера к этому моменту уже нет.
            QMetaObject::invokeMethod(this, [this, payload] {
                emit toolpathReady(QVariant::fromValue(payload));
            }, Qt::QueuedConnection);
        }));
    return 1;
}

int GcodeToolpath::load_string(const QString &text)
{
    setModelPath(QString());             // у строки нет файла рядом
    std::shared_ptr<const gv::PathData> data;
    try {
        data = preparePath(text);
    } catch (const std::exception &exc) {
        reportRefusal(QStringLiteral("разбор УП не прошёл: %1")
                          .arg(QString::fromUtf8(exc.what())));
        return 0;
    }
    applyToolpathData(data, text);
    mesh_->reset();                      // строка: у программы нет модели
    clearEdges();
    emit meshChanged();
    return data->seg_count;
}

QVector3D GcodeToolpath::sampleAt(double d) const
{
    if (!data_ || data_->pts.empty())
        return QVector3D(0.0f, 0.0f, 0.0f);
    const int n = int(data_->pts.size() / 3);
    if (n < 2)
        return QVector3D(float(data_->pts[0]), float(data_->pts[1]), float(data_->pts[2]));
    const int lo = indexAt(d);
    const int hi = lo + 1;
    const double span = data_->cum[std::size_t(hi)] - data_->cum[std::size_t(lo)];
    const double t = span > 1e-9 ? (d - data_->cum[std::size_t(lo)]) / span : 0.0;
    const double *a = &data_->pts[std::size_t(lo) * 3];
    const double *b = &data_->pts[std::size_t(hi) * 3];
    return QVector3D(float(a[0] + (b[0] - a[0]) * t),
                     float(a[1] + (b[1] - a[1]) * t),
                     float(a[2] + (b[2] - a[2]) * t));
}

bool GcodeToolpath::isRapidAt(double d) const
{
    if (!data_ || data_->rapid.empty())
        return false;
    return data_->rapid[std::size_t(indexAt(d))] != 0;
}

double GcodeToolpath::speedAt(double d) const
{
    if (!data_ || data_->speeds.empty())
        return 0.0;
    return data_->speeds[std::size_t(indexAt(d))];
}

// ---------------------------------------------------------------------------
// Приватная часть
// ---------------------------------------------------------------------------
void GcodeToolpath::onToolpathReady(const QVariant &payload)
{
    const PreparedPath result = payload.value<PreparedPath>();
    if (result.token != load_token_->load())
        return;                          // результат отменённой загрузки
    setModelPath(result.model_path);
    applyToolpathData(result.data, result.text);
    mesh_->reset();                      // кожу заменит фаза модели (шаг 5.4б)
    clearEdges();
    emit meshChanged();
    emit toolpathLoaded(result.data->seg_count);
    // Фазы модели в C++ ещё нет: загрузка завершается сразу после траектории,
    // иначе оверлей загрузки остался бы на экране.
    setLoading(false);
    emit loadFinished();
}

void GcodeToolpath::applyToolpathData(const std::shared_ptr<const gv::PathData> &data,
                                      const QString &text)
{
    data_ = data;
    stage_tools_.clear();
    for (const gv::StageRange &stage : data->stages)
        stage_tools_.append(toolMap(stage.info));
    QVariantMap first;
    for (const QVariantMap &info : stage_tools_) {
        if (!info.isEmpty()) {
            first = info;
            break;
        }
    }
    // Инструмент без единого UI_TOOL в этапах берётся из тега в тексте УП - как у
    // эталона (_apply_tool_info(parse_ui_tool(text))).
    applyToolInfo(first.isEmpty() ? toolMap(gv::parseUiTool(text.toStdString())) : first);

    // Оправка, заготовка и трансформа детали (UI_HOLDER, UI_STOCK, UI_ROTATION,
    // UI_ORIGIN, UI_CAD_MATRIX) приходят вместе с моделью: шаг 5.4б. Свойства
    // остаются пустыми, но сигнал испускается - QML перечитывает их, как у эталона.
    emit stockChanged();

    setTool(data->last[0], data->last[1], data->last[2]);

    stages_.clear();
    for (const gv::StageRange &stage : data->stages) {
        QVariantMap map;
        map.insert(QStringLiteral("start"), stage.start);
        map.insert(QStringLiteral("end"), stage.end);
        map.insert(QStringLiteral("label"), QString::fromUtf8(stage.label));
        map.insert(QStringLiteral("tool"), toolMap(stage.info));
        stages_.append(map);
    }
    move_length_ = data->total;
    min_y_ = data->min_y;
    radius_ = data->radius;

    const gv::Aabb *bounds = data->has_bounds ? &data->bounds : nullptr;
    rapid_->setPacked(toByteArray(data->rapid_stream.bytes), bounds, data->rapid_stream.nv);
    feed_->setPacked(toByteArray(data->feed_stream.bytes), bounds, data->feed_stream.nv);
    emit pathChanged();
    analyzeCollisions();                 // заготовки нет - скан пуст, поток не заводится
    runToolCheck();                      // сверка со станком ещё не перенесена
}

void GcodeToolpath::applyToolInfo(const QVariantMap &info)
{
    tool_info_ = info;
    // Профиль инструмента (tooling.py) и оправка ещё не перенесены: геометрия
    // инструмента, его обводка и кольца остаются пустыми, toolLength и toolRadius -
    // нулевыми, но метаданные UI_TOOL доступны QML, как у эталона.
    emit toolInfoChanged();
}

void GcodeToolpath::setLoading(bool v)
{
    if (v == loading_)
        return;
    loading_ = v;
    emit loadingChanged();
}

void GcodeToolpath::setModelPath(const QString &path)
{
    if (path == model_path_)
        return;
    model_path_ = path;
    emit modelPathChanged();
}

void GcodeToolpath::setTool(double x, double y, double z)
{
    if (x == tool_[0] && y == tool_[1] && z == tool_[2])
        return;
    tool_[0] = x;
    tool_[1] = y;
    tool_[2] = z;
    emit toolChanged();
}

void GcodeToolpath::clearEdges()
{
    // Свежий объект, а не очистка текущего: QML перезаливает буфер в Model даже
    // тогда, когда обводка пустеет после первого кадра.
    ToolpathGeometry *old = edges_;
    edges_ = new ToolpathGeometry();
    QQmlEngine::setObjectOwnership(edges_, QQmlEngine::CppOwnership);
    hlr_edges_->setSegments(nullptr, 0);
    if (old)
        old->deleteLater();
}

int GcodeToolpath::indexAt(double d) const
{
    const std::vector<double> &cum = data_->cum;
    const int n = int(cum.size());
    if (n < 2 || d <= 0.0)
        return 0;
    if (d >= cum[std::size_t(n - 1)])
        return n - 2;
    // Верхняя граница (первый элемент строго больше d) - то же, что
    // numpy.searchsorted(cum, d, side="right").
    const int i = int(std::upper_bound(cum.begin(), cum.end(), d) - cum.begin()) - 1;
    if (i < 0)
        return 0;
    return i > n - 2 ? n - 2 : i;
}

void GcodeToolpath::warnDeferred(const char *key, const QString &text)
{
    static QSet<QString> warned;
    const QString name = QString::fromLatin1(key);
    if (warned.contains(name))
        return;
    warned.insert(name);
    reportRefusal(text);
}

} // namespace gvqml
