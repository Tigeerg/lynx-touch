#pragma once

#include <QQuick3DGeometry>
#include <QVector>
#include <QtQml/qqmlregistration.h>

#include <TopoDS_Shape.hxx>

// =============================================================================
// StepEdgesGeometry — рёбра B-Rep модели STEP как Lines-геометрия QML
// (QQuick3DGeometry, примитив Lines, порядок пар как у GL_LINES).
//
// Повторяет эталонную геометрию рёбер модели
// (occt/step_import.cpp): обход TopAbs_EDGE в порядке
// TopExp_Explorer, полилиния каждого ребра - GCPnts_UniformDeflection с линейным
// допуском 0.05 мм (угловой критерий не используется: эталон его не задаёт),
// затем склейка почти совпадающих отрезков (_coalesce_segments,
// core/packing.cpp). Шовные линии криволинейных граней (прямое
// ребро, у которого все смежные грани криволинейны) отбрасываются: SolidWorks
// их тоже не рисует. Сам отбор и склейка лежат в gvstep (cpp/occt/step_edges):
// слой без Qt нужен и сборке модели (cpp/occt/step_bundle.cpp). Совпадение с
// эталоном проверяет cpp/stepledges_probe.cpp.
//
// Скрытие невидимых линий делает GPU, а не постобработка: геометрия рисуется
// парным материалом (stepledges.vert), который подтягивает линии к камере в
// clip space (z *= depthBias), а обычный тест глубины LESS/LESS_EQUAL отсекает
// линии за телом. Тело обязано быть непрозрачным (пишет в буфер глубины).
// =============================================================================

class StepEdgesGeometry : public QQuick3DGeometry
{
    Q_OBJECT
    QML_NAMED_ELEMENT(StepEdges)
    QML_ADDED_IN_VERSION(1, 0)

    // Путь к файлу STEP (.step/.stp). Смена пути перечитывает файл; deflection и
    // hideFormEdges пересчитывают рёбра по уже прочитанной форме.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    // Дискретизация криволинейных рёбер: линейный допуск, мм.
    Q_PROPERTY(qreal deflection READ deflection WRITE setDeflection NOTIFY deflectionChanged)
    // Скрывать шовные линии криволинейных граней (по умолчанию включено).
    Q_PROPERTY(bool hideFormEdges READ hideFormEdges WRITE setHideFormEdges NOTIFY hideFormEdgesChanged)
    // Диагностика: число отрезков в построенном буфере.
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY sourceChanged)
    // Диагностика переноса: число отрезков до склейки.
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

    // Хост на C++ (без QML): построение по уже прочитанной форме.
    // false — форма не дала ни одного отрезка.
    bool setShape(const TopoDS_Shape &shape);

    // Диагностика переноса: точки отрезков подряд парами (x, y, z), float32.
    // Порядок — порядок построения, тот же, что у эталонного массива.
    QVector<float> segmentPoints() const { return m_points; }

signals:
    void sourceChanged();
    void deflectionChanged();
    void hideFormEdgesChanged();

private:
    void rebuild();                       // source -> буферы геометрии

    QString m_source;
    TopoDS_Shape m_shape;                 // разобранный STEP; пуста до первого чтения
    qreal m_deflection = 0.05;            // мм
    bool m_hideFormEdges = true;
    int m_segmentCount = 0;
    int m_rawSegmentCount = 0;
    QVector<float> m_points;              // диагностика и буфер вершин
};
