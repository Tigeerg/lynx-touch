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

// Стенка точного контура реза (PLAN-STOCK.md, 3.1/1.5): вертикальные квады по
// ТОЧНОЙ границе реза (core/stock_sim, stockCutBoundary), а не по marching
// squares поля. Граница приходит готовой полилинией (boundaryData), низ квада
// берётся по глубине поля в точке контура (depthData, билинейно), верх стоит на
// плоскости нетронутого верха - стык верх/стенка/дно идёт по одному контуру.
// Поле и его байты не трогаются: точную границу считает ядро по ходам.
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
    Q_PROPERTY(QByteArray boundaryData READ boundaryData WRITE setBoundaryData NOTIFY changed)
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
    // Порог съёма, мм: им ядро отделяет тронутый узел поля от нетронутого
    // (знак границы). То же число, что у цвета поверхности (stocksurface.frag).
    double cutMm() const { return cut_mm_; }
    void setCutMm(double value);
    // Поле глубин байтами: countX * countY float32 в порядке depthMm. Нужно
    // только низу квада (глубина дна в точке контура). Пусто - стенки нет.
    QByteArray depthData() const { return depth_data_; }
    void setDepthData(const QByteArray &data);
    // Точная граница байтами: 4 float32 на отрезок (x0, y0, x1, y1), машинные
    // мм. Это представление границы (ядро, stockCutBoundary); пусто - стенки нет.
    QByteArray boundaryData() const { return boundary_data_; }
    void setBoundaryData(const QByteArray &data);

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
    QByteArray boundary_data_;
    int vertex_count_ = 0;
    int triangle_count_ = 0;
    int segment_count_ = 0;
};

// Слоистая поверхность остатка (PLAN-STOCK.md, 3.1; этап 1.3): верх нетронутого
// материала и дно реза ОДНОЙ сеткой, разрезанной по ТОЧНОЙ границе реза
// (boundaryData), а не порогом поля. Клетка без отрезков границы уходит своей
// половине двумя треугольниками, клетка с отрезками режется ими по отрезкам, и
// вершины схода ложатся ровно на полилинию границы: полосы перехода между
// уровнями нет вовсе, а отвесную грань между половинами несёт слой стенки
// (StockCutWallGeometry). Высоту вычитает тот же вершинный шейдер поверхности
// (stocksurface.vert) по texcoord, а половину вершины на границе называет её
// texcoord, сдвинутый на волосок внутрь половины (ядро, stockLayerSurface):
// поэтому ни шейдер, ни его материал не меняются - меняется только меш.
// Дробления клетки здесь нет: билинейная высота на подклетке - та же
// поверхность, а ступень уступа режется геометрией, а не дроблением.
class StockCutLayerGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList boundList READ boundList WRITE setBoundList NOTIFY changed)
    Q_PROPERTY(double cellMm READ cellMm WRITE setCellMm NOTIFY changed)
    Q_PROPERTY(int countX READ countX WRITE setCountX NOTIFY changed)
    Q_PROPERTY(int countY READ countY WRITE setCountY NOTIFY changed)
    Q_PROPERTY(double cutMm READ cutMm WRITE setCutMm NOTIFY changed)
    Q_PROPERTY(QByteArray depthData READ depthData WRITE setDepthData NOTIFY changed)
    Q_PROPERTY(QByteArray boundaryData READ boundaryData WRITE setBoundaryData NOTIFY changed)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY changed)
    Q_PROPERTY(int topVertexCount READ topVertexCount NOTIFY changed)
    Q_PROPERTY(int floorTriangleCount READ floorTriangleCount NOTIFY changed)
    Q_PROPERTY(int boundaryVertices READ boundaryVertices NOTIFY changed)

public:
    explicit StockCutLayerGeometry(QQuick3DObject *parent = nullptr);

    QVariantList boundList() const { return bound_list_; }
    void setBoundList(const QVariantList &list);
    double cellMm() const { return cell_mm_; }
    void setCellMm(double value);
    int countX() const { return count_x_; }
    void setCountX(int value);
    int countY() const { return count_y_; }
    void setCountY(int value);
    // Порог съёма, мм: им ядро отделяет тронутый узел поля от нетронутого (знак
    // границы). То же число, что у цвета поверхности (stocksurface.frag).
    double cutMm() const { return cut_mm_; }
    void setCutMm(double value);
    // Поле глубин байтами: countX * countY float32 в порядке depthMm. Пусто -
    // рисовать нечего: половины называет поле.
    QByteArray depthData() const { return depth_data_; }
    void setDepthData(const QByteArray &data);
    // Точная граница байтами: 4 float32 на отрезок (x0, y0, x1, y1), машинные мм
    // (ядро, stockCutBoundary). Пусто - границы нет, и каждая клетка уходит
    // половине по знаку своих узлов (нетронутая заготовка целиком верх).
    QByteArray boundaryData() const { return boundary_data_; }
    void setBoundaryData(const QByteArray &data);

    int vertexCount() const { return vertex_count_; }
    int triangleCount() const { return triangle_count_; }
    int topVertexCount() const { return top_vertex_count_; }
    int floorTriangleCount() const { return floor_triangle_count_; }
    int boundaryVertices() const { return boundary_vertices_; }

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
    QByteArray boundary_data_;
    int vertex_count_ = 0;
    int triangle_count_ = 0;
    int top_vertex_count_ = 0;
    int floor_triangle_count_ = 0;
    int boundary_vertices_ = 0;
};

// Точный контур реза отрезками линий: точная граница (ядро, stockCutBoundary)
// как полилиния на плоскости нетронутого верха (zMaxMm). Граница приходит
// готовыми отрезками (boundaryData), а не считается по полю: она независима от
// узла, и её рисуют поверх ступеней точного вида для сверки. Буфер - 12 Б на
// вершину (позиция 3f), две вершины на отрезок, PrimitiveType::Lines.
class StockCutContourGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList boundList READ boundList WRITE setBoundList NOTIFY changed)
    Q_PROPERTY(QByteArray boundaryData READ boundaryData WRITE setBoundaryData NOTIFY changed)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY changed)

public:
    explicit StockCutContourGeometry(QQuick3DObject *parent = nullptr);

    QVariantList boundList() const { return bound_list_; }
    void setBoundList(const QVariantList &list);
    QByteArray boundaryData() const { return boundary_data_; }
    void setBoundaryData(const QByteArray &data);

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
    QByteArray boundary_data_;
    int vertex_count_ = 0;
    int segment_count_ = 0;
};

} // namespace gvqml
