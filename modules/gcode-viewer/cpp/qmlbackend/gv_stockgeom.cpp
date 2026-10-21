// cpp/qmlbackend/gv_stockgeom.cpp - сборка буферов верха поля и оболочки коробки.
//
// Разбора чисел здесь нет: узлы сетки и углы коробки считает ядро
// (core/stock_sim, stockSurfaceMesh и stockShellVertices), а этот файл только
// кладёт готовый буфер в QQuick3DGeometry тем же порядком, что геометрии этапа 3
// (qt/gv_geometry): clear -> setVertexData -> setStride -> addAttribute ->
// setBounds -> update.
#include "qmlbackend/gv_stockgeom.h"

#include <QVector3D>

#include <cstring>

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

// Габарит из QML-списка [xmin, xmax, ymin, ymax, zmin, zmax], мм. Пустой или
// короткий список - габарита нет: рисовать нечего.
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

// Поле глубин из байтов: countX * countY float32 в порядке depthMm. Размер не
// совпал (поля ещё нет или оно чужого узла) - пусто, рисовать нечего.
std::vector<float> floatsFromBytes(const QByteArray &bytes, int countX, int countY)
{
    if (countX < 2 || countY < 2)
        return {};
    const qsizetype expected = qsizetype(countX) * qsizetype(countY)
            * qsizetype(sizeof(float));
    if (bytes.size() != expected)
        return {};
    std::vector<float> out(std::size_t(countX) * std::size_t(countY));
    std::memcpy(out.data(), bytes.constData(), std::size_t(bytes.size()));
    return out;
}

// Точная граница из байтов: 4 float32 на отрезок (x0, y0, x1, y1), машинные мм.
// Пусто или размер не кратен четырём - границы нет, рисовать нечего.
std::vector<gv::StockContourSegment> segmentsFromBytes(const QByteArray &bytes)
{
    std::vector<gv::StockContourSegment> out;
    const qsizetype stride = 4 * qsizetype(sizeof(float));
    if (bytes.isEmpty() || bytes.size() % stride != 0)
        return out;
    const int count = int(bytes.size() / stride);
    const float *values = reinterpret_cast<const float *>(bytes.constData());
    out.reserve(std::size_t(count));
    for (int i = 0; i < count; ++i) {
        gv::StockContourSegment segment;
        segment.x0Mm = double(values[i * 4 + 0]);
        segment.y0Mm = double(values[i * 4 + 1]);
        segment.x1Mm = double(values[i * 4 + 2]);
        segment.y1Mm = double(values[i * 4 + 3]);
        out.push_back(segment);
    }
    return out;
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
    setStride(5 * 4);                      // позиция 3f + texcoord 2f
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 3 * 4, F32);
    // Без IndexSemantic индексы не читаются: приёмка буфера - штатный путь
    // QQuick3DGeometry (addAttribute, "to use indexed drawing").
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                 QQuick3DGeometry::Attribute::U32Type);
    // Габарит по всему ходу высоты: вершина стоит на верху заготовки, а
    // вершинный шейдер опускает её до дна - отсечение по одному верху соврало бы.
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
    setStride(3 * 4);                      // позиция 3f
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    // Габарит с зазорами: стенка крупнее физического габарита ровно на них.
    setBounds(QVector3D(float(bounds_.xMinMm - gap_side_mm_),
                        float(bounds_.yMinMm - gap_side_mm_),
                        float(bounds_.zMinMm - gap_bottom_mm_)),
              QVector3D(float(bounds_.xMaxMm + gap_side_mm_),
                        float(bounds_.yMaxMm + gap_side_mm_),
                        float(bounds_.zMaxMm + gap_top_mm_)));
    update();
}

// ---------------------------------------------------------------------------
// StockCutWallGeometry
// ---------------------------------------------------------------------------
StockCutWallGeometry::StockCutWallGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void StockCutWallGeometry::setBoundList(const QVariantList &list)
{
    if (list == bound_list_)
        return;
    bound_list_ = list;
    has_bounds_ = parseBounds(list, &bounds_);
    rebuild();
    emit changed();
}

void StockCutWallGeometry::setCellMm(double value)
{
    if (value == cell_mm_)
        return;
    cell_mm_ = value;
    rebuild();
    emit changed();
}

void StockCutWallGeometry::setCountX(int value)
{
    if (value == count_x_)
        return;
    count_x_ = value;
    rebuild();
    emit changed();
}

void StockCutWallGeometry::setCountY(int value)
{
    if (value == count_y_)
        return;
    count_y_ = value;
    rebuild();
    emit changed();
}

void StockCutWallGeometry::setCutMm(double value)
{
    if (value == cut_mm_)
        return;
    cut_mm_ = value;
    rebuild();
    emit changed();
}

void StockCutWallGeometry::setDepthData(const QByteArray &data)
{
    if (data == depth_data_)
        return;
    depth_data_ = data;
    rebuild();
    emit changed();
}

void StockCutWallGeometry::setBoundaryData(const QByteArray &data)
{
    if (data == boundary_data_)
        return;
    boundary_data_ = data;
    rebuild();
    emit changed();
}

void StockCutWallGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    vertex_count_ = 0;
    triangle_count_ = 0;
    segment_count_ = 0;
    const std::vector<float> depth = floatsFromBytes(depth_data_, count_x_, count_y_);
    const std::vector<gv::StockContourSegment> segments = segmentsFromBytes(boundary_data_);
    if (!has_bounds_ || depth.empty() || segments.empty() || !(cell_mm_ > 0.0)) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    const gv::StockWallMesh mesh = gv::stockCutWallFromLayer(bounds_, cell_mm_, depth,
                                                             count_x_, count_y_, cut_mm_,
                                                             segments);
    vertex_count_ = int(mesh.vertices.size() / 4);
    triangle_count_ = mesh.triangleCount();
    segment_count_ = triangle_count_ / 2;
    if (vertex_count_ == 0 || triangle_count_ == 0) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    setVertexData(asBytes(mesh.vertices));
    setIndexData(asBytes(mesh.indices));
    setStride(4 * 4);                      // позиция 3f + глубина дна 1f (ядро)
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                 QQuick3DGeometry::Attribute::U32Type);
    // Габарит по всему ходу высоты: стенка идёт от верха заготовки вниз, и
    // отсечение по одной вершине соврало бы.
    setBounds(QVector3D(float(bounds_.xMinMm), float(bounds_.yMinMm), float(bounds_.zMinMm)),
              QVector3D(float(bounds_.xMaxMm), float(bounds_.yMaxMm), float(bounds_.zMaxMm)));
    update();
}

// ---------------------------------------------------------------------------
// StockCutLayerGeometry
// ---------------------------------------------------------------------------
StockCutLayerGeometry::StockCutLayerGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void StockCutLayerGeometry::setBoundList(const QVariantList &list)
{
    if (list == bound_list_)
        return;
    bound_list_ = list;
    has_bounds_ = parseBounds(list, &bounds_);
    rebuild();
    emit changed();
}

void StockCutLayerGeometry::setCellMm(double value)
{
    if (value == cell_mm_)
        return;
    cell_mm_ = value;
    rebuild();
    emit changed();
}

void StockCutLayerGeometry::setCountX(int value)
{
    if (value == count_x_)
        return;
    count_x_ = value;
    rebuild();
    emit changed();
}

void StockCutLayerGeometry::setCountY(int value)
{
    if (value == count_y_)
        return;
    count_y_ = value;
    rebuild();
    emit changed();
}

void StockCutLayerGeometry::setCutMm(double value)
{
    if (value == cut_mm_)
        return;
    cut_mm_ = value;
    rebuild();
    emit changed();
}

void StockCutLayerGeometry::setDepthData(const QByteArray &data)
{
    if (data == depth_data_)
        return;
    depth_data_ = data;
    rebuild();
    emit changed();
}

void StockCutLayerGeometry::setBoundaryData(const QByteArray &data)
{
    if (data == boundary_data_)
        return;
    boundary_data_ = data;
    rebuild();
    emit changed();
}

// Раскладка вершины слоистой поверхности: её читает вершинный шейдер поверхности
// (stocksurface.vert), поэтому она объявлена здесь одним местом - и шагом, и
// смещениями. Позиция 3f, texcoord0 2f (выборка поля), texcoord1 2f (сторона
// половины в .x, второй float не читается). TexCoord1 - это vec2: уложись в шаг
// только один float (как было в 0.58.1+mesh.5), движок читал бы у последней
// вершины байты за буфером, а у остальных - начало следующей вершины.
constexpr int kLayerStrideBytes = 7 * 4;
constexpr int kLayerUv0OffsetBytes = 3 * 4;
constexpr int kLayerUv1OffsetBytes = 5 * 4;
constexpr int kLayerUv1Bytes = 2 * 4;
static_assert(kLayerUv1OffsetBytes + kLayerUv1Bytes <= kLayerStrideBytes,
              "texcoord1 (признак стороны) обязан уложиться в шаг вершины слоя");

void StockCutLayerGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    vertex_count_ = 0;
    triangle_count_ = 0;
    top_vertex_count_ = 0;
    floor_triangle_count_ = 0;
    boundary_vertices_ = 0;
    const std::vector<float> depth = floatsFromBytes(depth_data_, count_x_, count_y_);
    const std::vector<gv::StockContourSegment> segments = segmentsFromBytes(boundary_data_);
    if (!has_bounds_ || depth.empty() || !(cell_mm_ > 0.0)) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    const gv::StockLayerSurface mesh = gv::stockLayerSurface(bounds_, cell_mm_, depth,
                                                            count_x_, count_y_, cut_mm_,
                                                            segments);
    vertex_count_ = int(mesh.vertices.size() / 7);
    triangle_count_ = mesh.triangleCount();
    top_vertex_count_ = mesh.topVertexCount;
    floor_triangle_count_ = mesh.floorTriangles();
    boundary_vertices_ = mesh.boundaryVertices;
    if (vertex_count_ == 0 || triangle_count_ == 0) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }

    setVertexData(asBytes(mesh.vertices));
    setIndexData(asBytes(mesh.indices));
    setStride(kLayerStrideBytes);
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, kLayerUv0OffsetBytes, F32);
    // Сторона половины (1 - верх, 0 - дно) - первый float texcoord1 той же
    // вершины: шейдер читает её как UV1.x (stocksurface.vert, steppedDepth), а
    // сторону знает тот, кто режет клетку цепочкой (ядро stockLayerSurface).
    // TexCoord1 - это vec2, поэтому признак занимает два float, и объявлять их
    // обязательно оба: один float вылезал бы за шаг вершины (за раскладкой следит
    // static_assert выше).
    addAttribute(QQuick3DGeometry::Attribute::TexCoord1Semantic, kLayerUv1OffsetBytes, F32);
    addAttribute(QQuick3DGeometry::Attribute::IndexSemantic, 0,
                 QQuick3DGeometry::Attribute::U32Type);
    // Габарит по всему ходу высоты: вершина стоит на верху заготовки, а
    // вершинный шейдер опускает её до дна - отсечение по одному верху соврало бы.
    setBounds(QVector3D(float(bounds_.xMinMm), float(bounds_.yMinMm), float(bounds_.zMinMm)),
              QVector3D(float(bounds_.xMaxMm), float(bounds_.yMaxMm), float(bounds_.zMaxMm)));
    update();
}

// ---------------------------------------------------------------------------
// StockCutContourGeometry
// ---------------------------------------------------------------------------
StockCutContourGeometry::StockCutContourGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
    rebuild();
}

void StockCutContourGeometry::setBoundList(const QVariantList &list)
{
    if (list == bound_list_)
        return;
    bound_list_ = list;
    has_bounds_ = parseBounds(list, &bounds_);
    rebuild();
    emit changed();
}

void StockCutContourGeometry::setBoundaryData(const QByteArray &data)
{
    if (data == boundary_data_)
        return;
    boundary_data_ = data;
    rebuild();
    emit changed();
}

void StockCutContourGeometry::rebuild()
{
    clear();
    setPrimitiveType(QQuick3DGeometry::PrimitiveType::Lines);
    vertex_count_ = 0;
    segment_count_ = 0;
    const std::vector<gv::StockContourSegment> contour = segmentsFromBytes(boundary_data_);
    if (!has_bounds_ || contour.empty()) {
        setBounds(QVector3D(0, 0, 0), QVector3D(0, 0, 0));
        update();
        return;
    }
    segment_count_ = int(contour.size());

    // Контур лежит на плоскости нетронутого верха: две вершины на отрезок, порядок
    // тот же, что у отрезков ядра (PrimitiveType::Lines индексов не требует).
    const float zTop = float(bounds_.zMaxMm);
    std::vector<float> vertices;
    vertices.reserve(contour.size() * 6);
    for (const gv::StockContourSegment &segment : contour) {
        vertices.push_back(float(segment.x0Mm));
        vertices.push_back(float(segment.y0Mm));
        vertices.push_back(zTop);
        vertices.push_back(float(segment.x1Mm));
        vertices.push_back(float(segment.y1Mm));
        vertices.push_back(zTop);
    }
    vertex_count_ = int(vertices.size() / 3);

    setVertexData(asBytes(vertices));
    setStride(3 * 4);                      // позиция 3f
    addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, F32);
    setBounds(QVector3D(float(bounds_.xMinMm), float(bounds_.yMinMm), float(bounds_.zMaxMm)),
              QVector3D(float(bounds_.xMaxMm), float(bounds_.yMaxMm), float(bounds_.zMaxMm)));
    update();
}

} // namespace gvqml
