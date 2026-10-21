// qt/gv_geometry.h - Qt Quick 3D geometries in C++ (stage 3 of the port).
//
// Mirrors the former Python wrapper (qt/gv_geometry.py): the same six classes,
// the same data fields, the same attributes and offsets, the same vertex order
// and the same setBounds. The buffer formats are frozen because they are the
// contract of QML and the shaders, not an implementation detail:
// ToolpathGeometry stride 20 = position 3f + texcoord0 2f (toolpath.vert/.frag);
// HLRLineGeometry stride 48 = position vec4, normal vec4, texcoord0 vec4 (stepledges.vert);
// GridGeometry stride 28 = position 3f + RGBA 4f;
// AxisArrowGeometry stride 12 = position 3f;
// MeshGeometry stride 24 = position 3f + normal 3f (normal offset 12);
// ToolGeometry stride 24 = position 3f + normal 3f (normal offset 12).
// The match with the reference is checked byte for byte: the probe
// cpp/geometry_probe.cpp writes the buffers and geometry state, and the check
// compares them with the live reference.
//
// What is not here: QML type registration and signals (sizeChanged, changed) -
// that is stage 5 (the controller). Here there is only buffer assembly and its
// installation.
#pragma once

#include <QByteArray>
#include <QQuick3DGeometry>

#include <array>
#include <vector>

#include "core/path_prep.h"      // gv::Aabb - scene bounds, like gv_types.Aabb

namespace gvgeom {

// ---------------------------------------------------------------------------
// Shared part
// ---------------------------------------------------------------------------

// The attribute type is the same for all geometries: float32. The names are as
// in the former Python header (_F32/_POSITION/_TEXCOORD0/_NORMAL/_COLOR/_LINES/
// _TRIANGLES).
constexpr QQuick3DGeometry::Attribute::ComponentType F32 =
        QQuick3DGeometry::Attribute::F32Type;

// The (lo, hi) bounds pair in scene coordinates plus a "bounds set" flag (in the
// reference a None instead of Aabb answers to it).
struct BoundsOrNone {
    bool present = false;
    gv::Aabb box;
};

// ---------------------------------------------------------------------------
// ToolpathGeometry: path lines, the path parameter in TexCoord0.x
// ---------------------------------------------------------------------------
class ToolpathGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 5 * 4;      // position (3f) + texcoord (2f)

    explicit ToolpathGeometry(QQuick3DObject *parent = nullptr);

    int vertexCount() const { return nv_; }

    // Set an already packed line buffer (assembled outside the GUI thread).
    // bounds == nullptr - no bounds, like set_packed(raw, None, nv) in the
    // reference.
    void setPacked(const QByteArray &raw, const gv::Aabb *bounds, int nv);

    // Unpadded bounds (without pad=1.0 from setBounds): the former Python wrapper
    // put it into the reference's same _bounds field and the inherited _rebuild
    // did the setup.
    const BoundsOrNone &boundsOrNone() const { return bounds_; }

private:
    void rebuild();

    QByteArray raw_;              // interleaved position(3f) + texcoord(2f)
    int nv_ = 0;                  // vertex count
    BoundsOrNone bounds_;         // scene bounds (x0,y0,z0,x1,y1,z1)
};

// ---------------------------------------------------------------------------
// HLRLineGeometry: model contour as ribbons (two triangles per segment)
// ---------------------------------------------------------------------------
class HLRLineGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 12 * 4;
    static constexpr float FLOAT_SCALE = 1.0012f;   // radial offset from the reference point

    explicit HLRLineGeometry(QQuick3DObject *parent = nullptr);

    int vertexCount() const { return nv_; }

    // Reference point of the model frame, from which the ends are pushed
    // (nullptr - none).
    void setReference(const double *center);

    // segs - count segments of 6 float32 each (two points of three), the model
    // frame as is.
    void setSegments(const float *segs, int count);

    // Unpadded bounds: see ToolpathGeometry::boundsOrNone.
    const BoundsOrNone &boundsOrNone() const { return bounds_; }

private:
    void rebuild();

    QByteArray raw_;
    int nv_ = 0;
    std::vector<float> ref_;      // reference point in float32, like _ref in the reference
    // Frame ribbons: the capacity survives the frame, so a frame only assigns and
    // fills (on Test_model 64.5 KB for the silhouette, 683 KB for the outline).
    std::vector<float> vert_;
    BoundsOrNone bounds_;         // packed (x0,y0,z0,x1,y1,z1)
};

// ---------------------------------------------------------------------------
// GridGeometry: a grid of lines in the XZ plane fading softly toward the edge
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
    double feather_ = 0.45;      // fraction of half the size that fades at the edge
};

// ---------------------------------------------------------------------------
// AxisArrowGeometry: a flat double-sided axis marker in the XY plane
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
// MeshGeometry: a triangle mesh (position + flat normals) in the scene
// ---------------------------------------------------------------------------
class MeshGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 6 * 4;      // position (3f) + normal (3f)

    explicit MeshGeometry(QQuick3DObject *parent = nullptr);

    int triangleCount() const { return nv_ / 3; }

    // verts - nFloats float64 values, a multiple of 9 (by triangles). Normals, if
    // not given, are computed by cross product in float32 - as in the reference.
    void setTriangles(const double *verts, int nFloats);
    void setTriangles(const double *verts, int nFloats, const double *normals);
    void reset();

    // Bounds without padding: for a mesh setBounds is set exactly from these
    // numbers.
    const std::array<double, 3> &lo() const { return lo_; }
    const std::array<double, 3> &hi() const { return hi_; }

private:
    void rebuild();

    QByteArray data_;             // packed float32 position + normal
    int nv_ = 0;
    std::array<double, 3> lo_{0.0, 0.0, 0.0};
    std::array<double, 3> hi_{0.0, 0.0, 0.0};
};

// ---------------------------------------------------------------------------
// ToolGeometry: a surface of revolution from a 2D tool profile
// ---------------------------------------------------------------------------
class ToolGeometry : public QQuick3DGeometry
{
public:
    static constexpr int STRIDE = 6 * 4;
    static constexpr int SEGMENTS = 48;

    explicit ToolGeometry(QQuick3DObject *parent = nullptr);

    int triangleCount() const { return nv_ / 3; }

    // profile - count pairs (radius_mm, height_mm). segments <= 0 - default 48.
    void setProfile(const double *profile, int count, int segments = 0);

    // Bounds without padding: see MeshGeometry::lo.
    const std::array<double, 3> &lo() const { return lo_; }
    const std::array<double, 3> &hi() const { return hi_; }

private:
    void rebuild();

    QByteArray data_;
    int nv_ = 0;
    std::array<double, 3> lo_{0.0, 0.0, 0.0};
    std::array<double, 3> hi_{0.0, 0.0, 0.0};
};

} // namespace gvgeom
