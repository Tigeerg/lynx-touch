// cpp/qmlbackend/gv_stockgeom.h - geometries for drawing the stock remainder: the
// top of the height field and the box shell (walls and bottom) without the top
// face.
//
// These two are not part of the set of six stage 3 geometries (qt/gv_geometry,
// checked byte for byte and by sha256 in oracle_manifest.json) and must not be:
// there is no removal field in the manifest, and the top height comes from the
// field texture, not from a buffer. The mesh here is only vertex positions: it is
// built from the same nodes that core/stock_sim computes (stockSurfaceMesh,
// stockShellVertices), so the layer check is the stocksurface and stockshell
// cases in cpp/stock_sim_probe.cpp, not the six-geometry buffer.
//
// The top buffer format is 20 B per vertex (position 3f + texcoord 2f), like the
// path buffers: the stocksurface.vert vertex shader needs exactly these two
// attributes, and the indices (U32) assemble the field cells into triangles.
#pragma once

#include <QByteArray>
#include <QQuick3DGeometry>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "core/stock_sim.h"

namespace gvqml {

// Top of the height field: a grid of nodes in machine mm, the height is set by
// the vertex shader from the field texture. The properties repeat what the
// controller publishes about the field (stockBounds, stockCellMm,
// stockCountX/Y), so QML binds them straight to the controller: one truth,
// without a second node counter.
class StockSurfaceGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList boundList READ boundList WRITE setBoundList NOTIFY changed)
    Q_PROPERTY(double cellMm READ cellMm WRITE setCellMm NOTIFY changed)
    Q_PROPERTY(int countX READ countX WRITE setCountX NOTIFY changed)
    Q_PROPERTY(int countY READ countY WRITE setCountY NOTIFY changed)
    Q_PROPERTY(int subdiv READ subdiv WRITE setSubdiv NOTIFY changed)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY changed)

public:
    explicit StockSurfaceGeometry(QQuick3DObject *parent = nullptr);

    QVariantList boundList() const { return bound_list_; }
    void setBoundList(const QVariantList &list);
    double cellMm() const { return cell_mm_; }
    void setCellMm(double value);
    int countX() const { return count_x_; }
    void setCountX(int value);
    int countY() const { return count_y_; }
    void setCountY(int value);
    // Subdivision of a field cell in the drawn mesh (1 - a field node). It
    // changes only the drawing geometry: the field and its checks are computed
    // from the nodes (core/stock_sim, stockSurfaceMesh). Needed for the smooth
    // view - see stocksurface.vert.
    int subdiv() const { return subdiv_; }
    void setSubdiv(int value);

    int vertexCount() const { return vertex_count_; }
    int triangleCount() const { return triangle_count_; }

    // The bounds the mesh was built by, machine mm: the probe uses them to check
    // the mesh against the field without parsing the buffer.
    gv::StockBounds bounds() const { return bounds_; }

signals:
    void changed();

private:
    void rebuild();

    QVariantList bound_list_;      // [xmin, xmax, ymin, ymax, zmin, zmax], mm
    gv::StockBounds bounds_;
    bool has_bounds_ = false;
    double cell_mm_ = 0.5;
    int count_x_ = 0;
    int count_y_ = 0;
    int subdiv_ = 1;
    int vertex_count_ = 0;
    int triangle_count_ = 0;
};

// Box walls and bottom: five rectangles in machine mm, no top face - the field
// draws it. The gaps are the former fractions of a pixel that separate coincident
// stock and part faces; they do not move the physical bounds.
class StockShellGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList boundList READ boundList WRITE setBoundList NOTIFY changed)
    Q_PROPERTY(double cellMm READ cellMm WRITE setCellMm NOTIFY changed)
    Q_PROPERTY(int countX READ countX WRITE setCountX NOTIFY changed)
    Q_PROPERTY(int countY READ countY WRITE setCountY NOTIFY changed)
    Q_PROPERTY(double gapSideMm READ gapSideMm WRITE setGapSideMm NOTIFY changed)
    Q_PROPERTY(double gapTopMm READ gapTopMm WRITE setGapTopMm NOTIFY changed)
    Q_PROPERTY(double gapBottomMm READ gapBottomMm WRITE setGapBottomMm NOTIFY changed)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)

public:
    explicit StockShellGeometry(QQuick3DObject *parent = nullptr);

    QVariantList boundList() const { return bound_list_; }
    void setBoundList(const QVariantList &list);
    double cellMm() const { return cell_mm_; }
    void setCellMm(double value);
    int countX() const { return count_x_; }
    void setCountX(int value);
    int countY() const { return count_y_; }
    void setCountY(int value);
    double gapSideMm() const { return gap_side_mm_; }
    void setGapSideMm(double value);
    double gapTopMm() const { return gap_top_mm_; }
    void setGapTopMm(double value);
    double gapBottomMm() const { return gap_bottom_mm_; }
    void setGapBottomMm(double value);

    int vertexCount() const { return vertex_count_; }

signals:
    void changed();

private:
    void rebuild();

    QVariantList bound_list_;
    gv::StockBounds bounds_;
    bool has_bounds_ = false;
    double cell_mm_ = 0.5;
    int count_x_ = 0;
    int count_y_ = 0;
    double gap_side_mm_ = 0.0;
    double gap_top_mm_ = 0.0;
    double gap_bottom_mm_ = 0.0;
    int vertex_count_ = 0;
};

} // namespace gvqml
