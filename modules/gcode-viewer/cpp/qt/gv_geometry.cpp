// qt/gv_geometry.cpp - геометрии Qt Quick 3D на C++ (этап 3 плана переноса).
//
// Построчный перенос modules/gcode-viewer/geometry.py. Расхождения недопустимы:
// буферы этих геометрий читают шейдеры toolpath.* и stepledges.*, а совпадение с
// питоновским эталоном проверяется байт в байт (cpp/geometry_probe.cpp и
// modules/gcode-viewer/test_geometry_core.py). Отсюда два правила в коде:
//   * порядок операций и порядок вершин сохраняются буквально, включая места,
//     где выражение выглядит избыточным;
//   * там, где Python считает в float32 (проверки numpy над float32-массивами),
//     C++ считает в float, а не в double.
#include "qt/gv_geometry.h"

#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace gvgeom {
namespace {

constexpr double PI = 3.141592653589793;       // math.pi
constexpr double TWO_PI = 2.0 * PI;

// Целая часть от деления, как питоновский `a // b` над float: CPython считает
// остаток fmod и делит разность, а на отрицательных значениях floor(a / b) даёт
// другой результат. Габарит сетки считается этим числом, поэтому повторяем.
double pyFloorDiv(double vx, double wx)
{
    double mod = std::fmod(vx, wx);
    double div = (vx - mod) / wx;
    if (mod != 0.0) {
        if ((wx < 0.0) != (mod < 0.0)) {
            mod += wx;
            div -= 1.0;
        }
    }
    if (div == 0.0)
        return std::copysign(0.0, vx / wx);
    double floorDiv = std::floor(div);
    if (div - floorDiv > 0.5)
        floorDiv += 1.0;
    return floorDiv;
}

// Габарит-накопитель: минимум и максимум по осям, как в питоновских циклах.
struct LoHi {
    double lo[3] = {std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::infinity()};
    double hi[3] = {-std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity(),
                    -std::numeric_limits<double>::infinity()};

    void add(double x, double y, double z)
    {
        if (x < lo[0]) lo[0] = x;
        if (y < lo[1]) lo[1] = y;
        if (z < lo[2]) lo[2] = z;
        if (x > hi[0]) hi[0] = x;
        if (y > hi[1]) hi[1] = y;
        if (z > hi[2]) hi[2] = z;
    }
};

QByteArray asBytes(const std::vector<float> &floats)
{
    return QByteArray(reinterpret_cast<const char *>(floats.data()),
                      int(floats.size() * sizeof(float)));
}

} // namespace

// ===========================================================================
// ToolpathGeometry
// ===========================================================================
ToolpathGeometry::ToolpathGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void ToolpathGeometry::setPacked(const QByteArray &raw, const gv::Aabb *bounds, int nv)
{
    raw_ = raw;
    nv_ = nv;
    bounds_.present = bounds != nullptr;
    if (bounds != nullptr)
        bounds_.box = *bounds;
    rebuild();
}

void ToolpathGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
    if (raw_.isEmpty()) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }
    setVertexData(raw_);
    setStride(STRIDE);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::TexCoordSemantic, 3 * 4, F32);
    double x0 = 0.0, y0 = 0.0, z0 = 0.0, x1 = 0.0, y1 = 0.0, z1 = 0.0;
    if (bounds_.present) {
        x0 = bounds_.box.lo[0];
        y0 = bounds_.box.lo[1];
        z0 = bounds_.box.lo[2];
        x1 = bounds_.box.hi[0];
        y1 = bounds_.box.hi[1];
        z1 = bounds_.box.hi[2];
    }
    const double pad = 1.0;
    setBounds(QVector3D(float(x0 - pad), float(y0 - pad), float(z0 - pad)),
              QVector3D(float(x1 + pad), float(y1 + pad), float(z1 + pad)));
    update();
}

// ===========================================================================
// HLRLineGeometry
// ===========================================================================
HLRLineGeometry::HLRLineGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void HLRLineGeometry::setReference(const double *center)
{
    if (center == nullptr) {
        ref_.clear();
        return;
    }
    ref_.assign({float(center[0]), float(center[1]), float(center[2])});
}

void HLRLineGeometry::setSegments(const float *segs, int count)
{
    if (segs == nullptr || count == 0) {
        raw_.clear();
        nv_ = 0;
        bounds_ = BoundsOrNone{};
        rebuild();
        return;
    }
    const int m = count;
    // Радиальный отжим FLOAT_SCALE запекается здесь (буфер статичен на загрузку);
    // сдвиг вдоль луча зрения живёт в stepledges.vert (eyeRaw + camFloatMm).
    std::vector<float> a(segs, segs + std::size_t(m) * 6);
    if (!ref_.empty()) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            const float r = ref_[i % 3];
            a[i] = r + (a[i] - r) * FLOAT_SCALE;      // порядок операций как в numpy
        }
    }

    static const int pick[6] = {0, 1, 1, 0, 1, 0};
    static const float sgn[6] = {1.0f, 1.0f, -1.0f, 1.0f, -1.0f, -1.0f};
    std::vector<float> vert(std::size_t(m) * 6 * 12, 0.0f);
    LoHi range;
    for (int s = 0; s < m; ++s) {
        const float *pa = &a[std::size_t(s) * 6];
        const float *pb = pa + 3;
        float d[3];
        for (int j = 0; j < 3; ++j)
            d[j] = pb[j] - pa[j];
        // numpy.linalg.norm над float32 считает ((x*x + y*y) + z*z) в float32.
        const float ln = std::sqrt((d[0] * d[0] + d[1] * d[1]) + d[2] * d[2]);
        const float divisor = (ln == 0.0f) ? 1.0f : ln;   // np.where(ln == 0, 1, ln)
        for (int j = 0; j < 3; ++j)
            d[j] = d[j] / divisor;
        // Треугольники (pa+, pb+, pb-) и (pa+, pb-, pa-): 12 float на вершину,
        // position @0, normal (направление отрезка) @16, texcoord0 (сторона) @32.
        for (int v = 0; v < 6; ++v) {
            const float *p = (pick[v] == 0) ? pa : pb;
            float *o = &vert[(std::size_t(s) * 6 + v) * 12];
            o[0] = p[0];
            o[1] = p[1];
            o[2] = p[2];
            o[4] = d[0];
            o[5] = d[1];
            o[6] = d[2];
            o[8] = sgn[v];
        }
        for (int j = 0; j < 3; ++j) {
            range.lo[j] = std::min(range.lo[j], double(std::min(pa[j], pb[j])));
            range.hi[j] = std::max(range.hi[j], double(std::max(pa[j], pb[j])));
        }
    }
    raw_ = asBytes(vert);
    nv_ = m * 6;
    bounds_.present = true;
    bounds_.box.lo = {range.lo[0], range.lo[1], range.lo[2]};
    bounds_.box.hi = {range.hi[0], range.hi[1], range.hi[2]};
    rebuild();
}

void HLRLineGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    if (raw_.isEmpty()) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }
    setVertexData(raw_);
    setStride(STRIDE);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 4 * 4, F32);
    addAttribute(QQuick3DGeometry::Attribute::TexCoordSemantic, 8 * 4, F32);
    if (bounds_.present) {
        const double pad = 1.0;
        setBounds(QVector3D(float(bounds_.box.lo[0] - pad), float(bounds_.box.lo[1] - pad),
                            float(bounds_.box.lo[2] - pad)),
                  QVector3D(float(bounds_.box.hi[0] + pad), float(bounds_.box.hi[1] + pad),
                            float(bounds_.box.hi[2] + pad)));
    }
    update();
}

// ===========================================================================
// GridGeometry
// ===========================================================================
GridGeometry::GridGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void GridGeometry::setSize(double v)
{
    size_ = v;
    rebuild();
}

void GridGeometry::setStep(double v)
{
    step_ = std::max(1.0, v);
    rebuild();
}

void GridGeometry::setLevel(double v)
{
    level_ = v;
    rebuild();
}

void GridGeometry::setCenterX(double v)
{
    cx_ = v;
    rebuild();
}

void GridGeometry::setCenterZ(double v)
{
    cz_ = v;
    rebuild();
}

void GridGeometry::setFeather(double v)
{
    feather_ = std::max(0.0, std::min(0.95, v));
    rebuild();
}

double GridGeometry::alphaAt(double dist) const
{
    const double f = feather_;
    if (f <= 1e-6)
        return 1.0;
    const double t = (dist - (1.0 - f)) / f;
    if (t <= 0.0)
        return 1.0;
    if (t >= 1.0)
        return 0.0;
    return 1.0 - (t * t * (3.0 - 2.0 * t));
}

void GridGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
    const double half = size_ / 2.0;
    const double step = step_;
    const int n = int(pyFloorDiv(half, step));
    const double y = level_;
    const double cx = cx_;
    const double cz = cz_;
    const int segs = std::max(8, 2 * n);          // дробление вдоль каждой линии
    std::vector<float> buf;
    LoHi range;

    const auto vertex = [&](double x, double z) {
        const double d = (half > 0.0) ? (std::max(std::fabs(x - cx), std::fabs(z - cz)) / half)
                                      : 0.0;
        const double a = alphaAt(d);
        buf.push_back(float(x));
        buf.push_back(float(y));
        buf.push_back(float(z));
        buf.push_back(1.0f);
        buf.push_back(1.0f);
        buf.push_back(1.0f);
        buf.push_back(float(a));
        range.add(x, y, z);
    };

    for (int i = -n; i <= n; ++i) {
        const double c = i * step;
        for (int k = 0; k < segs; ++k) {                  // линия вдоль Z
            const double z0 = cz - half + (2.0 * half) * double(k) / double(segs);
            const double z1 = cz - half + (2.0 * half) * double(k + 1) / double(segs);
            vertex(cx + c, z0);
            vertex(cx + c, z1);
        }
        for (int k = 0; k < segs; ++k) {                  // линия вдоль X
            const double x0 = cx - half + (2.0 * half) * double(k) / double(segs);
            const double x1 = cx - half + (2.0 * half) * double(k + 1) / double(segs);
            vertex(x0, cz + c);
            vertex(x1, cz + c);
        }
    }

    setVertexData(asBytes(buf));
    setStride(STRIDE);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::ColorSemantic, 3 * 4, F32);
    setBounds(QVector3D(float(range.lo[0] - 1), float(range.lo[1] - 1), float(range.lo[2] - 1)),
              QVector3D(float(range.hi[0] + 1), float(range.hi[1] + 1), float(range.hi[2] + 1)));
    update();
}

// ===========================================================================
// AxisArrowGeometry
// ===========================================================================
AxisArrowGeometry::AxisArrowGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    rebuild();
}

void AxisArrowGeometry::setLength(double v)
{
    length_ = v;
    rebuild();
}

void AxisArrowGeometry::setHeadLength(double v)
{
    headLen_ = v;
    rebuild();
}

void AxisArrowGeometry::setHeadWidth(double v)
{
    headW_ = v;
    rebuild();
}

void AxisArrowGeometry::setShaftWidth(double v)
{
    shaftW_ = v;
    rebuild();
}

void AxisArrowGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    const double L = length_;
    double s = L - headLen_;                  // стержень кончается там, где начинается голова
    if (s <= 0.0)
        s = L * 0.5;
    const double sw = shaftW_;
    const double hw = headW_;
    const double front[9][3] = {
        {0.0, -sw, 0.0}, {0.0, sw, 0.0}, {s, sw, 0.0},
        {0.0, -sw, 0.0}, {s, sw, 0.0},  {s, -sw, 0.0},
        {s, -hw, 0.0},   {s, hw, 0.0},  {L, 0.0, 0.0},
    };
    std::vector<float> buf;
    LoHi range;
    const auto push = [&](const double *p) {          // _pack: float32 в буфер, габарит по double
        buf.push_back(float(p[0]));
        buf.push_back(float(p[1]));
        buf.push_back(float(p[2]));
        range.add(p[0], p[1], p[2]);
    };
    for (int i = 0; i < 9; ++i)
        push(front[i]);
    for (int i = 0; i < 9; i += 3) {                  // обратная сторона: те же треугольники
        push(front[i + 2]);
        push(front[i + 1]);
        push(front[i]);
    }

    setVertexData(asBytes(buf));
    setStride(STRIDE);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    setBounds(QVector3D(float(range.lo[0] - 1), float(range.lo[1] - 1), float(range.lo[2] - 1)),
              QVector3D(float(range.hi[0] + 1), float(range.hi[1] + 1), float(range.hi[2] + 1)));
}

// ===========================================================================
// MeshGeometry
// ===========================================================================
MeshGeometry::MeshGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    rebuild();
}

void MeshGeometry::setTriangles(const double *verts, int nFloats)
{
    setTriangles(verts, nFloats, nullptr);
}

void MeshGeometry::setTriangles(const double *verts, int nFloats, const double *normals)
{
    if (verts != nullptr && nFloats >= 9) {
        const int nt = nFloats / 9;
        // Треугольники сначала приводятся к float32 (ascontiguousarray), и все
        // дальнейшие вычисления идут в float - как в numpy над float32-массивом.
        std::vector<float> tri(std::size_t(nt) * 9);
        for (int i = 0; i < nt * 9; ++i)
            tri[std::size_t(i)] = float(verts[i]);
        std::vector<float> inter(std::size_t(nt) * 18);
        for (int t = 0; t < nt; ++t) {
            const float *a = &tri[std::size_t(t) * 9];
            const float *b = a + 3;
            const float *c = a + 6;
            float face_n[3] = {0.0f, 0.0f, 1.0f};
            if (normals == nullptr) {
                const float u0 = b[0] - a[0], u1 = b[1] - a[1], u2 = b[2] - a[2];
                const float w0 = c[0] - a[0], w1 = c[1] - a[1], w2 = c[2] - a[2];
                const float fn[3] = {u1 * w2 - u2 * w1, u2 * w0 - u0 * w2, u0 * w1 - u1 * w0};
                const float ln = std::sqrt((fn[0] * fn[0] + fn[1] * fn[1]) + fn[2] * fn[2]);
                const float divisor = (ln == 0.0f) ? 1.0f : ln;    // вырожденный треугольник
                for (int j = 0; j < 3; ++j)
                    face_n[j] = fn[j] / divisor;
            }
            for (int v = 0; v < 3; ++v) {
                float *o = &inter[(std::size_t(t) * 3 + v) * 6];
                for (int j = 0; j < 3; ++j)
                    o[j] = a[v * 3 + j];
                if (normals == nullptr) {
                    for (int j = 0; j < 3; ++j)
                        o[3 + j] = face_n[j];
                } else {
                    // Нормаль задана на угол треугольника и у углов бывает разной:
                    // _smooth_face_normals кластеризует нормали вокруг каждого
                    // сваренного угла, поэтому углы одной грани получают разные
                    // средние. Столбец первого угла на весь треугольник - другая
                    // (сглаженная не в ту сторону) картинка.
                    const double *n = &normals[(std::size_t(t) * 3 + std::size_t(v)) * 3];
                    for (int j = 0; j < 3; ++j)
                        o[3 + j] = float(n[j]);
                }
            }
        }
        data_ = asBytes(inter);
        nv_ = nt * 3;
        // Габарит - по исходным double: питоновский _verts_bounds перебирает
        // массив до приведения к float32.
        LoHi range;
        for (int i = 0; i < nFloats; ++i) {
            const int j = i % 3;
            if (verts[i] < range.lo[j])
                range.lo[j] = verts[i];
            if (verts[i] > range.hi[j])
                range.hi[j] = verts[i];
        }
        lo_ = {range.lo[0], range.lo[1], range.lo[2]};
        hi_ = {range.hi[0], range.hi[1], range.hi[2]};
        stride_ = STRIDE;
        rebuild();
        return;
    }
    // Пустой или неполный вход: геометрия пуста, габарит нулевой. Питоновская
    // ветка без numpy (список точек) здесь не переносится: этап 0 оставил её без
    // потребителей, а модели приходят массивом.
    data_.clear();
    nv_ = 0;
    stride_ = STRIDE;
    lo_ = {0.0, 0.0, 0.0};
    hi_ = {0.0, 0.0, 0.0};
    rebuild();
}

void MeshGeometry::reset()
{
    data_.clear();
    nv_ = 0;
    stride_ = STRIDE;
    rebuild();
}

void MeshGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    if (data_.isEmpty()) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }
    setVertexData(data_);
    setStride(STRIDE);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, F32);
    setBounds(QVector3D(float(lo_[0]), float(lo_[1]), float(lo_[2])),
              QVector3D(float(hi_[0]), float(hi_[1]), float(hi_[2])));
    update();
}

// ===========================================================================
// ToolGeometry
// ===========================================================================
ToolGeometry::ToolGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    rebuild();
}

void ToolGeometry::setProfile(const double *profile, int count, int segments)
{
    const int seg = (segments > 0) ? segments : SEGMENTS;
    std::vector<std::array<double, 2>> prof;
    prof.reserve(std::size_t(std::max(0, count)));
    for (int i = 0; i < count; ++i)
        prof.push_back({std::max(0.0, profile[2 * i]), profile[2 * i + 1]});
    std::vector<std::array<double, 2>> cleaned;      // соседние дубликаты точек отброшены
    for (const auto &p : prof) {
        if (cleaned.empty() || std::fabs(p[0] - cleaned.back()[0]) > 1e-9
                || std::fabs(p[1] - cleaned.back()[1]) > 1e-9) {
            cleaned.push_back(p);
        }
    }
    if (cleaned.size() < 2 || seg < 3) {
        data_.clear();
        nv_ = 0;
        rebuild();
        return;
    }

    std::vector<double> cosv(std::size_t(seg) + 1);
    std::vector<double> sinv(std::size_t(seg) + 1);
    for (int s = 0; s <= seg; ++s) {
        cosv[std::size_t(s)] = std::cos(TWO_PI * double(s) / double(seg));
        sinv[std::size_t(s)] = std::sin(TWO_PI * double(s) / double(seg));
    }
    std::vector<float> buf;
    LoHi range;
    const auto vtx = [&](double x, double y, double z, const double *n) {
        buf.push_back(float(x));
        buf.push_back(float(y));
        buf.push_back(float(z));
        buf.push_back(float(n[0]));
        buf.push_back(float(n[1]));
        buf.push_back(float(n[2]));
        range.add(x, y, z);
    };

    for (std::size_t i = 0; i + 1 < cleaned.size(); ++i) {
        const double r0 = cleaned[i][0];
        const double y0 = cleaned[i][1];
        const double r1 = cleaned[i + 1][0];
        const double y1 = cleaned[i + 1][1];
        if (r0 <= 1e-9 && r1 <= 1e-9)
            continue;
        const double ur = r1 - r0;
        const double uy = y1 - y0;
        double nl = std::sqrt(ur * ur + uy * uy);
        if (nl == 0.0)
            nl = 1.0;                                // Python: `or 1.0`
        const double npr = uy / nl;
        const double npy = -ur / nl;                 // внешняя нормаль отрезка (r, y)
        for (int s = 0; s < seg; ++s) {
            const double c0 = cosv[std::size_t(s)], s0 = sinv[std::size_t(s)];
            const double c1 = cosv[std::size_t(s) + 1], s1 = sinv[std::size_t(s) + 1];
            const double n0[3] = {npr * c0, npy, npr * s0};
            const double n1[3] = {npr * c1, npy, npr * s1};
            if (r0 <= 1e-9) {                        // веер вверх от оси
                const double apex[3] = {npr * (c0 + c1) * 0.5, npy, npr * (s0 + s1) * 0.5};
                vtx(0.0, y0, 0.0, apex);
                vtx(r1 * c0, y1, r1 * s0, n1);
                vtx(r1 * c1, y1, r1 * s1, n1);
            } else if (r1 <= 1e-9) {                 // веер вниз к оси
                const double apex[3] = {npr * (c0 + c1) * 0.5, npy, npr * (s0 + s1) * 0.5};
                vtx(r0 * c0, y0, r0 * s0, n0);
                vtx(r0 * c1, y0, r0 * s1, n1);
                vtx(0.0, y1, 0.0, apex);
            } else {                                 // четырёхугольник - два треугольника
                vtx(r0 * c0, y0, r0 * s0, n0);
                vtx(r0 * c1, y0, r0 * s1, n1);
                vtx(r1 * c1, y1, r1 * s1, n1);
                vtx(r0 * c0, y0, r0 * s0, n0);
                vtx(r1 * c1, y1, r1 * s1, n1);
                vtx(r1 * c0, y1, r1 * s0, n0);
            }
        }
    }

    data_ = asBytes(buf);
    nv_ = int(buf.size()) / 6;
    // Пустой профиль оставляет габарит нулевым - как в Python (`lo if nv else [0, 0, 0]`).
    lo_ = (nv_ > 0) ? std::array<double, 3>{range.lo[0], range.lo[1], range.lo[2]}
                    : std::array<double, 3>{0.0, 0.0, 0.0};
    hi_ = (nv_ > 0) ? std::array<double, 3>{range.hi[0], range.hi[1], range.hi[2]}
                    : std::array<double, 3>{0.0, 0.0, 0.0};
    rebuild();
}

void ToolGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    if (data_.isEmpty()) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }
    setVertexData(data_);
    setStride(STRIDE);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 12, F32);
    setBounds(QVector3D(float(lo_[0]), float(lo_[1]), float(lo_[2])),
              QVector3D(float(hi_[0]), float(hi_[1]), float(hi_[2])));
    update();
}

} // namespace gvgeom
