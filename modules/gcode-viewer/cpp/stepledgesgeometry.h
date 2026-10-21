#pragma once

#include <QQuick3DGeometry>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <TopoDS_Shape.hxx>

// =============================================================================
// StepEdgesGeometry — B-Rep edges of a STEP model as QML Lines geometry
// (QQuick3DGeometry, Lines primitive, pair order as in GL_LINES).
//
// Repeats the reference edge geometry of the model
// (occt/step_import.cpp): traversal of TopAbs_EDGE in
// TopExp_Explorer order, the polyline of each edge - GCPnts_UniformDeflection with a
// linear deflection of 0.05 mm (the angular criterion is not used: the reference does
// not set one), then coalescing of nearly coincident segments (_coalesce_segments,
// core/packing.cpp). Seam lines of curved faces (a straight
// edge whose adjacent faces are all curved) are dropped: SolidWorks
// does not draw them either. The selection and coalescing themselves live in gvstep
// (cpp/occt/step_edges): the layer without Qt is also needed by the model assembly
// (cpp/occt/step_bundle.cpp). Agreement with the reference is checked by
// cpp/stepledges_probe.cpp.
//
// Hidden line removal is done by the GPU, not by post-processing: the geometry is
// drawn with a paired material (stepledges.vert) that pulls the lines toward the
// camera in clip space (z *= depthBias), and the ordinary LESS/LESS_EQUAL depth test
// culls lines behind the body. The body must be opaque (it writes to the depth
// buffer).
// =============================================================================

class StepEdgesGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StepEdges)
    QML_ADDED_IN_VERSION(1, 0)

    // Path to the STEP file (.step/.stp). Changing the path re-reads the file;
    // deflection and hideFormEdges recompute the edges from the already read shape.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    // Discretization of curved edges: linear deflection, mm.
    Q_PROPERTY(qreal deflection READ deflection WRITE setDeflection NOTIFY deflectionChanged)
    // Hide seam lines of curved faces (enabled by default).
    Q_PROPERTY(bool hideFormEdges READ hideFormEdges WRITE setHideFormEdges NOTIFY hideFormEdgesChanged)
    // Diagnostics: number of segments in the built buffer.
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY sourceChanged)
    // Port diagnostics: number of segments before coalescing.
    Q_PROPERTY(int rawSegmentCount READ rawSegmentCount NOTIFY sourceChanged)

public:
    explicit StepEdgesGeometry(QQuick3DObject *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &path);

    qreal deflection() const { return m_deflection; }
    void setDeflection(qreal d);

    bool hideFormEdges() const { return m_hideFormEdges; }
    void setHideFormEdges(bool on);

    int segmentCount() const { return m_segmentCount; }
    int rawSegmentCount() const { return m_rawSegmentCount; }

    // C++ host (without QML): building from an already read shape.
    // false — the shape produced no segments at all.
    bool setShape(const TopoDS_Shape &shape);

    // Port diagnostics: segment points in a row, in pairs (x, y, z), float32.
    // The order is the build order, the same as in the reference array.
    QVector<float> segmentPoints() const { return m_points; }

signals:
    void sourceChanged();
    void deflectionChanged();
    void hideFormEdgesChanged();

private:
    void rebuild();                       // source -> geometry buffers

    QString m_source;
    TopoDS_Shape m_shape;                 // parsed STEP; null until the first read
    qreal m_deflection = 0.05;            // mm
    bool m_hideFormEdges = true;
    int m_segmentCount = 0;
    int m_rawSegmentCount = 0;
    QVector<float> m_points;              // diagnostics and vertex buffer
};
