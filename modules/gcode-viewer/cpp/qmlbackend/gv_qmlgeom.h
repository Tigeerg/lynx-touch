// cpp/qmlbackend/gv_qmlgeom.h - QML geometry types of the MockBackend module.
//
// The six geometries of stage 3 (qt/gv_geometry, target gvqtgeom) already build
// buffers equal to the reference qt/gv_geometry.cpp byte for byte, but they are
// plain C++ classes: QML does not see them. The classes below are thin
// subclasses: the buffer, attributes, stride and vertex order stay stage 3 code,
// and here only what the QML contract requires is added - registration under the
// URI MockBackend 1.0, properties with signals (GcodeViewer3D.qml sets them on
// GridGeometry and AxisArrowGeometry) and counters (vertexCount, triangleCount)
// that the checks read.
//
// The buffer formats are the contract of QML and the shaders, not an
// implementation detail:
// ToolpathGeometry stride 20 = position 3f + texcoord0 2f (toolpath.vert/.frag);
// HLRLineGeometry stride 48 = position vec4, normal vec4, texcoord0 vec4 (stepledges.vert);
// GridGeometry stride 28 = position 3f + RGBA 4f;
// AxisArrowGeometry stride 12 = position 3f;
// MeshGeometry stride 24 = position 3f + normal 3f;
// ToolGeometry stride 24 = position 3f + normal 3f.
// The layout and the format breakdown are in qt/gv_geometry.h; they are not
// duplicated here.
//
// QML creates the grid and the axis marker itself by name from MockBackend (see
// GcodeViewer3D.qml); the other four arrive as controller properties.
#pragma once

#include <QByteArray>
#include <QQuick3DObject>
#include <QtQml/qqmlregistration.h>

#include "qt/gv_geometry.h"

namespace gvqml {

// ---------------------------------------------------------------------------
// ToolpathGeometry: path lines (stride 20), the path parameter in TexCoord0.x
// ---------------------------------------------------------------------------
class ToolpathGeometry : public gvgeom::ToolpathGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)

public:
    explicit ToolpathGeometry(QQuick3DObject *parent = nullptr);

    // Set a ready buffer (assembled outside the GUI thread) and notify QML.
    void setPacked(const QByteArray &raw, const gv::Aabb *bounds, int nv);

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// HLRLineGeometry: contour ribbons (stride 48) with an offset from the reference point
// ---------------------------------------------------------------------------
class HLRLineGeometry : public gvgeom::HLRLineGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)

public:
    explicit HLRLineGeometry(QQuick3DObject *parent = nullptr);

    void setReference(const double *center);
    void setSegments(const float *segs, int count);

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// GridGeometry: floor grid in the XZ plane fading toward the edge
// ---------------------------------------------------------------------------
class GridGeometry : public gvgeom::GridGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double size READ size WRITE setSize NOTIFY sizeChanged)
    Q_PROPERTY(double step READ step WRITE setStep NOTIFY sizeChanged)
    Q_PROPERTY(double level READ level WRITE setLevel NOTIFY sizeChanged)
    Q_PROPERTY(double centerX READ centerX WRITE setCenterX NOTIFY sizeChanged)
    Q_PROPERTY(double centerZ READ centerZ WRITE setCenterZ NOTIFY sizeChanged)
    Q_PROPERTY(double feather READ feather WRITE setFeather NOTIFY sizeChanged)

public:
    explicit GridGeometry(QQuick3DObject *parent = nullptr);

    // The setter overrides the base one: the same buffer recompute plus a QML
    // signal.
    void setSize(double v);
    void setStep(double v);
    void setLevel(double v);
    void setCenterX(double v);
    void setCenterZ(double v);
    void setFeather(double v);

signals:
    void sizeChanged();
};

// ---------------------------------------------------------------------------
// AxisArrowGeometry: a flat double-sided axis marker (stride 12)
// ---------------------------------------------------------------------------
class AxisArrowGeometry : public gvgeom::AxisArrowGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(double length READ length WRITE setLength NOTIFY changed)
    Q_PROPERTY(double headLength READ headLength WRITE setHeadLength NOTIFY changed)
    Q_PROPERTY(double headWidth READ headWidth WRITE setHeadWidth NOTIFY changed)
    Q_PROPERTY(double shaftWidth READ shaftWidth WRITE setShaftWidth NOTIFY changed)

public:
    explicit AxisArrowGeometry(QQuick3DObject *parent = nullptr);

    void setLength(double v);
    void setHeadLength(double v);
    void setHeadWidth(double v);
    void setShaftWidth(double v);

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// MeshGeometry: skin mesh (stride 24), flat normals are computed by stage 3
// ---------------------------------------------------------------------------
class MeshGeometry : public gvgeom::MeshGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY changed)

public:
    explicit MeshGeometry(QQuick3DObject *parent = nullptr);

    void setTriangles(const double *verts, int nFloats);
    void setTriangles(const double *verts, int nFloats, const double *normals);
    void reset();

signals:
    void changed();
};

// ---------------------------------------------------------------------------
// ToolGeometry: a surface of revolution from a tool/holder profile (stride 24)
// ---------------------------------------------------------------------------
class ToolGeometry : public gvgeom::ToolGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY changed)

public:
    explicit ToolGeometry(QQuick3DObject *parent = nullptr);

    void setProfile(const double *profile, int count, int segments = 0);

signals:
    void changed();
};

} // namespace gvqml
