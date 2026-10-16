// qt/gv_geometry.h - геометрии Qt Quick 3D на C++ (этап 3 плана переноса).
//
// Зеркало modules/gcode-viewer/geometry.py: те же шесть классов, те же поля данных,
// те же атрибуты и смещения, тот же порядок вершин и те же setBounds. Форматы буферов
// заморожены, потому что это контракт QML и шейдеров, а не деталь реализации:
//   ToolpathGeometry  stride 20 = position 3f + texcoord0 2f   (toolpath.vert/.frag);
//   HLRLineGeometry   stride 48 = position vec4, normal vec4, texcoord0 vec4 (stepledges.vert);
//   GridGeometry      stride 28 = position 3f + RGBA 4f;
//   AxisArrowGeometry stride 12 = position 3f;
//   MeshGeometry      stride 24 = position 3f + normal 3f (смещение нормали 12);
//   ToolGeometry      stride 24 = position 3f + normal 3f (смещение нормали 12).
// Совпадение с питоновским эталоном проверяется байт в байт: пробник
// cpp/geometry_probe.cpp пишет буферы и состояние геометрий, проверка
// modules/gcode-viewer/test_geometry_core.py сравнивает их с живыми питоновскими.
//
// Чего здесь нет: регистрации типов в QML и сигналов (sizeChanged, changed) - это
// этап 5 (контроллер). Здесь только сборка буфера и его установка.
#pragma once

#include <QByteArray>
#include <QQuick3DGeometry>

#include <array>
#include <vector>

#include "core/path_prep.h"      // gv::Aabb - габарит сцены, как gv_types.Aabb

namespace gvgeom {

// ---------------------------------------------------------------------------
// Общая часть
// ---------------------------------------------------------------------------

// Тип атрибута у всех геометрий один: float32. Имена - как в питоновской шапке
// (_F32/_POSITION/_TEXCOORD0/_NORMAL/_COLOR/_LINES/_TRIANGLES).
constexpr QQuick3DGeometry::Attribute::ComponentType F32 =
        QQuick3DGeometry::Attribute::F32Type;

// Пара (lo, hi) габарита в координатах сцены плюс признак «габарит задан»
// (в Python ему отвечает None вместо Aabb).
struct BoundsOrNone {
    bool present = false;
    gv::Aabb box;
};

// ---------------------------------------------------------------------------
// ToolpathGeometry: линии траектории, параметр пути в TexCoord0.x
// ---------------------------------------------------------------------------
class ToolpathGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 5 * 4;      // position (3f) + texcoord (2f)

    explicit ToolpathGeometry(QQuick3DObject *parent = nullptr);

    int vertexCount() const { return nv_; }

    // Установка уже упакованного буфера линий (собирается вне GUI-потока).
    // bounds == nullptr - габарита нет, как set_packed(raw, None, nv) в Python.
    void setPacked(const QByteArray &raw, const gv::Aabb *bounds, int nv);

private:
    void rebuild();

    QByteArray raw_;              // interleaved position(3f) + texcoord(2f)
    int nv_ = 0;                  // число вершин
    BoundsOrNone bounds_;         // габарит сцены (x0,y0,z0,x1,y1,z1)
};

// ---------------------------------------------------------------------------
// HLRLineGeometry: контур модели лентами (два треугольника на отрезок)
// ---------------------------------------------------------------------------
class HLRLineGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 12 * 4;
    static constexpr float FLOAT_SCALE = 1.0012f;   // радиальный отжим от опорной точки

    explicit HLRLineGeometry(QQuick3DObject *parent = nullptr);

    int vertexCount() const { return nv_; }

    // Опорная точка кадра модели, от которой отжимаются концы (nullptr - нет).
    void setReference(const double *center);

    // segs - count отрезков по 6 float32 (две точки по три), кадр модели как есть.
    void setSegments(const float *segs, int count);

private:
    void rebuild();

    QByteArray raw_;
    int nv_ = 0;
    std::vector<float> ref_;      // опорная точка в float32, как _ref в Python
    BoundsOrNone bounds_;         // упакованный (x0,y0,z0,x1,y1,z1)
};

// ---------------------------------------------------------------------------
// GridGeometry: сетка линий в плоскости XZ с мягким растворением к краю
// ---------------------------------------------------------------------------
class GridGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 7 * 4;      // position (3f) + RGBA (4f)

    explicit GridGeometry(QQuick3DObject *parent = nullptr);

    double size() const { return size_; }
    void setSize(double v);
    double step() const { return step_; }
    void setStep(double v);
    double level() const { return level_; }
    void setLevel(double v);
    double centerX() const { return cx_; }
    void setCenterX(double v);
    double centerZ() const { return cz_; }
    void setCenterZ(double v);
    double feather() const { return feather_; }
    void setFeather(double v);

private:
    double alphaAt(double dist) const;
    void rebuild();

    double size_ = 200.0;
    double step_ = 10.0;
    double level_ = 0.0;
    double cx_ = 0.0;
    double cz_ = 0.0;
    double feather_ = 0.45;      // доля половины размера, гаснущая у края
};

// ---------------------------------------------------------------------------
// AxisArrowGeometry: плоский двусторонний указатель оси в плоскости XY
// ---------------------------------------------------------------------------
class AxisArrowGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 3 * 4;

    explicit AxisArrowGeometry(QQuick3DObject *parent = nullptr);

    double length() const { return length_; }
    void setLength(double v);
    double headLength() const { return headLen_; }
    void setHeadLength(double v);
    double headWidth() const { return headW_; }
    void setHeadWidth(double v);
    double shaftWidth() const { return shaftW_; }
    void setShaftWidth(double v);

private:
    void rebuild();

    double length_ = 12.0;
    double headLen_ = 3.5;
    double headW_ = 2.0;
    double shaftW_ = 0.7;
};

// ---------------------------------------------------------------------------
// MeshGeometry: треугольная сетка (позиция + плоские нормали) в сцене
// ---------------------------------------------------------------------------
class MeshGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 6 * 4;      // position (3f) + normal (3f)

    explicit MeshGeometry(QQuick3DObject *parent = nullptr);

    int triangleCount() const { return nv_ / 3; }

    // verts - nFloats значений float64, кратно 9 (треугольниками). Нормали, если
    // не заданы, считаются векторным произведением в float32 - как в Python.
    void setTriangles(const double *verts, int nFloats);
    void setTriangles(const double *verts, int nFloats, const double *normals);
    void reset();

private:
    void rebuild();

    QByteArray data_;             // упакованные float32 position + normal
    int nv_ = 0;
    int stride_ = STRIDE;         // зеркало поля _stride в Python (в сборке не участвует)
    std::array<double, 3> lo_{0.0, 0.0, 0.0};
    std::array<double, 3> hi_{0.0, 0.0, 0.0};
};

// ---------------------------------------------------------------------------
// ToolGeometry: тело вращения по двумерному профилю инструмента
// ---------------------------------------------------------------------------
class ToolGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 6 * 4;
    static constexpr int SEGMENTS = 48;

    explicit ToolGeometry(QQuick3DObject *parent = nullptr);

    int triangleCount() const { return nv_ / 3; }

    // profile - count пар (radius_mm, height_mm). segments <= 0 - умолчание 48.
    void setProfile(const double *profile, int count, int segments = 0);

private:
    void rebuild();

    QByteArray data_;
    int nv_ = 0;
    std::array<double, 3> lo_{0.0, 0.0, 0.0};
    std::array<double, 3> hi_{0.0, 0.0, 0.0};
};

} // namespace gvgeom
