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

// Стенка точного контура реза (PLAN-STOCK.md, 3.1): вертикальные квады по границе,
// где глубина поля линейно пересекает порог cutMm. Контур считает marching squares
// (core/stock_sim, stockCutContour), поэтому стенка кармана - ровная линия
// под-ячейковой точности, а не ступени узлов поля; верх стоит на плоскости
// нетронутого верха (zMaxMm), низ - на дне реза в точке контура, поэтому стык
// верх/стенка/дно идёт по одному контуру, без щели.
//
// Буфер - тот же, что отдаёт ядро (stockCutWall): 16 Б на вершину (позиция 3f +
// глубина дна 1f), индексы U32, два треугольника на отрезок контура. Четвёртое
// число оставлено как у ядра (им сверяется буфер), но шейдеру оно не нужно:
// стенка целиком снятый металл, её цвет - от порога съёма (свойство cutMm).
// Поле приходит байтами поля глубин (QByteArray float32, порядок depthMm), а не
// текстурой: контур - это геометрия, и считать его надо там же, где он строится.
class StockCutWallGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList boundList READ boundList WRITE setBoundList NOTIFY changed)
    Q_PROPERTY(double cellMm READ cellMm WRITE setCellMm NOTIFY changed)
    Q_PROPERTY(int countX READ countX WRITE setCountX NOTIFY changed)
    Q_PROPERTY(int countY READ countY WRITE setCountY NOTIFY changed)
    Q_PROPERTY(double cutMm READ cutMm WRITE setCutMm NOTIFY changed)
    Q_PROPERTY(QByteArray depthData READ depthData WRITE setDepthData NOTIFY changed)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY changed)
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY changed)

public:
    explicit StockCutWallGeometry(QQuick3DObject *parent = nullptr);

    QVariantList boundList() const { return bound_list_; }
    void setBoundList(const QVariantList &list);
    double cellMm() const { return cell_mm_; }
    void setCellMm(double value);
    int countX() const { return count_x_; }
    void setCountX(int value);
    int countY() const { return count_y_; }
    void setCountY(int value);
    // Порог съёма, мм: граница реза - где глубина поля пересекает его. То же
    // число, что у цвета поверхности (stocksurface.frag, свойство cutMm в QML).
    double cutMm() const { return cut_mm_; }
    void setCutMm(double value);
    // Поле глубин байтами: countX * countY float32 в порядке depthMm (та же
    // копия, что идёт текстурой в stockField). Пусто - поля нет, стенки нет.
    QByteArray depthData() const { return depth_data_; }
    void setDepthData(const QByteArray &data);

    int vertexCount() const { return vertex_count_; }
    int triangleCount() const { return triangle_count_; }
    int segmentCount() const { return segment_count_; }

    gv::StockBounds bounds() const { return bounds_; }

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
    double cut_mm_ = 0.05;
    QByteArray depth_data_;
    int vertex_count_ = 0;
    int triangle_count_ = 0;
    int segment_count_ = 0;
};

// Точный контур реза отрезками линий: та же граница, что у стенки, но нарисованная
// как полилиния на плоскости нетронутого верха (zMaxMm). Нужен для прямой сверки
// со ступенчатой границей поля: он показывает, где граница реза на самом деле.
// Буфер - 12 Б на вершину (позиция 3f), две вершины на отрезок контура,
// PrimitiveType::Lines.
class StockCutContourGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList boundList READ boundList WRITE setBoundList NOTIFY changed)
    Q_PROPERTY(double cellMm READ cellMm WRITE setCellMm NOTIFY changed)
    Q_PROPERTY(int countX READ countX WRITE setCountX NOTIFY changed)
    Q_PROPERTY(int countY READ countY WRITE setCountY NOTIFY changed)
    Q_PROPERTY(double cutMm READ cutMm WRITE setCutMm NOTIFY changed)
    Q_PROPERTY(QByteArray depthData READ depthData WRITE setDepthData NOTIFY changed)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY changed)

public:
    explicit StockCutContourGeometry(QQuick3DObject *parent = nullptr);

    QVariantList boundList() const { return bound_list_; }
    void setBoundList(const QVariantList &list);
    double cellMm() const { return cell_mm_; }
    void setCellMm(double value);
    int countX() const { return count_x_; }
    void setCountX(int value);
    int countY() const { return count_y_; }
    void setCountY(int value);
    double cutMm() const { return cut_mm_; }
    void setCutMm(double value);
    QByteArray depthData() const { return depth_data_; }
    void setDepthData(const QByteArray &data);

    int vertexCount() const { return vertex_count_; }
    int segmentCount() const { return segment_count_; }

    gv::StockBounds bounds() const { return bounds_; }

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
    double cut_mm_ = 0.05;
    QByteArray depth_data_;
    int vertex_count_ = 0;
    int segment_count_ = 0;
};

} // namespace gvqml
