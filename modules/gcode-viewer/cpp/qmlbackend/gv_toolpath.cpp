// cpp/qmlbackend/gv_toolpath.cpp - контроллер вьювера на C++ (этапы 5.4а - 5.4в).
//
// Разбор УП, подготовка траектории, модель STEP/STL, обводка и силуэт, профиль
// инструмента с оправкой и коллизии считаются ядром gvcore и слоем gvstep
// (Qt-free), установка буферов - геометрии этапа 3 (gvqtgeom). Здесь только
// состояние контроллера и его контракт с QML.
//
// Скан коллизий идёт на пуле потоков: данные пути лежат за shared_ptr, результат
// возвращается вызовом очереди событий и применяется только при совпадении
// токена (collision_token_), поэтому устаревший скан не публикуется.
#include "qmlbackend/gv_toolpath.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QRunnable>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/QQmlEngine>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

#include "core/gcode_parser.h"
#include "core/normals.h"
#include "core/outline.h"
#include "core/packing.h"
#include "core/silhouette.h"
#include "core/stl_import.h"
#include "core/tangents.h"
#include "core/ui_tags.h"
#include "occt/step_bundle.h"
#include "occt/step_edges.h"
#include "occt/step_import.h"

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

// Один и тот же инструмент: у эталона это сравнение двух dict, здесь - полей.
// setToolStage зовётся на каждую смену этапа, и без сравнения геометрия
// инструмента пересобиралась бы на каждый вызов.
bool sameTool(const gv::ToolInfo &a, const gv::ToolInfo &b)
{
    return a.present == b.present && a.number == b.number && a.type == b.type
        && a.diameter == b.diameter && a.corner_radius == b.corner_radius
        && a.taper_deg == b.taper_deg && a.tip_angle_deg == b.tip_angle_deg
        && a.tip_diameter == b.tip_diameter && a.flute_length == b.flute_length
        && a.shoulder_length == b.shoulder_length && a.body_length == b.body_length
        && a.shank_diameter == b.shank_diameter && a.overall_length == b.overall_length
        && a.flutes == b.flutes;
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

// double -> float отдельной функцией: запись float(v) в объявлении с
// инициализацией компилятор разбирает как параметр функции (v - имя
// параметра), а не как приведение.
float f32(double v)
{
    return static_cast<float>(v);
}

// Машинные (X, Y, Z) -> сцена (X, Z, -Y), как _to_3d у эталона.
QVector3D toScene(const std::array<double, 3> &p)
{
    return QVector3D(f32(p[0]), f32(p[2]), f32(-p[1]));
}

// Кватернион по трём столбцам матрицы поворота: повтор ui_tags.quat_from_cols.
// Считается в double, к float приводится конструктором QQuaternion - так же, как
// питоновский вызов QQuaternion(...) с float64-аргументами.
QQuaternion quatFromCols(const std::array<double, 9> &matrix)
{
    const double m00 = matrix[0], m10 = matrix[1], m20 = matrix[2];
    const double m01 = matrix[3], m11 = matrix[4], m21 = matrix[5];
    const double m02 = matrix[6], m12 = matrix[7], m22 = matrix[8];
    const double tr = m00 + m11 + m22;
    if (tr > 0.0) {
        const double s = std::sqrt(tr + 1.0) * 2.0;
        return QQuaternion(float(0.25 * s), float((m21 - m12) / s),
                           float((m02 - m20) / s), float((m10 - m01) / s));
    }
    if (m00 > m11 && m00 > m22) {
        const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
        return QQuaternion(float((m21 - m12) / s), float(0.25 * s),
                           float((m01 + m10) / s), float((m02 + m20) / s));
    }
    if (m11 > m22) {
        const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
        return QQuaternion(float((m02 - m20) / s), float((m01 + m10) / s),
                           float(0.25 * s), float((m12 + m21) / s));
    }
    const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
    return QQuaternion(float((m10 - m01) / s), float((m02 + m20) / s),
                       float((m12 + m21) / s), float(0.25 * s));
}

// Рёбра STEP до склейки (N,2,3 float64 плоским рядом) -> склейка и упаковка:
// повтор пары _coalesce_segments + _pack_edge_segments питоновского эталона
// (controller._load_model_verts, ветка STEP).
gv::PackedEdges packStepEdges(const std::vector<double> &flat)
{
    std::vector<gv::EdgeSegment> segments;
    segments.reserve(flat.size() / 6);
    for (std::size_t i = 0; i + 6 <= flat.size(); i += 6) {
        gv::EdgeSegment segment;
        segment.a = {flat[i], flat[i + 1], flat[i + 2]};
        segment.b = {flat[i + 3], flat[i + 4], flat[i + 5]};
        segments.push_back(segment);
    }
    const std::vector<gv::EdgeSegment> merged = gv::coalesceStepSegments(segments);
    std::vector<double> p0(merged.size() * 3);
    std::vector<double> p1(merged.size() * 3);
    for (std::size_t i = 0; i < merged.size(); ++i) {
        for (std::size_t k = 0; k < 3; ++k) {
            p0[i * 3 + k] = merged[i].a[k];
            p1[i * 3 + k] = merged[i].b[k];
        }
    }
    return gv::packEdgesNp(p0, p1);
}

// Ветка STL: суп треугольников, разворот оболочки наружу, crease-aware нормали и
// топология силуэта - тот же набор, что у питоновского _load_model_verts для STL.
// Обводка STL приходит не отсюда: её считает отдельный проход (у эталона -
// _feature_edges в своём потоке, 0.4 с на Test_model.STL).
bool fillStlModel(const QString &path, ModelData *out, QString *error)
{
    const QByteArray utf8 = path.toUtf8();
    std::vector<double> soup =
        gv::loadStlVertices(std::string(utf8.constData(), std::size_t(utf8.size())));
    if (soup.empty()) {
        *error = QStringLiteral("STL не прочитан или пуст: %1").arg(path);
        return false;
    }
    // Суп перемещается в кожу и разворачивается на месте: лишней копии супа нет.
    out->verts = std::move(soup);
    gv::orientShellOutwardInPlace(out->verts);
    out->normals = gv::smoothFaceNormals(out->verts);
    out->topo = gv::buildSilhouetteTopology(out->verts);
    return true;
}

// Чтение модели, как питоновский controller._load_model_verts: STEP (суп, рёбра
// B-Rep до склейки, нормали и поверхности одним проходом) или STL. Обводка STEP
// склеивается и упаковывается здесь же.
//
// Отказ возвращает nullptr и текст причины: молчаливого отказа нет.
//
// Чтение сериализовано мьютексом: у питоновского эталона ту же роль играет замок
// _occt (OCP отпускает GIL, и два потока на одной форме портят кучу), а здесь
// рабочий поток загрузки и синхронный load_gcode могут идти внахлёст.
std::shared_ptr<ModelData> readModelData(const QString &path, QString *used_path,
                                         QString *error)
{
    static std::mutex read_mutex;
    const std::lock_guard<std::mutex> guard(read_mutex);

    const QString low = path.toLower();
    auto out = std::make_shared<ModelData>();
    if (used_path != nullptr)
        *used_path = path;

    if (low.endsWith(QLatin1String(".stl")))
        return fillStlModel(path, out.get(), error) ? out : nullptr;

    if (!low.endsWith(QLatin1String(".step")) && !low.endsWith(QLatin1String(".stp"))) {
        *error = QStringLiteral("неизвестное расширение модели: %1").arg(path);
        return nullptr;
    }

    const QByteArray utf8 = path.toUtf8();
    const std::string file(utf8.constData(), std::size_t(utf8.size()));
    try {
        gv::StepBundleAll all = gv::loadStepBundleAll(file);
        out->is_step = true;
        // Перемещение: суп, нормали и поверхности едут в материал модели без
        // копии (на Test_model это около 5 МБ).
        out->verts = std::move(all.bundle.verts);
        out->normals = std::move(all.bundle.normals);
        out->surfaces = std::move(all.surfaces);
        out->step_edges = packStepEdges(all.bundle.segments);
        out->has_step_edges = true;
        // Семейства касательных строятся один раз на модель и живут до её смены:
        // режим CNC_SILHOUETTE=analytic берёт их на кадре (у эталона то же место).
        const gv::SkinTriangles skin = gv::buildSkinTriangles(out->verts, out->normals);
        out->families = gv::buildTangentFamilies(out->surfaces, &out->step_edges, &skin);
        out->topo = gv::buildSilhouetteTopology(out->verts);
        return out;
    } catch (const std::exception &exc) {
        // Модель рядом бывает и STL: эталон при отказе STEP переходит на него.
        const QFileInfo info(path);
        const QString base =
            info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
        QString stl;
        const char *const exts[] = {".stl", ".STL", ".Stl"};
        for (const char *ext : exts) {
            const QString candidate = base + QLatin1String(ext);
            if (QFileInfo::exists(candidate)) {
                stl = candidate;
                break;
            }
        }
        reportRefusal(QStringLiteral("STEP не прочитан: %1 (%2)")
                          .arg(path, QString::fromUtf8(exc.what())));
        if (stl.isEmpty()) {
            *error = QStringLiteral("STEP не прочитан: %1").arg(path);
            return nullptr;
        }
        reportRefusal(QStringLiteral("читается STL рядом с моделью: %1").arg(stl));
        if (used_path != nullptr)
            *used_path = stl;
        auto fallback = std::make_shared<ModelData>();
        return fillStlModel(stl, fallback.get(), error) ? fallback : nullptr;
    }
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

    // Ручная поправка положения детали (CNC_OFS_MX/MY/MZ, машинные оси) - то же
    // чтение окружения, что у эталона.
    part_offset_ = {
        qEnvironmentVariable("CNC_OFS_MX", QStringLiteral("0")).toDouble(),
        qEnvironmentVariable("CNC_OFS_MY", QStringLiteral("0")).toDouble(),
        qEnvironmentVariable("CNC_OFS_MZ", QStringLiteral("0")).toDouble(),
    };

    // Оправка постоянна (гайка ER20 и корпус шпинделя): тело, обводка
    // перевёрнутой оболочкой и кольца строятся один раз, как у эталона.
    const std::vector<gv::ProfilePoint> holder_profile = gv::holderSolidProfile();
    setToolProfile(holder_geo_, holder_profile);
    setToolProfile(holder_outline_geo_,
                   gv::toolOutlineProfile(gv::holderOutlineProfile(), 0.1));
    setRingSegments(holder_rings_geo_,
                    gv::ringsSegments(gv::profileRingPoints(holder_profile)));
    holder_radius_ = 1.0;
    for (const gv::ProfilePoint &point : holder_profile) {
        holder_radius_ = std::max(holder_radius_, point.radius_mm);
    }

    // Результат рабочего потока приходит сигналом контракта; соединение прямое:
    // сигнал испускается уже на GUI-потоке (см. load_gcode_async).
    connect(this, &GcodeToolpath::toolpathReady, this, &GcodeToolpath::onToolpathReady);
    connect(this, &GcodeToolpath::meshReady, this, &GcodeToolpath::onMeshReady);

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
    // Полудиагональ габарита кожи: QML берёт её для толщины обводки. Без меша -
    // 1.0, как у эталона.
    if (!has_mesh_)
        return 1.0;
    double sum = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double d = mesh_hi_[std::size_t(i)] - mesh_lo_[std::size_t(i)];
        sum += d * d;
    }
    return 0.5 * std::sqrt(sum);
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
    if (stage_specs_.empty())
        return;
    const int i = std::max(0, std::min(int(stage_specs_.size()) - 1, index));
    const gv::ToolInfo &spec = stage_specs_[std::size_t(i)];
    if (spec.present && !sameTool(spec, tool_spec_))
        applyToolInfo(spec);
}

void GcodeToolpath::analyzeCollisions()
{
    // Скан идёт вне GUI-потока (эталон - свой поток). Без пути, этапов или
    // заготовки сканировать нечего: результат пуст, как у эталона без UI_STOCK.
    if (!data_ || data_->stages.empty() || !stock_bounds_) {
        collisions_.clear();
        emit collisionsChanged();
        return;
    }
    const double total = data_->total;
    std::vector<double> starts;
    starts.reserve(data_->stages.size());
    for (const gv::StageRange &stage : data_->stages)
        starts.push_back(stage.start * total);
    std::vector<double> lens;
    lens.reserve(std::size_t(stage_specs_.size()));
    for (const gv::ToolInfo &spec : stage_specs_)
        lens.push_back(gv::toolExposedLength(spec));
    // Вылет оправки из UI_HOLDER перекрывает профиль: это настоящий вылет.
    const double gauge = holder_tag_ && holder_tag_->has_gauge ? holder_tag_->gauge : 0.0;
    if (gauge > 0.0)
        lens.assign(std::max(std::size_t(1), starts.size()), gauge);
    if (lens.empty())
        lens.push_back(0.0);
    while (lens.size() < starts.size())
        lens.push_back(lens.back());

    const int token = ++collision_token_;
    const std::shared_ptr<const gv::PathData> data = data_;
    const std::array<double, 6> stock = *stock_bounds_;
    const double step = gv::collisionStep();
    QThreadPool::globalInstance()->start(QRunnable::create([this, token, data, starts, lens,
                                                            stock, step] {
        std::vector<gv::CollisionRange> ranges;
        try {
            ranges = gv::scanCollisions(data->pts, data->cum, data->total, starts, lens,
                                        stock, step);
        } catch (const std::exception &exc) {
            reportRefusal(QStringLiteral("скан коллизий не прошёл: %1")
                              .arg(QString::fromUtf8(exc.what())));
            return;
        }
        QMetaObject::invokeMethod(this, [this, token, ranges] {
            if (token != collision_token_)
                return;                              // скан устарел
            collisions_.clear();
            for (const gv::CollisionRange &range : ranges)
                collisions_.append(collisionMap(range));
            emit collisionsChanged();
        }, Qt::QueuedConnection);
    }));
}

void GcodeToolpath::setMachineTools(const QVariantList &tools)
{
    // Таблица стойки: [{number, length, name, enabled}], length - измеренный
    // вылет (мм). Номер без числа (нераспознанный) пропускается, как у эталона.
    machine_tools_.clear();
    for (const QVariant &item : tools) {
        const QVariantMap map = item.toMap();
        bool ok = false;
        const int number = map.value(QStringLiteral("number")).toInt(&ok);
        if (!ok)
            continue;
        gv::MachineTool tool;
        tool.length = map.value(QStringLiteral("length"), 0.0).toDouble();
        tool.name = map.value(QStringLiteral("name")).toString().toStdString();
        tool.enabled = map.value(QStringLiteral("enabled"), true).toBool();
        machine_tools_[number] = tool;
    }
    runToolCheck();
}

void GcodeToolpath::setMachineLimits(const QVariantMap &limits)
{
    // Пустая карта у эталона ложна: пределы тогда не заданы, а не нулевые.
    machine_limits_ = gv::MachineLimits{};
    if (!limits.isEmpty()) {
        machine_limits_.present = true;
        machine_limits_.z_max = limits.value(QStringLiteral("zMax"), 0.0).toDouble();
    }
    runToolCheck();
}

void GcodeToolpath::runToolCheck()
{
    tool_check_.clear();
    if (data_ && !machine_tools_.empty()) {
        try {
            const std::vector<gv::ToolCheckRow> rows = gv::toolCheckReport(
                stage_specs_, data_->stages, data_->pts, data_->cum,
                data_->total, machine_tools_, machine_limits_);
            for (const gv::ToolCheckRow &row : rows)
                tool_check_.append(toolCheckMap(row));
        } catch (const std::exception &exc) {
            reportRefusal(QStringLiteral("сверка инструмента не прошла: %1")
                              .arg(QString::fromUtf8(exc.what())));
            tool_check_.clear();
        }
    }
    emit toolCheckChanged();
}

void GcodeToolpath::refreshTangents(const QVector3D &view_point)
{
    // Глаз приходит в кадре сцены; в raw-кадр модели его переводит обратная
    // трансформа детали (та же, что у видимой геометрии).
    const QVector3D eye(view_point.x() - part_position_.x(),
                        view_point.y() - part_position_.y(),
                        view_point.z() - part_position_.z());
    const QVector3D raw = part_rotation_.inverted().rotatedVector(eye);
    eye_raw_ = raw;
    emit eyeChanged();

    // Режим задаёт CNC_SILHOUETTE: mesh (умолчание) - силуэт по топологии меша,
    // analytic - касательные образующие цилиндров и конусов STEP.
    //
    // Модель без меша выходит здесь же, как эталон: ничего не публикуется и не
    // отказывает. Слот зовётся на каждое движение камеры (десятки раз в секунду),
    // поэтому отказ считается один раз (warnDeferred), а не построчно.
    const double eye_mm[3] = {double(raw.x()), double(raw.y()), double(raw.z())};
    const QByteArray mode = qgetenv("CNC_SILHOUETTE").trimmed().toLower();
    std::vector<float> segments;
    try {
        if (mode == QByteArrayLiteral("analytic")) {
            if (rev_surfaces_.empty())
                return;
            if (!has_tan_fams_) {
                tan_fams_ = gv::buildTangentFamilies(
                    rev_surfaces_, has_step_edges_ ? &step_edges_ : nullptr, nullptr);
                has_tan_fams_ = true;
            }
            const std::vector<double> flat = gv::tangentFromFamilies(tan_fams_, eye_mm);
            segments.reserve(flat.size());
            for (double v : flat)
                segments.push_back(float(v));
        } else {
            if (!has_topo_)
                return;
            segments = gv::meshSilhouetteSegments(mesh_topo_, eye_mm);
        }
    } catch (const std::exception &exc) {
        warnDeferred("silhouette",
                     QStringLiteral("силуэт не посчитан: %1")
                         .arg(QString::fromUtf8(exc.what())));
        return;
    }
    setSilhouetteSegments(segments);
}

// Публикация силуэта: у эталона - set_segments сегментов float32, пустой список
// означает снятие слоя.
void GcodeToolpath::setSilhouetteSegments(const std::vector<float> &segments)
{
    if (segments.empty()) {
        hlr_tangents_->setSegments(nullptr, 0);
        return;
    }
    syncHlrReference();
    hlr_tangents_->setSegments(segments.data(), int(segments.size() / 6));
}

int GcodeToolpath::load_gcode(const QString &filepath)
{
    QString text;
    if (!readTextFile(filepath, &text))
        return 0;
    const int count = load_string(text);           // у строки модели нет
    applyModelPath(findModel(filepath));
    return count;
}

bool GcodeToolpath::loadModel(const QString &filepath)
{
    const QFileInfo info(filepath);
    if (!info.exists())
        return false;
    // Устаревшая асинхронная загрузка отменяется: её результат к этой модели
    // отношения не имеет (у эталона тот же счётчик _load_token).
    ++(*load_token_);
    applyModelPath(QDir::toNativeSeparators(info.absoluteFilePath()));
    emit loadFinished();                           // вся загрузка сделана здесь же
    return true;
}

// Загрузка модели по пути: чтение (STEP или STL) и публикация кожи с обводкой.
// Синхронный путь - load_gcode и loadModel; пустой путь означает, что модели нет,
// и тогда публикуется пустая кожа, как у эталона (_apply_mesh_verts(None)).
void GcodeToolpath::applyModelPath(const QString &path)
{
    QString error;
    QString used = path;
    std::shared_ptr<const ModelData> model;
    if (!path.isEmpty()) {
        try {
            model = readModelData(path, &used, &error);
        } catch (const std::exception &exc) {
            error = QStringLiteral("модель не прочитана: %1 (%2)")
                        .arg(path, QString::fromUtf8(exc.what()));
        }
        if (!model && !error.isEmpty())
            reportRefusal(error);
    }
    setModelPath(model ? used : path);
    applyModelData(model);
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
            // Сайдкар модели разрешает рабочий поток, как у эталона (_worker_load).
            payload.model_path = findModel(filepath);
            // Сигнал испускается на GUI-потоке: вызов отменяется, если объекта
            // контроллера к этому моменту уже нет.
            QMetaObject::invokeMethod(this, [this, payload] {
                emit toolpathReady(QVariant::fromValue(payload));
            }, Qt::QueuedConnection);

            // Вторая фаза: модель рядом с УП (STEP важнее STL). Читается здесь же,
            // на рабочем потоке; в буферы её ставит GUI-поток (meshReady). Обводка
            // STL идёт третьим проходом (startEdgesTask).
            const QString model_path = payload.model_path;
            QString error;
            QString used = model_path;
            std::shared_ptr<const ModelData> model;
            if (!model_path.isEmpty()) {
                try {
                    model = readModelData(model_path, &used, &error);
                } catch (const std::exception &exc) {
                    error = QStringLiteral("модель не прочитана: %1 (%2)")
                                .arg(model_path, QString::fromUtf8(exc.what()));
                }
                if (!model && !error.isEmpty())
                    reportRefusal(error);
            }
            if (token != token_ref->load())
                return;                   // загрузку обогнала новая - модель не нужна
            PreparedModel model_payload;
            model_payload.token = token;
            model_payload.path = model ? used : model_path;
            model_payload.model = model;
            QMetaObject::invokeMethod(this, [this, model_payload] {
                emit meshReady(QVariant::fromValue(model_payload));
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
    // Габарит кожи и силуэт прошлой модели эталон здесь не сбрасывает: строка
    // приходит без файла, а состояние модели живёт до следующей модели.
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
    mesh_->reset();                      // кожу поставит фаза модели
    clearEdges();
    emit meshChanged();
    emit toolpathLoaded(result.data->seg_count);
}

void GcodeToolpath::onMeshReady(const QVariant &payload)
{
    const PreparedModel result = payload.value<PreparedModel>();
    if (result.token != load_token_->load())
        return;                          // модель отменённой загрузки
    setModelPath(result.path);
    applyModelData(result.model);
    // Кожа и обводка стоят: оверлей загрузки уходит, как у эталона.
    setLoading(false);
    emit loadFinished();
}

void GcodeToolpath::applyToolpathData(const std::shared_ptr<const gv::PathData> &data,
                                      const QString &text)
{
    data_ = data;
    // Теги сцены идут первыми, как у эталона: трансформа детали собирается из них
    // и из габарита кожи, а путь публикуется уже с готовым положением детали.
    applySceneTags(text);
    stage_specs_.clear();
    stage_specs_.reserve(data->stages.size());
    for (const gv::StageRange &stage : data->stages)
        stage_specs_.push_back(stage.info);
    gv::ToolInfo first;
    for (const gv::ToolInfo &spec : stage_specs_) {
        if (spec.present) {
            first = spec;
            break;
        }
    }
    // Инструмент без единого UI_TOOL в этапах берётся из тега в тексте УП - как у
    // эталона (_apply_tool_info(parse_ui_tool(text))).
    applyToolInfo(first.present ? first : gv::parseUiTool(text.toStdString()));

    // Заготовка (UI_STOCK) публикуется до трансформы: подгонка модели под её
    // нижний угол без заготовки не работает. Оправка (UI_HOLDER) и её вылет
    // разобраны в applySceneTags; скан коллизий и сверка со станком идут ниже.
    emit stockChanged();
    rebuildPartTransform();

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
    analyzeCollisions();                 // скан коллизий оправки с заготовкой
    runToolCheck();                      // сверка УП со станочной таблицей
}

void GcodeToolpath::applyToolInfo(const gv::ToolInfo &spec)
{
    // Профиль инструмента (core/tooling): тело вращения, обводка перевёрнутой
    // оболочкой и кольца рёбер. Пустой профиль оставляет геометрии пустыми, как
    // у эталона set_profile(None).
    tool_spec_ = spec;
    tool_info_ = toolMap(spec);
    const std::vector<gv::ProfilePoint> profile =
        spec.present ? gv::toolProfile(spec) : std::vector<gv::ProfilePoint>{};
    setToolProfile(tool_geo_, profile);
    tool_length_ = 0.0;
    tool_radius_ = 0.0;
    for (const gv::ProfilePoint &point : profile) {
        tool_length_ = std::max(tool_length_, point.height_mm);
        tool_radius_ = std::max(tool_radius_, point.radius_mm);
    }
    // Обводка силуэта: профиль, раздвинутый на `pad`; QML гасит ближние грани,
    // поэтому виден только наружный контур.
    const double pad = std::max(0.12, 0.05 * tool_radius_);
    setToolProfile(tool_outline_geo_,
                   profile.empty() ? profile : gv::toolOutlineProfile(profile, pad));
    setRingSegments(tool_rings_geo_,
                    profile.empty() ? std::vector<gv::RingSegment>{}
                                    : gv::ringsSegments(gv::profileRingPoints(profile)));
    emit toolInfoChanged();
}

// ---------------------------------------------------------------------------
// Инструмент, оправка и коллизии
// ---------------------------------------------------------------------------
void GcodeToolpath::setToolProfile(ToolGeometry *geo, const std::vector<gv::ProfilePoint> &profile)
{
    if (profile.empty()) {
        geo->setProfile(nullptr, 0);
        return;
    }
    std::vector<double> flat;
    flat.reserve(profile.size() * 2);
    for (const gv::ProfilePoint &point : profile) {
        flat.push_back(point.radius_mm);
        flat.push_back(point.height_mm);
    }
    geo->setProfile(flat.data(), int(profile.size()));
}

void GcodeToolpath::setRingSegments(ToolpathGeometry *geo,
                                    const std::vector<gv::RingSegment> &segments)
{
    std::vector<double> p0;
    std::vector<double> p1;
    p0.reserve(segments.size() * 3);
    p1.reserve(segments.size() * 3);
    for (const gv::RingSegment &segment : segments) {
        p0.push_back(segment.x0);
        p0.push_back(segment.y0);
        p0.push_back(segment.z0);
        p1.push_back(segment.x1);
        p1.push_back(segment.y1);
        p1.push_back(segment.z1);
    }
    const gv::PackedEdges packed = gv::packEdgesNp(p0, p1);
    geo->setPacked(toByteArray(packed.raw), packed.has_bounds ? &packed.bounds : nullptr,
                   packed.nv);
}

QVariantMap GcodeToolpath::toolCheckMap(const gv::ToolCheckRow &row)
{
    QVariantMap map;
    map.insert(QStringLiteral("number"), row.number);
    map.insert(QStringLiteral("name"), QString::fromUtf8(row.name.c_str()));
    map.insert(QStringLiteral("ncLength"), row.nc_length);
    map.insert(QStringLiteral("depth"), row.depth);
    // Нет измеренной длины у стойки - None, как у эталона.
    map.insert(QStringLiteral("machineLength"),
               row.has_machine_length ? QVariant(row.machine_length) : QVariant());
    map.insert(QStringLiteral("delta"), row.has_delta ? QVariant(row.delta) : QVariant());
    map.insert(QStringLiteral("status"), QString::fromLatin1(row.status.c_str()));
    map.insert(QStringLiteral("message"), QString::fromUtf8(row.message.c_str()));
    return map;
}

QVariantMap GcodeToolpath::collisionMap(const gv::CollisionRange &range)
{
    QVariantMap map;
    map.insert(QStringLiteral("d"), range.d);
    map.insert(QStringLiteral("depth"), range.depth);
    map.insert(QStringLiteral("scene"),
               QVariantList{range.scene[0], range.scene[1], range.scene[2]});
    map.insert(QStringLiteral("scene_hb"),
               QVariantList{range.scene_hb[0], range.scene_hb[1], range.scene_hb[2]});
    return map;
}


// ---------------------------------------------------------------------------
// Модель, обводка и трансформа детали
// ---------------------------------------------------------------------------
void GcodeToolpath::applySceneTags(const QString &text)
{
    const std::string utf8 = text.toUtf8().toStdString();
    rot_ = gv::parseUiRotation(utf8);
    model_pos_ = gv::parseUiOrigin(utf8);
    const std::optional<std::array<double, 9>> matrix = gv::parseUiCadMatrix(utf8);
    has_cad_ = matrix.has_value();
    if (matrix)
        cad_matrix_ = *matrix;
    const std::optional<std::array<double, 3>> origin = gv::parseUiCadMatrixOrigin(utf8);
    cad_origin_ = origin ? *origin : std::array<double, 3>{0.0, 0.0, 0.0};

    const std::optional<std::array<double, 6>> stock = gv::parseUiStock(utf8);
    stock_bounds_ = stock;                               // габарит для скана коллизий
    if (stock) {
        const std::array<double, 6> &s = *stock;         // xmin, xmax, ymin, ymax, zmin, zmax
        const std::array<double, 3> center{(s[0] + s[1]) / 2.0,
                                           (s[2] + s[3]) / 2.0,
                                           (s[4] + s[5]) / 2.0};
        stock_center_ = toScene(center);                 // машинные -> сцена
        stock_size_ = QVector3D(f32(s[1] - s[0]), f32(s[5] - s[4]), f32(s[3] - s[2]));
        stock_lower_m_ = {s[0], s[2], s[4]};             // нижний угол, машинные
        has_stock_ = true;
    } else {
        // Габарита в теге нет: заготовка снимается, а её прежние числа остаются
        // (у эталона те же строки и тот же порядок: QML их читает только при
        // hasStock).
        has_stock_ = false;
    }

    // Оправка: вылет (holderGauge) и имя. Без тега вылет нулевой, и скан
    // коллизий берёт вылет из профиля инструмента, как у эталона.
    holder_tag_ = gv::parseUiHolder(utf8);
    holder_gauge_ = holder_tag_ && holder_tag_->has_gauge ? holder_tag_->gauge : 0.0;
}

void GcodeToolpath::rebuildPartTransform()
{
    // Итоговая трансформа: сцена <- база <- WCS <- CAD (эталон
    // _rebuild_part_transform). База разворачивает машинный Z-вверх в сцену
    // Y-вверх, WCS и CAD приходят тегами UI_ROTATION/UI_ORIGIN/UI_CAD_MATRIX.
    const QQuaternion q_base = QQuaternion::fromEulerAngles(-90.0f, 0.0f, 0.0f);
    const QQuaternion q_wcs = QQuaternion::fromEulerAngles(f32(rot_[0]), f32(rot_[1]),
                                                           f32(rot_[2]));
    const QVector3D t_wcs(f32(model_pos_[0]), f32(model_pos_[1]), f32(model_pos_[2]));
    QQuaternion q_cad;
    QVector3D t_cad;
    if (has_cad_) {
        q_cad = quatFromCols(cad_matrix_);
        t_cad = QVector3D(f32(cad_origin_[0]), f32(cad_origin_[1]), f32(cad_origin_[2]));
    } else {
        q_cad = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);     // CAD-трансформы нет
    }
    const QQuaternion q_m = q_wcs * q_cad;               // CAD -> машинные
    const QQuaternion q = q_base * q_m;

    QVector3D t;
    if (has_stock_ && has_mesh_) {
        // Асимметричная подгонка: нижний угол повёрнутого габарита модели
        // совмещается с нижним углом заготовки.
        double rmin[3] = {1e30, 1e30, 1e30};
        // Углы габарита: для каждой оси - обе границы, порядок обхода как у
        // эталона (lo, hi) по x, затем по y, затем по z.
        const double corners[3][2] = {{mesh_lo_[0], mesh_hi_[0]},
                                      {mesh_lo_[1], mesh_hi_[1]},
                                      {mesh_lo_[2], mesh_hi_[2]}};
        for (int ix = 0; ix < 2; ++ix) {
            for (int iy = 0; iy < 2; ++iy) {
                for (int iz = 0; iz < 2; ++iz) {
                    const QVector3D r = q_m.rotatedVector(
                        QVector3D(f32(corners[0][ix]), f32(corners[1][iy]),
                                  f32(corners[2][iz])));
                    rmin[0] = std::min(rmin[0], double(r.x()));
                    rmin[1] = std::min(rmin[1], double(r.y()));
                    rmin[2] = std::min(rmin[2], double(r.z()));
                }
            }
        }
        const std::array<double, 3> shift{stock_lower_m_[0] - rmin[0],
                                          stock_lower_m_[1] - rmin[1],
                                          stock_lower_m_[2] - rmin[2]};
        t = q_base.rotatedVector(QVector3D(f32(shift[0]), f32(shift[1]), f32(shift[2])));
    } else {
        // Регистрация без переноса (флаг _part_auto_center у эталона всегда
        // включён): центр кожи повёрнутой модели ставится в центр траектории.
        const QVector3D rc = q.rotatedVector(mesh_center_);
        t = center() - rc;
    }
    t += toScene(part_offset_);                          // машинные оси -> сцена
    part_rotation_ = q;
    part_position_ = t;
    emit partTransformChanged();
}

void GcodeToolpath::applyModelData(const std::shared_ptr<const ModelData> &model)
{
    // Порядок как у эталона (_apply_mesh_verts): снять прошлую кожу, обводку и
    // силуэт, затем поставить новую кожу и запустить обводку отдельным проходом.
    mesh_->reset();
    clearEdges();                                        // свежая пустая обводка
    hlr_tangents_->setSegments(nullptr, 0);              // силуэт прошлой модели убран
    mesh_center_ = QVector3D(0.0f, 0.0f, 0.0f);
    has_mesh_ = false;
    const int token = ++edge_token_;

    // Состояние чтения модели публикуется даже у меша без вершин: эталон ставит
    // его в _load_model_verts, до установки кожи. Отказ чтения сбрасывает рёбра
    // B-Rep и нормали, а поверхности и семейства оставляет прежними - тоже как
    // эталон (их переписывает только удачная ветка STEP).
    has_step_edges_ = model ? model->has_step_edges : false;
    step_edges_ = model ? model->step_edges : gv::PackedEdges{};
    if (model && model->is_step) {
        rev_surfaces_ = model->surfaces;
        tan_fams_ = model->families;
        has_tan_fams_ = true;
    } else if (model) {
        rev_surfaces_.clear();                           // STL: поверхностей нет
        tan_fams_ = gv::TangentFamilies{};
        has_tan_fams_ = false;
    }
    mesh_topo_ = model ? model->topo : gv::MeshTopology{};

    const bool has_verts = model && !model->verts.empty();
    if (has_verts) {
        // Нормали приходят float32 (F,3,3); MeshGeometry принимает double и сам
        // приводит их к float32 - значение от этого не меняется.
        std::vector<double> normals;
        if (model->normals.size() == model->verts.size()) {
            normals.reserve(model->normals.size());
            for (float v : model->normals)
                normals.push_back(double(v));
        }
        mesh_->setTriangles(model->verts.data(), int(model->verts.size()),
                            normals.empty() ? nullptr : normals.data());
        mesh_lo_ = mesh_->lo();
        mesh_hi_ = mesh_->hi();
    }

    if (has_verts) {
        const std::array<double, 3> lo = mesh_lo_;
        const std::array<double, 3> hi = mesh_hi_;
        mesh_center_ = QVector3D(f32((lo[0] + hi[0]) / 2.0),
                                 f32((lo[1] + hi[1]) / 2.0),
                                 f32((lo[2] + hi[2]) / 2.0));
        has_mesh_ = true;
        has_topo_ = true;
        startEdgesTask(model, token);
        rebuildPartTransform();                          // габарит кожи изменился
    } else {
        mesh_topo_ = gv::MeshTopology{};                 // меша нет - силуэта нет
        has_topo_ = false;
    }
    emit meshChanged();
}

void GcodeToolpath::startEdgesTask(const std::shared_ptr<const ModelData> &model, int token)
{
    // Обводка вторым проходом (эталон - поток _compute_edges): STEP отдаёт её
    // готовой из чтения модели, STL считает рёбра по двугранному углу
    // (_feature_edges, 20 градусов). Работа идёт вне GUI-потока, результат
    // применяется по токену: обводка устаревшей модели не публикуется.
    const std::shared_ptr<const ModelData> shared = model;
    QThreadPool::globalInstance()->start(QRunnable::create([this, token, shared] {
        gv::PackedEdges packed;
        if (shared->has_step_edges) {
            packed = shared->step_edges;
        } else {
            try {
                packed = gv::featureEdgesNp(shared->verts, 20.0);
            } catch (const std::exception &exc) {
                reportRefusal(QStringLiteral("обводка меша не посчитана: %1")
                                  .arg(QString::fromUtf8(exc.what())));
                return;
            }
        }
        QMetaObject::invokeMethod(this, [this, token, packed] {
            if (token != edge_token_)
                return;                                  // обводка устарела
            setEdgesPacked(packed);
            emit meshChanged();
        }, Qt::QueuedConnection);
    }));
}

void GcodeToolpath::setEdgesPacked(const gv::PackedEdges &packed)
{
    // Свежий объект, а не установка буфера в текущий: QML перезаливает буфер в
    // Model только тогда, когда меняется сам объект геометрии.
    ToolpathGeometry *fresh = new ToolpathGeometry();
    QQmlEngine::setObjectOwnership(fresh, QQmlEngine::CppOwnership);
    fresh->setPacked(toByteArray(packed.raw), packed.has_bounds ? &packed.bounds : nullptr,
                     packed.nv);
    ToolpathGeometry *old = edges_;
    edges_ = fresh;
    // Лента контура (stride 48) идёт тем же содержимым: обводка B-Rep в основной
    // сцене и её позиционная копия для отжима от центра габарита.
    syncHlrReference();
    const std::vector<float> positions = gv::packedPositions(packed);
    hlr_edges_->setSegments(positions.empty() ? nullptr : positions.data(),
                            int(positions.size() / 6));
    if (old)
        old->deleteLater();
}

void GcodeToolpath::syncHlrReference()
{
    // Общий центр габарита кожи: от него обе ленты контура отжимаются наружу
    // (FLOAT_SCALE в HLRLineGeometry), поэтому он один на слои обводки и силуэта.
    const double center[3] = {0.5 * (mesh_lo_[0] + mesh_hi_[0]),
                              0.5 * (mesh_lo_[1] + mesh_hi_[1]),
                              0.5 * (mesh_lo_[2] + mesh_hi_[2])};
    hlr_edges_->setReference(center);
    hlr_tangents_->setReference(center);
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
