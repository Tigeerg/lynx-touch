#include "stepledgesgeometry.h"

#include <QFile>
#include <QFileInfo>
#include <QVector3D>

// OCCT: STEP reading
#include <STEPControl_Reader.hxx>
#include <IFSelect_ReturnStatus.hxx>

// OCCT: topology traversal and edge geometry
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <BRep_Tool.hxx>
#include <gp_Pnt.hxx>

namespace {

// Endpoints of the trimmed edge curve (already 3D, location applied).
bool edgeEndpoints(const TopoDS_Edge &edge, gp_Pnt &p0, gp_Pnt &p1)
{
    BRepAdaptor_Curve curve(edge);
    curve.D0(curve.FirstParameter(), p0);
    curve.D0(curve.LastParameter(), p1);
    return true;
}

// A straight edge whose every adjacent face is a non-planar surface is a
// parametric line of the surface (seam, generatrix): CAD hides them.
bool isFormEdge(const TopoDS_Edge &edge,
                const TopTools_IndexedDataMapOfShapeListOfShape &edgeFaces)
{
    if (!edgeFaces.Contains(edge))
        return false;                       // free edge: a real boundary
    const TopTools_ListOfShape &faces = edgeFaces.FindFromKey(edge);
    bool allCurved = true;
    for (TopTools_ListOfShape::Iterator it(faces); it.More(); it.Next()) {
        const TopoDS_Face &face = TopoDS::Face(it.Value());
        if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane) {
            allCurved = false;
            break;
        }
    }
    return allCurved;
}

void appendPolyline(QVector<float> &pos, const QVector3D &a, const QVector3D &b)
{
    pos << a.x() << a.y() << a.z()
        << b.x() << b.y() << b.z();
}

QVector3D toQt(const gp_Pnt &p)
{
    return QVector3D(float(p.X()), float(p.Y()), float(p.Z()));
}

} // namespace

StepEdgesGeometry::StepEdgesGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
}

void StepEdgesGeometry::setSource(const QString &path)
{
    if (m_source == path)
        return;
    m_source = path;
    rebuild();
    emit sourceChanged();
}

void StepEdgesGeometry::setDeflection(qreal d)
{
    d = qMax<qreal>(0.001, d);
    if (qFuzzyCompare(m_deflection, d))
        return;
    m_deflection = d;
    emit deflectionChanged();
    rebuild();
}

void StepEdgesGeometry::setAngularDeflection(qreal a)
{
    a = qBound<qreal>(0.01, a, 1.5);
    if (qFuzzyCompare(m_angularDeflection, a))
        return;
    m_angularDeflection = a;
    emit deflectionChanged();
    rebuild();
}

void StepEdgesGeometry::setHideFormEdges(bool on)
{
    if (m_hideFormEdges == on)
        return;
    m_hideFormEdges = on;
    emit hideFormEdgesChanged();
    rebuild();
}

bool StepEdgesGeometry::setShape(const TopoDS_Shape &shape)
{
    clear();                                             // QQuick3DGeometry

    // EDGE -> FACE map for the form edge filter (built once per shape).
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    if (m_hideFormEdges)
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE,
                                      edgeFaces);

    QVector<float> pos;                                  // interleaved x,y,z
    pos.reserve(4096);
    QVector3D lo(1e18f, 1e18f, 1e18f), hi(-1e18f, -1e18f, -1e18f);
    int segments = 0;

    auto track = [&](const QVector3D &p) {
        lo.setMin(p);
        hi.setMax(p);
    };

    for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(exp.Current());
        if (BRep_Tool::Degenerated(edge))
            continue;
        if (m_hideFormEdges && isFormEdge(edge, edgeFaces))
            continue;

        BRepAdaptor_Curve curve(edge);
        const bool line = curve.GetType() == GeomAbs_Line;
        QVector<QVector3D> pts;
        if (line) {
            gp_Pnt a, b;
            edgeEndpoints(edge, a, b);
            pts = { toQt(a), toQt(b) };                  // exact endpoints
        } else {
            // Tangential criterion: every chord deviates by no more than
            // `deflection` mm and bends by no more than `angularDeflection` rad.
            GCPnts_TangentialDeflection sampler(
                curve, m_deflection, m_angularDeflection);
            if (sampler.NbPoints() < 2)
                continue;
            pts.reserve(sampler.NbPoints());
            for (int i = 1; i <= sampler.NbPoints(); ++i)
                pts.append(toQt(sampler.Value(i)));
        }
        for (int i = 0; i + 1 < pts.size(); ++i) {
            appendPolyline(pos, pts[i], pts[i + 1]);     // LINES pair
            track(pts[i]);
            track(pts[i + 1]);
            ++segments;
        }
    }

    if (pos.isEmpty())
        return false;

    setPrimitiveType(PrimitiveType::Lines);              // GL_LINES semantics
    setStride(3 * sizeof(float));
    setVertexData(QByteArray(reinterpret_cast<const char *>(pos.constData()),
                             int(pos.size() * sizeof(float))));
    addAttribute(Attribute::Semantic::PositionSemantic, 0,
                 Attribute::ComponentType::F32Type);
    setBounds(lo, hi);
    update();
    m_segmentCount = segments;
    return true;
}

void StepEdgesGeometry::rebuild()
{
    clear();
    m_segmentCount = 0;
    if (m_source.isEmpty() || !QFileInfo::exists(m_source)) {
        update();
        return;
    }

    STEPControl_Reader reader;
    if (reader.ReadFile(m_source.toLocal8Bit().constData())
            != IFSelect_ReturnStatus::IFSelect_RetDone) {
        qWarning("StepEdgesGeometry: STEP read failed: %s",
                 qUtf8Printable(m_source));
        update();
        return;
    }
    reader.TransferRoots();
    setShape(reader.OneShape());
}
