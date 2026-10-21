// cpp/qmlbackend/gv_toolpath.cpp - the C++ viewer controller (stages 5.4a - 5.4c).
//
// NC parsing, path preparation, the STEP/STL model, outline and silhouette, the
// tool profile with the holder and collisions are computed by the gvcore core and
// the gvstep layer (Qt-free); buffer installation is done by the stage 3
// geometries (gvqtgeom). Here there is only the controller state and its contract
// with QML.
//
// The collision scan runs on the thread pool: the path data lives behind
// shared_ptr, the result returns by an event-queue call and is applied only when
// the token matches (collision_token_), so a stale scan is not published.
#include "qmlbackend/gv_toolpath.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QQuick3DTextureData>
#include <QRunnable>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/QQmlEngine>

#include <algorithm>
#include <chrono>
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

// Controller log category: a stage failure is visible in the viewer output.
Q_LOGGING_CATEGORY(gvController, "gv.controller")

namespace gvqml {

namespace {

// Stage failure: qCWarning plus a line in stderr.
//
// Why two channels: in a Qt built for PySide6 message output is off by default -
// measurement showed that qWarning without an installed handler prints nothing to
// stdout or stderr, while with qInstallMessageHandler it gets through. One
// qCWarning is not enough: a C++ controller failure inside the viewer process
// would stay invisible. The stderr line is always visible, qCWarning - where a
// message handler is installed.
// Monotonic microseconds: they measure the removal cancel latency - the GUI thread
// writes the request time, the worker thread subtracts it from its stop time.
long long steadyUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void reportRefusal(const QString &text)
{
    qCWarning(gvController).noquote() << text;
    const QByteArray utf8 = (QStringLiteral("gv.controller: ") + text).toUtf8();
    std::fwrite(utf8.constData(), 1, std::size_t(utf8.size()), stderr);
    std::fputc('\n', stderr);
}

// UI_TOOL tag keys in the order of the reference ui_tags.parse_ui_tool: QML reads
// the structure by field names, so the names are not translated.
const char *const TOOL_FIELDS[] = {
    "number", "diameter", "corner_radius", "taper_deg", "tip_angle_deg",
    "tip_diameter", "flute_length", "shoulder_length", "body_length",
    "shank_diameter", "overall_length", "flutes",
};

// UI_TOOL fields that the reference parse returns as integers (int(num(...))).
bool isToolIntField(const QString &key)
{
    return key == QLatin1String("number") || key == QLatin1String("flutes");
}

// Parsed UI_TOOL tag -> QML contract QVariantMap ({} without the tag: an empty map
// means "tool not described", like the reference's empty dict).
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

// The same tool: in the reference this compares two dicts, here - the fields.
// setToolStage is called on every stage change, and without the comparison the
// tool geometry would be rebuilt on every call.
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

// Core buffer (std::vector) -> QByteArray: an empty vector stays an empty array.
QByteArray toByteArray(const std::vector<unsigned char> &bytes)
{
    if (bytes.empty())
        return QByteArray();
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}

// Read the NC into text: the reference path reads utf-8 with errors="ignore", in
// C++ an incomplete sequence is replaced with U+FFFD (invalid bytes do not occur
// in an NC program; the behaviour on them is named, not guessed).
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

// Model sidecar next to the NC program: STEP matters more than STL (the STEP
// contour is real B-Rep edges, the STL one is recovered by dihedral angle).
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
    // Separators as in OS paths: the property is read by a human and by the
    // reference path (the reference's os.path.abspath returns backslashes).
    return QDir::toNativeSeparators(QFileInfo(stl).absoluteFilePath());
}

// The heavy part of loading: NC parsing and buffer preparation. A free function,
// not a method: a worker thread must compute it without looking into the controller
// object (the object may disappear while loading runs). No Qt objects, no locks -
// the gvcore core is pure.
std::shared_ptr<const gv::PathData> preparePath(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    const gv::ParseResult parsed =
        gv::parseGcode(std::string(utf8.constData(), std::size_t(utf8.size())));
    return std::make_shared<const gv::PathData>(
        gv::prepareOrdered(parsed.ordered, parsed.stages));
}

// double -> float in a separate function: writing float(v) in a declaration with
// an initializer is parsed by the compiler as a function parameter (v is the
// parameter name), not as a cast.
float f32(double v)
{
    return static_cast<float>(v);
}

// Machine (X, Y, Z) -> scene (X, Z, -Y), like the reference _to_3d.
QVector3D toScene(const std::array<double, 3> &p)
{
    return QVector3D(f32(p[0]), f32(p[2]), f32(-p[1]));
}

// Quaternion from three columns of a rotation matrix: mirrors
// ui_tags.quat_from_cols. Computed in double, converted to float by the
// QQuaternion constructor - as the reference QQuaternion(...) call with float64
// arguments.
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

// STEP edges before welding (N,2,3 float64 flat row) -> welding and packing:
// mirrors the pair _coalesce_segments + _pack_edge_segments of the reference
// (controller._load_model_verts, STEP branch). The welding tolerances come from the
// preset.
gv::PackedEdges packStepEdges(const std::vector<double> &flat,
                              const gv::QualityPreset &quality)
{
    std::vector<gv::EdgeSegment> segments;
    segments.reserve(flat.size() / 6);
    for (std::size_t i = 0; i + 6 <= flat.size(); i += 6) {
        gv::EdgeSegment segment;
        segment.a = {flat[i], flat[i + 1], flat[i + 2]};
        segment.b = {flat[i + 3], flat[i + 4], flat[i + 5]};
        segments.push_back(segment);
    }
    const std::vector<gv::EdgeSegment> merged =
        gv::coalesceStepSegments(segments, quality.coalesceDistTolMm,
                                 quality.coalesceGapTolMm);
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

// STL branch: triangle soup, shell flip outward, crease-aware normals and
// silhouette topology - the same set as the reference _load_model_verts for STL.
// The STL outline does not come from here: it is computed by a separate pass (in
// the reference - _feature_edges in its own thread, 0.4 s on Test_model.STL).
bool fillStlModel(const QString &path, ModelData *out, QString *error,
                  const gv::QualityPreset &quality)
{
    const QByteArray utf8 = path.toUtf8();
    std::vector<double> soup =
        gv::loadStlVertices(std::string(utf8.constData(), std::size_t(utf8.size())));
    if (soup.empty()) {
        *error = QStringLiteral("STL не прочитан или пуст: %1").arg(path);
        return false;
    }
    // The soup is moved into the skin and flipped in place: there is no extra soup
    // copy.
    out->verts = std::move(soup);
    gv::orientShellOutwardInPlace(out->verts);
    out->normals = gv::smoothFaceNormals(out->verts, quality.creaseDeg);
    out->topo = gv::buildSilhouetteTopology(out->verts, quality);
    return true;
}

// Model reading, like the reference controller._load_model_verts: STEP (soup,
// B-Rep edges before welding, normals and surfaces in one pass) or STL. The STEP
// outline is welded and packed right here.
//
// A refusal returns nullptr and the reason text: there is no silent failure.
//
// Reading is serialised by a mutex: in the reference the _occt lock plays the same
// role (OCP releases the GIL, and two threads on one shape corrupt the heap), while
// here the loading worker thread and a synchronous load_gcode may overlap.
std::shared_ptr<ModelData> readModelData(const QString &path, QString *used_path,
                                         QString *error)
{
    static std::mutex read_mutex;
    const std::lock_guard<std::mutex> guard(read_mutex);

    // The level is read once per model load: it is used by both reading branches
    // and by the STL outline (startEdgesTask), but not by the per-frame pass.
    const gv::QualityPreset quality = gv::presetFromEnv();

    const QString low = path.toLower();
    auto out = std::make_shared<ModelData>();
    out->quality = quality;
    if (used_path != nullptr)
        *used_path = path;

    if (low.endsWith(QLatin1String(".stl")))
        return fillStlModel(path, out.get(), error, quality) ? out : nullptr;

    if (!low.endsWith(QLatin1String(".step")) && !low.endsWith(QLatin1String(".stp"))) {
        *error = QStringLiteral("неизвестное расширение модели: %1").arg(path);
        return nullptr;
    }

    const QByteArray utf8 = path.toUtf8();
    const std::string file(utf8.constData(), std::size_t(utf8.size()));
    try {
        gv::StepBundleAll all =
            gv::loadStepBundleAll(file, quality.stepDeflectionMm,
                                  quality.edgeDeflectionMm, quality.creaseDeg);
        out->is_step = true;
        // Move: the soup, normals and surfaces travel into the model material
        // without a copy (on Test_model that is about 5 MB).
        out->verts = std::move(all.bundle.verts);
        out->normals = std::move(all.bundle.normals);
        out->surfaces = std::move(all.surfaces);
        out->step_edges = packStepEdges(all.bundle.segments, quality);
        out->has_step_edges = true;
        // Tangent families are built once per model and live until it changes: the
        // CNC_SILHOUETTE=analytic mode takes them per frame (the same place as in
        // the reference).
        const gv::SkinTriangles skin = gv::buildSkinTriangles(out->verts, out->normals);
        out->families = gv::buildTangentFamilies(out->surfaces, &out->step_edges, &skin,
                                                 quality);
        out->topo = gv::buildSilhouetteTopology(out->verts, quality);
        return out;
    } catch (const std::exception &exc) {
        // There may be an STL next to the model: the reference falls back to it when
        // STEP fails.
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
        fallback->quality = quality;
        return fillStlModel(stl, fallback.get(), error, quality) ? fallback : nullptr;
    }
}

} // namespace

GcodeToolpath::GcodeToolpath(QObject *parent)
    : QObject(parent)
{
    // Geometries are created without a parent: the parent of a QQuick3DGeometry must
    // be a QQuick3DObject, and the controller is a plain QObject. Ownership stays
    // with C++, otherwise QML would free an object the scene still shows.
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
    tool_profile_edges_ = new HLRLineGeometry();
    holder_profile_edges_ = new HLRLineGeometry();
    // Removal field as a texture: 1x1 with a zero until the first ready field. The
    // object is created in advance on purpose: a Texture in QML referring to an
    // empty property would be left without data, while drawing takes the field only
    // in the ready state.
    stock_field_ = new QQuick3DTextureData();
    stock_field_->setFormat(QQuick3DTextureData::R32F);
    stock_field_->setSize(QSize(1, 1));
    stock_field_->setTextureData(QByteArray(4, '\0'));
    const QVector<QObject *> owned = {
        rapid_, feed_, mesh_, edges_, hlr_edges_, hlr_tangents_, tool_geo_,
        tool_outline_geo_, holder_geo_, holder_outline_geo_, tool_rings_geo_,
        holder_rings_geo_, tool_profile_edges_, holder_profile_edges_, stock_field_,
    };
    for (QObject *object : owned)
        QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);

    // Manual part position correction (CNC_OFS_MX/MY/MZ, machine axes) - the same
    // environment read as the reference.
    part_offset_ = {
        qEnvironmentVariable("CNC_OFS_MX", QStringLiteral("0")).toDouble(),
        qEnvironmentVariable("CNC_OFS_MY", QStringLiteral("0")).toDouble(),
        qEnvironmentVariable("CNC_OFS_MZ", QStringLiteral("0")).toDouble(),
    };

    // The holder is constant (ER20 nut and spindle body): the body, the
    // inverted-hull outline and the rings are built once, as in the reference.
    holder_profile_ = gv::holderSolidProfile();
    setToolProfile(holder_geo_, holder_profile_);
    setToolProfile(holder_outline_geo_,
                   gv::toolOutlineProfile(gv::holderOutlineProfile(), 0.1));
    setRingSegments(holder_rings_geo_,
                    gv::ringsSegments(gv::profileRingPoints(holder_profile_)));
    holder_tan_fams_ = gv::profileTangentFamilies(holder_profile_);
    holder_radius_ = 1.0;
    for (const gv::ProfilePoint &point : holder_profile_) {
        holder_radius_ = std::max(holder_radius_, point.radius_mm);
    }

    // The worker-thread result arrives by contract signal; the connection is direct:
    // the signal is already emitted on the GUI thread (see load_gcode_async).
    connect(this, &GcodeToolpath::toolpathReady, this, &GcodeToolpath::onToolpathReady);
    connect(this, &GcodeToolpath::meshReady, this, &GcodeToolpath::onMeshReady);

    // Deferred, as in the reference: let QML finish binding, then report so the
    // bindings recompute.
    QTimer::singleShot(0, this, &GcodeToolpath::ready);
}

GcodeToolpath::~GcodeToolpath()
{
    // Pool tasks (loading, outline, collision scan) call the controller by this:
    // reportRefusal from a thread and QMetaObject::invokeMethod(this,...). A task
    // living longer than the object hits a dead QObject - SIGSEGV in
    // QObject::thread on exit (Test_model.ngc, offscreen, code 139; the same at the
    // entry point). waitForDone holds the GUI thread until the tasks finish: their
    // deferred calls are no longer needed by then, and ~QObject discards them. The
    // global pool in this process holds only the controller's tasks, so the wait is
    // bounded by them.
    QThreadPool::globalInstance()->waitForDone();
}

// ---------------------------------------------------------------------------
// Properties: path data
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
    // Half-diagonal of the skin bounds: QML takes it for the outline thickness.
    // Without a mesh - 1.0, as in the reference.
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
    // Machine (X, Y, Z) -> scene (X, Z, -Y), like the reference _to_3d.
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
QQuick3DGeometry *GcodeToolpath::toolProfileEdges() const { return tool_profile_edges_; }
QQuick3DGeometry *GcodeToolpath::holderProfileEdges() const { return holder_profile_edges_; }

// ---------------------------------------------------------------------------
// QML slots
// ---------------------------------------------------------------------------
void GcodeToolpath::setToolStage(int index)
{
    if (stage_specs_.empty())
        return;
    const int i = std::max(0, std::min(int(stage_specs_.size()) - 1, index));
    const gv::ToolInfo &spec = stage_specs_[std::size_t(i)];
    if (spec.present && !sameTool(spec, tool_spec_)) {
        applyToolInfo(spec);
        // Removal is not recomputed here: the field is built from the tools of the
        // program STAGES (stockCutterPlan), and the selected stage is not part of
        // it. A restart would cancel the live walk and return the field to the full
        // program - during playback that would show material not yet removed.
    }
}

void GcodeToolpath::analyzeCollisions()
{
    // The scan runs off the GUI thread (the reference uses its own thread). Without
    // a path, stages or stock there is nothing to scan: the result is empty, as in
    // the reference without UI_STOCK.
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
    // The holder gauge length from UI_HOLDER overrides the profile: this is the real
    // gauge length.
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
                return;                              // the scan is stale
            collisions_.clear();
            for (const gv::CollisionRange &range : ranges)
                collisions_.append(collisionMap(range));
            emit collisionsChanged();
        }, Qt::QueuedConnection);
    }));
}

void GcodeToolpath::clearStockField()
{
    stock_depth_.clear();
    stock_bounds_list_.clear();
    stock_count_x_ = 0;
    stock_count_y_ = 0;
    stock_has_field_ = false;
    // The texture is 1x1 again: the previous bytes do not hold GPU memory, and QML
    // is not left with a texture of a foreign size when there is no field.
    if (stock_field_ != nullptr) {
        stock_field_->setSize(QSize(1, 1));
        stock_field_->setTextureData(QByteArray(4, '\0'));
        stock_field_->update();
    }
}

void GcodeToolpath::startStockSim()
{
    // A program change cancels the old pass: the generation grows, the request time
    // is written before the new pass starts. The worker sees a foreign generation and
    // returns an incomplete field, which is not published.
    const bool was_running = stock_sim_state_ == QLatin1String("computing")
            || stock_tick_in_flight_;
    const std::shared_ptr<StockRunToken> token = stock_token_;
    const int generation = token->generation.fetch_add(1) + 1;
    if (was_running)
        token->cancel_us.store(steadyUs());

    // The field is dropped here and only here: it was computed for the previous
    // path, stock and tools, and the new program has nothing to do with them. After
    // that the field lives until a new one is ready - neither a full pass, nor a
    // tick, nor a seek clears it (DESIGN.md: "while it computes, the last whole
    // field is visible"). The first pass of a program has no previous field, and the
    // stock box stays on screen; a cancelled pass publishes nothing, that is, it
    // leaves the same last whole field on screen.
    stock_session_.reset();
    stock_tick_in_flight_ = false;
    stock_live_target_ = 0.0;
    clearStockField();
    stock_sim_progress_ = 0.0;
    stock_sim_time_ms_ = 0.0;
    stock_sim_tick_ms_ = 0.0;

    // Without a path or UI_STOCK bounds there is nothing to compute. A cancelled
    // pass stays in the cancelled state (no result), otherwise - idle (not computed
    // yet).
    if (!data_ || !stock_bounds_) {
        const QString next = was_running ? QStringLiteral("cancelled") : QStringLiteral("idle");
        const bool changed = next != stock_sim_state_;
        stock_sim_state_ = next;
        emit stockSimChanged();
        emit stockSimProgressChanged();
        if (changed)
            emit stockSimStateChanged();
        return;
    }

    const gv::QualityPreset preset = gv::presetFromEnv();
    const double cell = preset.stockSimCellMm;
    const std::array<double, 6> &s = *stock_bounds_;
    const gv::StockBounds bounds{s[0], s[1], s[2], s[3], s[4], s[5]};
    const std::shared_ptr<const gv::PathData> data = data_;
    const gv::ToolInfo initial = program_tool_;
    stock_cell_mm_ = cell;
    stock_sim_state_ = QStringLiteral("computing");
    emit stockSimChanged();
    emit stockSimProgressChanged();
    emit stockSimStateChanged();

    // The full pass runs to the end of the program: while the operator is not
    // playing, the screen shows the remainder removed by the whole program. The live
    // walk starts from this field.
    QThreadPool::globalInstance()->start(QRunnable::create(
        [this, generation, token, data, bounds, cell, initial] {
            auto session = std::make_shared<StockSession>();
            session->data = data;
            auto payload = std::make_shared<StockPassPayload>();
            const double total = data->total;
            payload->total_mm = total;
            double cancel_ms = -1.0;       // the cancel latency of this pass, ms
            double last_progress = 0.0;
            const double report_step = std::max(1e-6, total / 100.0);
            try {
                if (!session->walk.start(*data, bounds, cell,
                                         gv::stockCutterPlan(*data, initial))) {
                    payload->refused = session->walk.refusedStage() >= 0;
                    payload->refused_stage = session->walk.refusedStage();
                    payload->refused_tool = session->walk.refusedTool();
                } else {
                    const bool done = session->walk.advanceTo(
                        total,
                        [&token, generation, &cancel_ms](double) {
                            if (token->generation.load() == generation)
                                return false;
                            const long long requested = token->cancel_us.load();
                            if (requested >= 0)
                                cancel_ms = double(steadyUs() - requested) / 1000.0;
                            return true;
                        },
                        [this, generation, token, report_step, total,
                         &last_progress](double d) {
                            // Progress - by path length, as in the collision scan;
                            // only 1 percent steps go to the queue, otherwise 70
                            // thousand events per program.
                            if (d - last_progress < report_step)
                                return;
                            last_progress = d;
                            QMetaObject::invokeMethod(this, [this, generation, token, d, total] {
                                if (token->generation.load() != generation)
                                    return;
                                stock_sim_progress_ = total > 0.0 ? std::min(1.0, d / total) : 0.0;
                                emit stockSimProgressChanged();
                            }, Qt::QueuedConnection);
                        });
                    payload->cancelled = !done;
                }
            } catch (const std::exception &exc) {
                reportRefusal(QStringLiteral("съём материала не посчитан: %1")
                                  .arg(QString::fromUtf8(exc.what())));
                payload->failed = true;
            }
            if (payload->cancelled && cancel_ms >= 0.0) {
                // A cancelled pass does not publish a field. Only the latency
                // measurement goes out: it belongs to the cancel, not to the result.
                QMetaObject::invokeMethod(this, [this, generation, cancel_ms] {
                    if (generation < stock_cancel_reported_)
                        return;              // a newer cancel is already measured
                    stock_cancel_reported_ = generation;
                    stock_sim_cancel_ms_ = cancel_ms;
                    emit stockSimChanged();
                }, Qt::QueuedConnection);
            }
            if (payload->cancelled) {
                return;                      // a cancelled pass publishes nothing
            }
            if (!payload->refused && !payload->failed) {
                const gv::StockField &field = session->walk.field();
                const std::vector<float> &depth = field.depthMm();
                payload->depth = depth.empty()
                    ? QByteArray()
                    : QByteArray(reinterpret_cast<const char *>(depth.data()),
                                 qsizetype(depth.size() * sizeof(float)));
                const gv::StockBounds &b = field.bounds();
                payload->bounds = QVariantList{b.xMinMm, b.xMaxMm, b.yMinMm, b.yMaxMm,
                                               b.zMinMm, b.zMaxMm};
                payload->cell_mm = field.cellMm();
                payload->count_x = field.countX();
                payload->count_y = field.countY();
                payload->has_field = !payload->depth.isEmpty();
                payload->target_mm = total;
                // Field reset plus the walk: the same cost that simulateStock
                // returns as two numbers.
                payload->time_ms = session->walk.lastResetMs()
                        + session->walk.advanceMs();
                payload->session = session;
            }
            QMetaObject::invokeMethod(this, [this, generation, token, payload] {
                if (token->generation.load() != generation)
                    return;                  // the program changed while computing
                applyStockPass(*payload);
            }, Qt::QueuedConnection);
        }));
}

void GcodeToolpath::stockFollow(double pathDistMm)
{
    // Live publication: the field must reflect removal up to pathDist. The tick is
    // set by QML (about 10 Hz), here there is only the target and the start; while a
    // tick runs, a call moves the target, and the tick completion catches up.
    if (!stock_session_ || !data_ || !(data_->total > 0.0))
        return;                            // no field yet: the first pass is running
    const double target = std::max(0.0, std::min(data_->total, pathDistMm));
    if (target == stock_live_target_ && !stock_tick_in_flight_)
        return;                            // this is already shown
    stock_live_target_ = target;
    startStockTick();
}

void GcodeToolpath::startStockTick()
{
    if (stock_tick_in_flight_ || !stock_session_) {
        return;
    }
    const std::shared_ptr<StockSession> session = stock_session_;
    const std::shared_ptr<StockRunToken> token = stock_token_;
    const int generation = token->generation.load();
    const double target = stock_live_target_;
    stock_tick_in_flight_ = true;
    // Work is running, and the screen shows the previous whole field: it no longer
    // describes the current pathDist, and the state says so.
    if (stock_sim_state_ != QLatin1String("computing")) {
        stock_sim_state_ = QStringLiteral("computing");
        emit stockSimStateChanged();
    }
    QThreadPool::globalInstance()->start(QRunnable::create(
        [this, session, target, token, generation] {
            const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
            auto payload = std::make_shared<StockPassPayload>();
            payload->total_mm = session->data->total;
            payload->target_mm = target;
            double cancel_ms = -1.0;
            try {
                // Backwards along the path the walk resets the field itself and goes
                // from the start; forwards it continues from the cursor. Both paths
                // give one field (the stock_live check in stock_sim_probe: the digest
                // equals the full pass).
                payload->cancelled = !session->walk.advanceTo(
                    target, [&token, generation, &cancel_ms](double) {
                        if (token->generation.load() == generation)
                            return false;
                        const long long requested = token->cancel_us.load();
                        if (requested >= 0)
                            cancel_ms = double(steadyUs() - requested) / 1000.0;
                        return true;
                    });
            } catch (const std::exception &exc) {
                reportRefusal(QStringLiteral("съём материала не посчитан: %1")
                                  .arg(QString::fromUtf8(exc.what())));
                payload->failed = true;
            }
            payload->time_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
            if (!payload->cancelled && !payload->failed) {
                // Field copy on the worker thread: QQuick3DTextureData has no partial
                // upload, so a tick pays for the whole buffer.
                const gv::StockField &field = session->walk.field();
                const std::vector<float> &depth = field.depthMm();
                payload->depth = depth.empty()
                    ? QByteArray()
                    : QByteArray(reinterpret_cast<const char *>(depth.data()),
                                 qsizetype(depth.size() * sizeof(float)));
                const gv::StockBounds &b = field.bounds();
                payload->bounds = QVariantList{b.xMinMm, b.xMaxMm, b.yMinMm, b.yMaxMm,
                                               b.zMinMm, b.zMaxMm};
                payload->cell_mm = field.cellMm();
                payload->count_x = field.countX();
                payload->count_y = field.countY();
                payload->has_field = !payload->depth.isEmpty();
            }
            QMetaObject::invokeMethod(this, [this, token, generation, payload, cancel_ms] {
                if (token->generation.load() != generation) {
                    // While the tick was computed, the program changed or removal was
                    // cancelled: the tick publishes nothing, only the cancel
                    // measurement.
                    if (cancel_ms >= 0.0 && generation >= stock_cancel_reported_) {
                        stock_cancel_reported_ = generation;
                        stock_sim_cancel_ms_ = cancel_ms;
                        emit stockSimChanged();
                    }
                    return;
                }
                stock_tick_in_flight_ = false;
                if (payload->failed) {
                    applyStockPass(*payload);  // the state stops being computing
                    return;
                }
                if (payload->cancelled || !payload->has_field)
                    return;                  // an incomplete field is not published
                applyStockPass(*payload);
                if (stock_live_target_ != payload->target_mm)
                    startStockTick();        // the target moved during the tick
            }, Qt::QueuedConnection);
        }));
}

void GcodeToolpath::cancelStockSim()
{
    // Cancel without replacement: the generation grows, the next pass starts a new
    // one.
    if (stock_sim_state_ != QLatin1String("computing"))
        return;
    stock_token_->generation.fetch_add(1);
    stock_token_->cancel_us.store(steadyUs());
    stock_tick_in_flight_ = false;
    // No new field is published, the live walk stops: a cancel stops the work but
    // does not rewrite what is already computed - the last whole field stays on
    // screen (if there was one; a cancelled first pass has none).
    stock_session_.reset();
    stock_sim_state_ = QStringLiteral("cancelled");
    emit stockSimChanged();
    emit stockSimProgressChanged();
    emit stockSimStateChanged();
}

void GcodeToolpath::applyStockPass(const StockPassPayload &payload)
{
    if (payload.refused) {
        // The tool is not expressible by a height map: there is no field, the screen
        // shows the stock box, and the reason is in the refusal line (reportRefusal:
        // qCWarning and stderr, no second channel is added).
        stock_session_.reset();
        clearStockField();
        stock_sim_progress_ = 0.0;
        stock_sim_state_ = QStringLiteral("refused");
        emit stockSimChanged();
        emit stockSimProgressChanged();
        emit stockSimStateChanged();
        reportRefusal(QStringLiteral(
            "съём материала не построен: инструмент T%1 (этап %2) картой высот не "
            "выражается - поднутрение; показана коробка заготовки")
                          .arg(payload.refused_tool)
                          .arg(payload.refused_stage + 1));
        return;
    }
    if (payload.failed) {
        stock_sim_state_ = QStringLiteral("idle");
        emit stockSimStateChanged();
        return;
    }
    if (!payload.has_field) {
        return;                            // nothing to publish
    }
    // The field is swapped only here and only whole: while a tick or a seek was
    // computed, the screen showed the previous field, not a mixture.
    stock_depth_ = payload.depth;
    stock_bounds_list_ = payload.bounds;
    stock_cell_mm_ = payload.cell_mm;
    stock_count_x_ = payload.count_x;
    stock_count_y_ = payload.count_y;
    stock_has_field_ = true;
    // The same field copy goes as a texture to the vertex shader: the size is the
    // node count, the row order is the same (a buffer row lies at V = 0), so a
    // node's texcoord is the centre of its texel. The update is the whole buffer:
    // QQuick3DTextureData has no partial upload, and this is the price of a tick
    // (measured by stockSimTickMs).
    if (stock_field_ != nullptr) {
        stock_field_->setSize(QSize(stock_count_x_, stock_count_y_));
        stock_field_->setFormat(QQuick3DTextureData::R32F);
        stock_field_->setTextureData(stock_depth_);
        stock_field_->update();
    }
    stock_sim_progress_ = payload.total_mm > 0.0
        ? std::max(0.0, std::min(1.0, payload.target_mm / payload.total_mm))
        : 0.0;
    if (payload.session) {
        // Full pass: the live walk starts from its field. The time is the field
        // reset plus the walk; the measurement runs in the core, on the same worker
        // thread.
        stock_session_ = payload.session;
        stock_live_target_ = payload.target_mm;
        stock_sim_time_ms_ = payload.time_ms;
        stock_sim_tick_ms_ = 0.0;
    } else {
        // Tick: the walk continued from the cursor, plus the field copy on the
        // worker thread.
        stock_sim_tick_ms_ = payload.time_ms;
    }
    stock_sim_state_ = QStringLiteral("ready");
    emit stockSimChanged();
    emit stockSimProgressChanged();
    emit stockSimStateChanged();
}

void GcodeToolpath::setMachineTools(const QVariantList &tools)
{
    // Machine table: [{number, length, name, enabled}], length - the measured gauge
    // length (mm). A number without a value (unrecognised) is skipped, as in the
    // reference.
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
    // An empty map is false in the reference: the limits are then unset, not zero.
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

void GcodeToolpath::setToolTip(const QVector3D &tip)
{
    tool_tip_ = tip;
}

void GcodeToolpath::refreshTangents(const QVector3D &view_point)
{
    // The eye arrives in the scene frame; it is converted to the raw model frame by
    // the inverse part transform (the same one the visible geometry uses).
    const QVector3D eye(view_point.x() - part_position_.x(),
                        view_point.y() - part_position_.y(),
                        view_point.z() - part_position_.z());
    const QVector3D raw = part_rotation_.inverted().rotatedVector(eye);
    eye_raw_ = raw;
    emit eyeChanged();

    // Tool and holder edges are also a silhouette: generators at the current eye.
    // They are computed before the model branches so the surface of revolution lives
    // even without a part.
    view_point_ = view_point;
    has_view_point_ = true;
    refreshProfileEdges();

    // The mode is set by CNC_SILHOUETTE: mesh (default) - silhouette from the mesh
    // topology, analytic - tangent generators of STEP cylinders and cones.
    //
    // A model without a mesh exits right here, as in the reference: nothing is
    // published and nothing fails. The slot is called on every camera move (dozens
    // of times per second), so the failure is counted once (warnDeferred), not line
    // by line.
    const double eye_mm[3] = {double(raw.x()), double(raw.y()), double(raw.z())};
    const QByteArray mode = qgetenv("CNC_SILHOUETTE").trimmed().toLower();
    // The frame buffers live in the object: a frame keeps the same numbers but does
    // not create intermediate vectors. The silhouette is filled directly into
    // sil_scratch_, the tangents - into tan_scratch_ (float64) with a conversion to
    // tan_segments_ (float32).
    const std::vector<float> *segments = nullptr;
    try {
        if (mode == QByteArrayLiteral("analytic")) {
            if (rev_surfaces_.empty())
                return;
            if (!has_tan_fams_) {
                tan_fams_ = gv::buildTangentFamilies(
                    rev_surfaces_, has_step_edges_ ? &step_edges_ : nullptr, nullptr,
                    quality_);
                has_tan_fams_ = true;
            }
            const std::vector<double> &flat =
                gv::tangentFromFamilies(tan_fams_, eye_mm, tan_scratch_);
            tan_segments_.clear();
            tan_segments_.reserve(flat.size());
            for (double v : flat)
                tan_segments_.push_back(float(v));
            segments = &tan_segments_;
        } else {
            if (!has_topo_)
                return;
            const std::vector<float> &mesh =
                gv::meshSilhouetteSegments(mesh_topo_, eye_mm, sil_scratch_);
            segments = &mesh;
        }
    } catch (const std::exception &exc) {
        warnDeferred("silhouette",
                     QStringLiteral("силуэт не посчитан: %1")
                         .arg(QString::fromUtf8(exc.what())));
        return;
    }
    // Publication outside try, as before: a failure of setSegments itself is not a
    // silhouette failure.
    setSilhouetteSegments(*segments);
}

// Silhouette publication: in the reference it is set_segments of float32 segments,
// an empty list means the layer is removed.
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
    const int count = load_string(text);           // a string has no model
    applyModelPath(findModel(filepath));
    return count;
}

bool GcodeToolpath::loadModel(const QString &filepath)
{
    const QFileInfo info(filepath);
    if (!info.exists())
        return false;
    // A stale asynchronous load is cancelled: its result has nothing to do with
    // this model (the reference has the same _load_token counter).
    ++(*load_token_);
    applyModelPath(QDir::toNativeSeparators(info.absoluteFilePath()));
    emit loadFinished();                           // the whole load is done right here
    return true;
}

// Model loading by path: reading (STEP or STL) and publishing the skin with the
// outline. The synchronous path - load_gcode and loadModel; an empty path means
// there is no model, and then an empty skin is published, as in the reference
// (_apply_mesh_verts(None)).
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
    // The token makes a stale result invisible: a new load cancels it. The counter
    // is taken by pointer and passed to the worker thread by copy: it outlives the
    // controller, so the thread reads the counter, not the object.
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
                // The failure is visible in the state too: the loading overlay does
                // not stay forever.
                QMetaObject::invokeMethod(this, [this] {
                    setLoading(false);
                    emit loadFinished();
                }, Qt::QueuedConnection);
                return;
            }
            if (token != token_ref->load())
                return;                   // a new load overtook this one - the result is not needed
            PreparedPath payload;
            payload.token = token;
            payload.text = *shared;
            payload.data = data;
            // The worker thread resolves the model sidecar, as in the reference
            // (_worker_load).
            payload.model_path = findModel(filepath);
            // The signal is emitted on the GUI thread: the call is cancelled if the
            // controller object is already gone by then.
            QMetaObject::invokeMethod(this, [this, payload] {
                emit toolpathReady(QVariant::fromValue(payload));
            }, Qt::QueuedConnection);

            // Second phase: the model next to the NC program (STEP matters more than
            // STL). It is read right here, on the worker thread; the GUI thread puts
            // it into the buffers (meshReady). The STL outline is a third pass
            // (startEdgesTask).
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
                return;                   // a new load overtook this one - the model is not needed
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
    setModelPath(QString());             // a string has no file nearby
    std::shared_ptr<const gv::PathData> data;
    try {
        data = preparePath(text);
    } catch (const std::exception &exc) {
        reportRefusal(QStringLiteral("разбор УП не прошёл: %1")
                          .arg(QString::fromUtf8(exc.what())));
        return 0;
    }
    applyToolpathData(data, text);
    mesh_->reset();                      // a string: the program has no model
    clearEdges();
    // The reference does not reset the skin bounds and the previous model's
    // silhouette here: the string comes without a file, and the model state lives
    // until the next model.
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
// Private part
// ---------------------------------------------------------------------------
void GcodeToolpath::onToolpathReady(const QVariant &payload)
{
    const PreparedPath result = payload.value<PreparedPath>();
    if (result.token != load_token_->load())
        return;                          // result of a cancelled load
    setModelPath(result.model_path);
    applyToolpathData(result.data, result.text);
    mesh_->reset();                      // the model phase will set the skin
    clearEdges();
    emit meshChanged();
    emit toolpathLoaded(result.data->seg_count);
}

void GcodeToolpath::onMeshReady(const QVariant &payload)
{
    const PreparedModel result = payload.value<PreparedModel>();
    if (result.token != load_token_->load())
        return;                          // model of a cancelled load
    setModelPath(result.path);
    applyModelData(result.model);
    // Skin and outline are set: the loading overlay goes away, as in the reference.
    setLoading(false);
    emit loadFinished();
}

void GcodeToolpath::applyToolpathData(const std::shared_ptr<const gv::PathData> &data,
                                      const QString &text)
{
    data_ = data;
    // Scene tags come first, as in the reference: the part transform is assembled
    // from them and from the skin bounds, and the path is published with the part
    // position already set.
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
    // A tool without a single UI_TOOL in the stages is taken from the tag in the NC
    // text - as in the reference (_apply_tool_info(parse_ui_tool(text))). It is also
    // the program tool before the first stage tag: removal must not depend on the
    // selected stage.
    program_tool_ = first.present ? first : gv::parseUiTool(text.toStdString());
    applyToolInfo(program_tool_);

    // The stock (UI_STOCK) is published before the transform: fitting the model to
    // its lower corner does not work without the stock. The holder (UI_HOLDER) and
    // its gauge length are parsed in applySceneTags; the collision scan and the
    // machine check run below.
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
    analyzeCollisions();                 // holder collisions with the stock
    startStockSim();                     // material removal over the whole program in the background
    runToolCheck();                      // NC check against the machine table
}

void GcodeToolpath::applyToolInfo(const gv::ToolInfo &spec)
{
    // Tool profile (core/tooling): a surface of revolution, an inverted-hull outline
    // and edge rings. An empty profile leaves the geometries empty, like the
    // reference set_profile(None).
    tool_spec_ = spec;
    tool_info_ = toolMap(spec);
    tool_profile_ = spec.present ? gv::toolProfile(spec) : std::vector<gv::ProfilePoint>{};
    const std::vector<gv::ProfilePoint> &profile = tool_profile_;
    tool_tan_fams_ = gv::profileTangentFamilies(profile);
    setToolProfile(tool_geo_, profile);
    tool_length_ = 0.0;
    tool_radius_ = 0.0;
    for (const gv::ProfilePoint &point : profile) {
        tool_length_ = std::max(tool_length_, point.height_mm);
        tool_radius_ = std::max(tool_radius_, point.radius_mm);
    }
    // Silhouette outline: the profile pushed out by `pad`; QML hides the near faces,
    // so only the outer contour is visible.
    const double pad = std::max(0.12, 0.05 * tool_radius_);
    setToolProfile(tool_outline_geo_,
                   profile.empty() ? profile : gv::toolOutlineProfile(profile, pad));
    setRingSegments(tool_rings_geo_,
                    profile.empty() ? std::vector<gv::RingSegment>{}
                                    : gv::ringsSegments(gv::profileRingPoints(profile)));
    refreshProfileEdges();
    emit toolInfoChanged();
}

// ---------------------------------------------------------------------------
// Tool, holder and collisions
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

void GcodeToolpath::setProfileEdges(HLRLineGeometry *geo,
                                    const std::vector<gv::ProfilePoint> &profile,
                                    const gv::TangentFamilies &families,
                                    const double eye_mm[3],
                                    gv::TangentScratch &scratch)
{
    // Edges of a surface of revolution as the same ribbons as the part
    // (stepledges): a set of segments in the profile frame (the tip at zero, axis
    // +Y). The horizontal rings are the same profileRingPoints sampling as
    // toolRings, so the edges land on real profile edges. The longitudinal
    // generators are the real silhouette: tangentFromFamilies over the profile at
    // the current eye, so a cylinder keeps two tangents instead of a seam at a fixed
    // azimuth. The offset toward the eye and the overlap with the body come from the
    // material (polyOffset) and depthBias -50000, so the radial FLOAT_SCALE offset
    // (setReference) is not needed here.
    const std::vector<gv::ProfilePoint> ring_pts = gv::profileRingPoints(profile);
    if (ring_pts.empty()) {
        geo->setSegments(nullptr, 0);
        return;
    }
    const std::vector<gv::RingSegment> rings = gv::ringsSegments(ring_pts);
    const std::vector<double> &flat =
        gv::tangentFromFamilies(families, eye_mm, scratch);
    std::vector<float> segs;
    segs.reserve(rings.size() * 6 + flat.size());
    for (const gv::RingSegment &ring : rings) {
        segs.push_back(float(ring.x0));
        segs.push_back(float(ring.y0));
        segs.push_back(float(ring.z0));
        segs.push_back(float(ring.x1));
        segs.push_back(float(ring.y1));
        segs.push_back(float(ring.z1));
    }
    for (double value : flat) {
        segs.push_back(float(value));
    }
    geo->setSegments(segs.data(), int(segs.size() / 6));
}

void GcodeToolpath::refreshProfileEdges()
{
    // The profile and the holder live in the scene frame without a rotation: axis
    // +Y, the tip at tool_tip_, the holder bottom shifted by holderBase (as in QML).
    if (!has_view_point_) {
        return;
    }
    const QVector3D tool_eye = view_point_ - tool_tip_;
    const double tool_eye_mm[3] = {double(tool_eye.x()), double(tool_eye.y()),
                                   double(tool_eye.z())};
    setProfileEdges(tool_profile_edges_, tool_profile_, tool_tan_fams_, tool_eye_mm,
                    tool_profile_scratch_);
    const double holder_base = holder_gauge_ > 0.0 ? holder_gauge_ : tool_length_;
    const double holder_eye_mm[3] = {tool_eye_mm[0], tool_eye_mm[1] - holder_base,
                                     tool_eye_mm[2]};
    setProfileEdges(holder_profile_edges_, holder_profile_, holder_tan_fams_,
                    holder_eye_mm, holder_profile_scratch_);
}

QVariantMap GcodeToolpath::toolCheckMap(const gv::ToolCheckRow &row)
{
    QVariantMap map;
    map.insert(QStringLiteral("number"), row.number);
    map.insert(QStringLiteral("name"), QString::fromUtf8(row.name.c_str()));
    map.insert(QStringLiteral("ncLength"), row.nc_length);
    map.insert(QStringLiteral("depth"), row.depth);
    // No measured machine length - None, as in the reference.
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
// Model, outline and part transform
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
    stock_bounds_ = stock;                               // bounds for the collision scan
    if (stock) {
        const std::array<double, 6> &s = *stock;         // xmin, xmax, ymin, ymax, zmin, zmax
        const std::array<double, 3> center{(s[0] + s[1]) / 2.0,
                                           (s[2] + s[3]) / 2.0,
                                           (s[4] + s[5]) / 2.0};
        stock_center_ = toScene(center);                 // machine -> scene
        stock_size_ = QVector3D(f32(s[1] - s[0]), f32(s[5] - s[4]), f32(s[3] - s[2]));
        stock_lower_m_ = {s[0], s[2], s[4]};             // lower corner, machine
        has_stock_ = true;
    } else {
        // There is no bounds in the tag: the stock is removed, and its previous
        // numbers stay (the reference has the same lines and the same order: QML
        // reads them only when hasStock).
        has_stock_ = false;
    }

    // Holder: gauge length (holderGauge) and name. Without the tag the gauge length
    // is zero, and the collision scan takes the gauge length from the tool profile,
    // as in the reference.
    holder_tag_ = gv::parseUiHolder(utf8);
    holder_gauge_ = holder_tag_ && holder_tag_->has_gauge ? holder_tag_->gauge : 0.0;
}

void GcodeToolpath::rebuildPartTransform()
{
    // The final transform: scene <- base <- WCS <- CAD (the reference
    // _rebuild_part_transform). The base turns machine Z-up into scene Y-up, WCS and
    // CAD come from the tags UI_ROTATION/UI_ORIGIN/UI_CAD_MATRIX.
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
        q_cad = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);     // no CAD transform
    }
    const QQuaternion q_m = q_wcs * q_cad;               // CAD -> machine
    const QQuaternion q = q_base * q_m;

    QVector3D t;
    if (has_stock_ && has_mesh_) {
        // Asymmetric fit: the lower corner of the rotated model bounds is aligned
        // with the lower corner of the stock.
        double rmin[3] = {1e30, 1e30, 1e30};
        // Bounds corners: for each axis - both limits, traversal order as in the
        // reference (lo, hi) over x, then y, then z.
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
        // Registration without transfer (the reference _part_auto_center flag is
        // always on): the skin centre of the rotated model is placed at the path
        // centre.
        const QVector3D rc = q.rotatedVector(mesh_center_);
        t = center() - rc;
    }
    t += toScene(part_offset_);                          // machine axes -> scene
    part_rotation_ = q;
    part_position_ = t;
    emit partTransformChanged();
}

void GcodeToolpath::applyModelData(const std::shared_ptr<const ModelData> &model)
{
    // Order as in the reference (_apply_mesh_verts): drop the previous skin, outline
    // and silhouette, then set the new skin and start the outline as a separate pass.
    mesh_->reset();
    clearEdges();                                        // fresh empty outline
    hlr_tangents_->setSegments(nullptr, 0);              // the previous model's silhouette removed
    mesh_center_ = QVector3D(0.0f, 0.0f, 0.0f);
    has_mesh_ = false;
    const int token = ++edge_token_;

    // The model read state is published even for a mesh without vertices: the
    // reference sets it in _load_model_verts, before installing the skin. A read
    // failure resets the B-Rep edges and normals, and leaves the surfaces and
    // families as before - also like the reference (only a successful STEP branch
    // rewrites them).
    has_step_edges_ = model ? model->has_step_edges : false;
    step_edges_ = model ? model->step_edges : gv::PackedEdges{};
    // The quality level travels with the model: it is used by the STL outline
    // (startEdgesTask) and the per-frame pass over the families. Without a model the
    // previous one stays.
    if (model)
        quality_ = model->quality;
    if (model && model->is_step) {
        rev_surfaces_ = model->surfaces;
        tan_fams_ = model->families;
        has_tan_fams_ = true;
    } else if (model) {
        rev_surfaces_.clear();                           // STL: no surfaces
        tan_fams_ = gv::TangentFamilies{};
        has_tan_fams_ = false;
    }
    mesh_topo_ = model ? model->topo : gv::MeshTopology{};

    const bool has_verts = model && !model->verts.empty();
    if (has_verts) {
        // Normals arrive as float32 (F,3,3); MeshGeometry takes double and casts them
        // to float32 itself - the value does not change.
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
        rebuildPartTransform();                          // the skin bounds changed
    } else {
        mesh_topo_ = gv::MeshTopology{};                 // no mesh - no silhouette
        has_topo_ = false;
    }
    emit meshChanged();
}

void GcodeToolpath::startEdgesTask(const std::shared_ptr<const ModelData> &model, int token)
{
    // Outline in a second pass (the reference uses the _compute_edges thread): STEP
    // gives it ready from the model read, STL computes edges by the preset's
    // dihedral angle and simplification tolerance (20 degrees and 0.15 mm at med).
    // The work runs off the GUI thread, the result is applied by token: the outline
    // of a stale model is not published.
    const std::shared_ptr<const ModelData> shared = model;
    const gv::QualityPreset quality = model ? model->quality : gv::QualityPreset{};
    QThreadPool::globalInstance()->start(QRunnable::create([this, token, shared, quality] {
        gv::PackedEdges packed;
        if (shared->has_step_edges) {
            packed = shared->step_edges;
        } else {
            try {
                packed = gv::featureEdgesNp(shared->verts, quality.outlineFeatureEdgeDeg,
                                            quality.outlineFeatureSimplifyMm);
            } catch (const std::exception &exc) {
                reportRefusal(QStringLiteral("обводка меша не посчитана: %1")
                                  .arg(QString::fromUtf8(exc.what())));
                return;
            }
        }
        QMetaObject::invokeMethod(this, [this, token, packed] {
            if (token != edge_token_)
                return;                                  // the outline is stale
            setEdgesPacked(packed);
            emit meshChanged();
        }, Qt::QueuedConnection);
    }));
}

void GcodeToolpath::setEdgesPacked(const gv::PackedEdges &packed)
{
    // A fresh object, not setting the buffer on the current one: QML re-uploads the
    // buffer into the Model only when the geometry object itself changes.
    ToolpathGeometry *fresh = new ToolpathGeometry();
    QQmlEngine::setObjectOwnership(fresh, QQmlEngine::CppOwnership);
    fresh->setPacked(toByteArray(packed.raw), packed.has_bounds ? &packed.bounds : nullptr,
                     packed.nv);
    ToolpathGeometry *old = edges_;
    edges_ = fresh;
    // The contour ribbon (stride 48) carries the same content: the B-Rep outline in
    // the main scene and its positional copy for the offset from the bounds centre.
    syncHlrReference();
    const std::vector<float> positions = gv::packedPositions(packed);
    hlr_edges_->setSegments(positions.empty() ? nullptr : positions.data(),
                            int(positions.size() / 6));
    if (old)
        old->deleteLater();
}

void GcodeToolpath::syncHlrReference()
{
    // Common centre of the skin bounds: both contour ribbons are pushed outward from
    // it (FLOAT_SCALE in HLRLineGeometry), so there is one for the outline and
    // silhouette layers.
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
    // A fresh object, not clearing the current one: QML re-uploads the buffer into
    // the Model even when the outline becomes empty after the first frame.
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
    // Upper bound (the first element strictly greater than d) - the same as
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
