#pragma once

#include <QQuick3DGeometry>
#include <QPointF>
#include <QtQml/qqmlregistration.h>

#include <TopoDS_Shape.hxx>

class QVector3D;

// =============================================================================
// StepEdgesGeometry - CAD edges (B-Rep) of a STEP model as a QML-usable Lines
// geometry (QQuick3DGeometry, primitive Lines, GL_LINES pair order).
//
// All TopAbs_EDGE elements are extracted from the TopoDS_Shape with a
// TopExp_Explorer; straight edges are taken analytically by their endpoints,
// curvilinear ones are discretized by GCPnts_TangentialDeflection (tangential
// criterion: both chord and angle deviation).  "Form" edges are filtered out:
// a straight edge whose every adjacent face is curved (a seam / generatrix of
// a cylinder or cone) is not a model edge, SolidWorks draws it neither.
//
// Visibility (hidden line removal) is done by the GPU, not by post-processing:
// the geometry is rendered with the paired custom material (stepledges.vert),
// which pulls the lines toward the camera in clip space (z *= depthBias),
// while standard LESS/LESS_EQUAL depth testing rejects lines behind the
// solid.  The solid itself must be opaque (write into the depth buffer).
// =============================================================================

class StepEdgesGeometry final : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StepEdges)
    QML_ADDED_IN_VERSION(1, 0)

    // Path to the STEP file (.step/.stp). Reloaded on change.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    // Discretization of curvilinear edges: linear deviation, mm.
    Q_PROPERTY(qreal deflection READ deflection WRITE setDeflection NOTIFY deflectionChanged)
    // Tangential deviation, radians (angle between the chord endpoints).
    Q_PROPERTY(qreal angularDeflection READ angularDeflection WRITE setAngularDeflection NOTIFY deflectionChanged)
    // Hide the seam/generatrix lines of curved faces (on by default).
    Q_PROPERTY(bool hideFormEdges READ hideFormEdges WRITE setHideFormEdges NOTIFY hideFormEdgesChanged)
    // Diagnostics: number of segments in the built buffer.
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY sourceChanged)

public:
    explicit StepEdgesGeometry(QQuick3DObject *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &path);

    qreal deflection() const { return m_deflection; }
    void setDeflection(qreal d);

    qreal angularDeflection() const { return m_angularDeflection; }
    void setAngularDeflection(qreal a);

    bool hideFormEdges() const { return m_hideFormEdges; }
    void setHideFormEdges(bool on);

    int segmentCount() const { return m_segmentCount; }

    // C++ host (without QML): build from an already loaded shape.
    // Returns false if the shape produced no edges.
    bool setShape(const TopoDS_Shape &shape);

signals:
    void sourceChanged();
    void deflectionChanged();
    void hideFormEdgesChanged();

private:
    void rebuild();                       // source -> geometry buffers

    QString m_source;
    qreal m_deflection = 0.05;            // mm
    qreal m_angularDeflection = 0.3;      // rad (~17 deg), like OCCT defaults
    bool m_hideFormEdges = true;
    int m_segmentCount = 0;
};
