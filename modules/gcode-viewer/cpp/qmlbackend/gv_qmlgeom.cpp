// cpp/qmlbackend/gv_qmlgeom.cpp - QML wrappers over the stage 3 geometries.
//
// There is no buffer assembly logic here: each method overrides the base-class
// method of the same name (qt/gv_geometry), so the order clear -> setVertexData
// -> setStride -> addAttribute -> setBounds -> update stays literally stage 3
// code, checked against the reference qt/gv_geometry.cpp byte for byte. Only a
// signal is added, which QML waits for to rebind the property.
#include "qmlbackend/gv_qmlgeom.h"

namespace gvqml {

// ---------------------------------------------------------------------------
// ToolpathGeometry
// ---------------------------------------------------------------------------
ToolpathGeometry::ToolpathGeometry(QQuick3DObject *parent)
    : gvgeom::ToolpathGeometry(parent)
{
}

void ToolpathGeometry::setPacked(const QByteArray &raw, const gv::Aabb *bounds, int nv)
{
    gvgeom::ToolpathGeometry::setPacked(raw, bounds, nv);
    emit changed();
}

// ---------------------------------------------------------------------------
// HLRLineGeometry
// ---------------------------------------------------------------------------
HLRLineGeometry::HLRLineGeometry(QQuick3DObject *parent)
    : gvgeom::HLRLineGeometry(parent)
{
}

void HLRLineGeometry::setReference(const double *center)
{
    gvgeom::HLRLineGeometry::setReference(center);
    emit changed();
}

void HLRLineGeometry::setSegments(const float *segs, int count)
{
    gvgeom::HLRLineGeometry::setSegments(segs, count);
    emit changed();
}

// ---------------------------------------------------------------------------
// GridGeometry
// ---------------------------------------------------------------------------
GridGeometry::GridGeometry(QQuick3DObject *parent)
    : gvgeom::GridGeometry(parent)
{
}

void GridGeometry::setSize(double v)
{
    gvgeom::GridGeometry::setSize(v);
    emit sizeChanged();
}

void GridGeometry::setStep(double v)
{
    gvgeom::GridGeometry::setStep(v);
    emit sizeChanged();
}

void GridGeometry::setLevel(double v)
{
    gvgeom::GridGeometry::setLevel(v);
    emit sizeChanged();
}

void GridGeometry::setCenterX(double v)
{
    gvgeom::GridGeometry::setCenterX(v);
    emit sizeChanged();
}

void GridGeometry::setCenterZ(double v)
{
    gvgeom::GridGeometry::setCenterZ(v);
    emit sizeChanged();
}

void GridGeometry::setFeather(double v)
{
    gvgeom::GridGeometry::setFeather(v);
    emit sizeChanged();
}

// ---------------------------------------------------------------------------
// AxisArrowGeometry
// ---------------------------------------------------------------------------
AxisArrowGeometry::AxisArrowGeometry(QQuick3DObject *parent)
    : gvgeom::AxisArrowGeometry(parent)
{
}

void AxisArrowGeometry::setLength(double v)
{
    gvgeom::AxisArrowGeometry::setLength(v);
    emit changed();
}

void AxisArrowGeometry::setHeadLength(double v)
{
    gvgeom::AxisArrowGeometry::setHeadLength(v);
    emit changed();
}

void AxisArrowGeometry::setHeadWidth(double v)
{
    gvgeom::AxisArrowGeometry::setHeadWidth(v);
    emit changed();
}

void AxisArrowGeometry::setShaftWidth(double v)
{
    gvgeom::AxisArrowGeometry::setShaftWidth(v);
    emit changed();
}

// ---------------------------------------------------------------------------
// MeshGeometry
// ---------------------------------------------------------------------------
MeshGeometry::MeshGeometry(QQuick3DObject *parent)
    : gvgeom::MeshGeometry(parent)
{
}

void MeshGeometry::setTriangles(const double *verts, int nFloats)
{
    gvgeom::MeshGeometry::setTriangles(verts, nFloats);
    emit changed();
}

void MeshGeometry::setTriangles(const double *verts, int nFloats, const double *normals)
{
    gvgeom::MeshGeometry::setTriangles(verts, nFloats, normals);
    emit changed();
}

void MeshGeometry::reset()
{
    gvgeom::MeshGeometry::reset();
    emit changed();
}

// ---------------------------------------------------------------------------
// ToolGeometry
// ---------------------------------------------------------------------------
ToolGeometry::ToolGeometry(QQuick3DObject *parent)
    : gvgeom::ToolGeometry(parent)
{
}

void ToolGeometry::setProfile(const double *profile, int count, int segments)
{
    gvgeom::ToolGeometry::setProfile(profile, count, segments);
    emit changed();
}

} // namespace gvqml
