#include "stepledgesgeometry.h"

#include <algorithm>
#include <array>
#include <exception>
#include <vector>

#include <QFile>
#include <QFileInfo>
#include <QVector3D>

// Отбор и склейка рёбер (обход TopAbs_EDGE, GCPnts_UniformDeflection, шовные
// линии) вынесены в gvstep: тот же алгоритм нужен C-ABI-мосту gvbridge без Qt.
#include "occt/step_edges.h"

// Чтение STEP - общее ядро (occt/step_import): свой разбор здесь был бы второй
// редакцией того же пути.
#include "occt/step_import.h"

StepEdgesGeometry::StepEdgesGeometry(QQuick3DObject *parent)
    : QQuick3DGeometry(parent)
{
}

void StepEdgesGeometry::setSource(const QString &path)
{
    if (m_source == path)
        return;
    m_source = path;
    m_shape = TopoDS_Shape();             // другой файл - кэш формы не годится
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
    m_points.clear();
    m_segmentCount = 0;
    m_rawSegmentCount = 0;

    const std::vector<gv::EdgeSegment> raw =
        gv::stepRawSegments(shape, double(m_deflection), m_hideFormEdges);
    m_rawSegmentCount = int(raw.size());
    const std::vector<gv::EdgeSegment> merged = gv::coalesceStepSegments(raw);
    m_segmentCount = int(merged.size());
    if (merged.empty()) {
        update();
        return false;
    }

    m_points.reserve(m_segmentCount * 6);
    float lo[3] = { 1e18f, 1e18f, 1e18f };
    float hi[3] = { -1e18f, -1e18f, -1e18f };
    for (const gv::EdgeSegment &s : merged) {
        for (int e = 0; e < 2; ++e) {
            const std::array<double, 3> &p = e ? s.b : s.a;
            for (int k = 0; k < 3; ++k) {
                const float v = float(p[k]);
                m_points.append(v);                  // пара точек: LINES
                lo[k] = std::min(lo[k], v);
                hi[k] = std::max(hi[k], v);
            }
        }
    }

    setPrimitiveType(PrimitiveType::Lines);              // семантика GL_LINES
    setStride(3 * sizeof(float));
    setVertexData(QByteArray(reinterpret_cast<const char *>(m_points.constData()),
                             int(m_points.size() * sizeof(float))));
    addAttribute(Attribute::Semantic::PositionSemantic, 0,
                 Attribute::ComponentType::F32Type);
    setBounds(QVector3D(lo[0], lo[1], lo[2]), QVector3D(hi[0], hi[1], hi[2]));
    update();
    return true;
}

void StepEdgesGeometry::rebuild()
{
    clear();
    m_points.clear();
    m_segmentCount = 0;
    m_rawSegmentCount = 0;
    if (m_source.isEmpty() || !QFileInfo::exists(m_source)) {
        update();
        return;
    }

    // Форма читается один раз и живёт в объекте. Смена deflection или
    // hideFormEdges пересчитывает рёбра, а STEP заново не читается: на
    // Test_model.STEP повторное чтение - 466 мс из 550 мс на смену свойства.
    if (m_shape.IsNull()) {
        try {
            // OCCT принимает путь в UTF-8: кириллица в пути иначе теряется.
            m_shape = gv::readStepShape(m_source.toUtf8().constData());
        } catch (const std::exception &e) {
            qWarning("StepEdgesGeometry: %s", e.what());
            update();
            return;
        }
    }
    setShape(m_shape);
}
