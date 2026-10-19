// cpp/qmlbackend/gv_stockgeom.h - геометрии рисования остатка заготовки: верх
// поля высот и оболочка коробки (стенки и дно) без верхней грани.
//
// В набор шести геометрий этапа 3 (qt/gv_geometry сверка
// байт в байт и sha256 в oracle_manifest.json) эти две не входят и не должны:
// поля съёма в манифесте нет, а высота верха приходит не из буфера, а из
// текстуры поля. Меша здесь только места вершин: он строится по тем же узлам,
// что считает core/stock_sim (stockSurfaceMesh, stockShellVertices), поэтому
// проверка слоя - случаи stocksurface и stockshell в cpp/stock_sim_probe.cpp,
// а не буфер шести геометрий.
//
// Формат буфера верха - 20 Б на вершину (позиция 3f + texcoord 2f), как у
// буферов траектории: вершинному шейдеру stocksurface.vert нужны ровно эти
// два атрибута, а индексы (U32) собирают ячейки поля в треугольники.
#pragma once

#include <QByteArray>
#include <QQuick3DGeometry>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "core/stock_sim.h"

namespace gvqml {

// Верх поля высот: сетка узлов в машинных мм, высота - вершинный шейдер по
// текстуре поля. Свойства повторяют то, что контроллер публикует о поле
// (stockBounds, stockCellMm, stockCountX/Y), поэтому QML связывает их прямо с
// контроллером: одна правда, без второго счётчика узлов.
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
    // Дробление клетки поля в рисуемом меше (1 - узел поля). Меняет только
    // геометрию рисования: поле и его сверки считаются по узлам (core/stock_sim,
    // stockSurfaceMesh). Нужно гладкому виду - см. stocksurface.vert.
    int subdiv() const { return subdiv_; }
    void setSubdiv(int value);

    int vertexCount() const { return vertex_count_; }
    int triangleCount() const { return triangle_count_; }

    // Габарит, по которому построен меш, машинные мм: им пробник сверяет меш
    // с полем, не разбирая буфер.
    gv::StockBounds bounds() const { return bounds_; }

signals:
    void changed();

private:
    void rebuild();

    QVariantList bound_list_;      // [xmin, xmax, ymin, ymax, zmin, zmax], мм
    gv::StockBounds bounds_;
    bool has_bounds_ = false;
    double cell_mm_ = 0.5;
    int count_x_ = 0;
    int count_y_ = 0;
    int subdiv_ = 1;
    int vertex_count_ = 0;
    int triangle_count_ = 0;
};

// Стенки и дно коробки: пять прямоугольников в машинных мм, верхней грани нет -
// её рисует поле. Зазоры - прежние доли пикселя, которыми разводят совпадающие
// грани заготовки и детали; физический габарит они не двигают.
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
