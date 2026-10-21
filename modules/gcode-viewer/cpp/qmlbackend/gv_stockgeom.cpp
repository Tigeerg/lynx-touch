// cpp/qmlbackend/gv_stockgeom.cpp - assembly of the field-top and box-shell
// buffers.
//
// There is no number parsing here: the grid nodes and box corners are computed by
// the core (core/stock_sim, stockSurfaceMesh and stockShellVertices), and this
// file only puts a ready buffer into QQuick3DGeometry in the same order as the
// stage 3 geometries (qt/gv_geometry): clear -> setVertexData -> setStride ->
// addAttribute -> setBounds -> update.
#include "qmlbackend/gv_stockgeom.h"

#include <QVector3D>

namespace gvqml {

namespace {

constexpr QQuick3DGeometry::Attribute::ComponentType F32 =
        QQuick3DGeometry::Attribute::F32Type;

QByteArray asBytes(const std::vector<float> &values)
{
    return values.empty()
        ? QByteArray()
        : QByteArray(reinterpret_cast<const char *>(values.data()),
                     qsizetype(values.size() * sizeof(float)));
}

QByteArray asBytes(const std::vector<std::uint32_t> &values)
{
    return values.empty()
        ? QByteArray()
        : QByteArray(reinterpret_cast<const char *>(values.data()),
                     qsizetype(values.size() * sizeof(std::uint32_t)));
}

// Bounds from a QML list [xmin, xmax, ymin, ymax, zmin, zmax], mm. An empty or
// short list means no bounds: nothing to draw.
bool parseBounds(const QVariantList &list, gv::StockBounds *out)
{
    if (list.size() < 6 || out == nullptr)
        return false;
    out->xMinMm = list.at(0).toDouble();
    out->xMaxMm = list.at(1).toDouble();
    out->yMinMm = list.at(2).toDouble();
    out->yMaxMm = list.at(3).toDouble();
    out->zMinMm = list.at(4).toDouble();
    out->zMaxMm = list.at(5).toDouble();
    return out->xMaxMm > out->xMinMm && out->yMaxMm > out->yMinMm;
}

} // namespace

// ---------------------------------------------------------------------------
// StockSurfaceGeometry
// ---------------------------------------------------------------------------
StockSurfaceGeometry::StockSurfaceGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void StockSurfaceGeometry::setBoundList(const QVariantList &list)
{
    if (list == bound_list_)
        return;
    bound_list_ = list;
    has_bounds_ = parseBounds(list, &bounds_);
    rebuild();
    emit changed();
}

void StockSurfaceGeometry::setCellMm(double value)
{
    if (value == cell_mm_)
        return;
    cell_mm_ = value;
    rebuild();
    emit changed();
}

void StockSurfaceGeometry::setCountX(int value)
{
    if (value == count_x_)
        return;
    count_x_ = value;
    rebuild();
    emit changed();
}

void StockSurfaceGeometry::setCountY(int value)
{
    if (value == count_y_)
        return;
    count_y_ = value;
    rebuild();
    emit changed();
}

void StockSurfaceGeometry::setSubdiv(int value)
{
    if (value == subdiv_)
        return;
    subdiv_ = value;
    rebuild();
    emit changed();
}

void StockSurfaceGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    vertex_count_ = 0;
    triangle_count_ = 0;
    if (!has_bounds_ || count_x_ < 2 || count_y_ < 2) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    const gv::StockSurfaceMesh mesh =
            gv::stockSurfaceMesh(bounds_, cell_mm_, count_x_, count_y_, subdiv_);
    vertex_count_ = int(mesh.vertices.size() / 5);
    triangle_count_ = mesh.triangleCount();
    if (vertex_count_ == 0 || triangle_count_ == 0) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    setVertexData(asBytes(mesh.vertices));
    setIndexData(asBytes(mesh.indices));
    setStride(5 * 4);                      // position 3f + texcoord 2f
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 3 * 4, F32);
    // Without IndexSemantic the indices are not read: buffer acceptance is the
    // standard QQuick3DGeometry path (addAttribute, "to use indexed drawing").
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                 QQuick3DGeometry::Attribute::U32Type);
    // Bounds over the whole height range: a vertex sits at the top of the stock,
    // and the vertex shader lowers it to the bottom - clipping by the top alone
    // would lie.
    setBounds(QVector3D(float(bounds_.xMinMm), float(bounds_.yMinMm), float(bounds_.zMinMm)),
              QVector3D(float(bounds_.xMaxMm), float(bounds_.yMaxMm), float(bounds_.zMaxMm)));
    update();
}

// ---------------------------------------------------------------------------
// StockShellGeometry
// ---------------------------------------------------------------------------
StockShellGeometry::StockShellGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void StockShellGeometry::setBoundList(const QVariantList &list)
{
    if (list == bound_list_)
        return;
    bound_list_ = list;
    has_bounds_ = parseBounds(list, &bounds_);
    rebuild();
    emit changed();
}

void StockShellGeometry::setCellMm(double value)
{
    if (value == cell_mm_)
        return;
    cell_mm_ = value;
    rebuild();
    emit changed();
}

void StockShellGeometry::setCountX(int value)
{
    if (value == count_x_)
        return;
    count_x_ = value;
    rebuild();
    emit changed();
}

void StockShellGeometry::setCountY(int value)
{
    if (value == count_y_)
        return;
    count_y_ = value;
    rebuild();
    emit changed();
}

void StockShellGeometry::setGapSideMm(double value)
{
    if (value == gap_side_mm_)
        return;
    gap_side_mm_ = value;
    rebuild();
    emit changed();
}

void StockShellGeometry::setGapTopMm(double value)
{
    if (value == gap_top_mm_)
        return;
    gap_top_mm_ = value;
    rebuild();
    emit changed();
}

void StockShellGeometry::setGapBottomMm(double value)
{
    if (value == gap_bottom_mm_)
        return;
    gap_bottom_mm_ = value;
    rebuild();
    emit changed();
}

void StockShellGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    vertex_count_ = 0;
    if (!has_bounds_ || count_x_ < 2 || count_y_ < 2) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    const std::vector<float> vertices =
            gv::stockShellVertices(bounds_, cell_mm_, count_x_, count_y_,
                                   gap_side_mm_, gap_top_mm_, gap_bottom_mm_);
    vertex_count_ = int(vertices.size() / 3);
    if (vertex_count_ == 0) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    setVertexData(asBytes(vertices));
    setStride(3 * 4);                      // position 3f
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    // Bounds with the gaps: the wall is larger than the physical bounds by exactly
    // them.
    setBounds(QVector3D(float(bounds_.xMinMm - gap_side_mm_),
                        float(bounds_.yMinMm - gap_side_mm_),
                        float(bounds_.zMinMm - gap_bottom_mm_)),
              QVector3D(float(bounds_.xMaxMm + gap_side_mm_),
                        float(bounds_.yMaxMm + gap_side_mm_),
                        float(bounds_.zMaxMm + gap_top_mm_)));
    update();
}

} // namespace gvqml
