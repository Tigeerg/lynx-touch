// occt/step_edges.h - рёбра B-Rep формы STEP как отрезки. Слой без Qt: тот же
// алгоритм нужен и QML-плагину CNC.Edges (cpp/stepledgesgeometry.cpp), и мосту
// gvbridge (cpp/bridge), поэтому он живёт в gvstep одной редакцией.
//
// Повтор питоновского эталона load_step_model(...) по рёбрам
// (occt/step_import.cpp): обход TopAbs_EDGE в порядке
// TopExp_Explorer, полилиния каждого ребра - GCPnts_UniformDeflection с линейным
// допуском 0.05 мм (угловой критерий не используется: эталон его не задаёт),
// затем склейка почти совпадающих отрезков (_coalesce_segments, core/packing.cpp).
// Шовные линии криволинейных граней (прямое ребро, у которого все смежные грани
// криволинейны) отбрасываются: SolidWorks их тоже не рисует.
//
// Совпадение с эталоном проверяет cpp/stepledges_probe.cpp и
// (сегменты до склейки на
// Test_model: 4884, после склейки 2426).
#pragma once

#include <array>
#include <vector>

#include <TopoDS_Shape.hxx>

namespace gv {

// Отрезок обводки в координатах модели, мм.
struct EdgeSegment {
    std::array<double, 3> a{};
    std::array<double, 3> b{};
};

// Рёбра формы до склейки: порядок обхода и порядок точек внутри ребра - часть
// контракта с эталоном, менять нельзя. dropSeams снимает шовные линии
// криволинейных граней (у эталона выключение не предусмотрено).
std::vector<EdgeSegment> stepRawSegments(const TopoDS_Shape &shape,
                                         double deflection = 0.05,
                                         bool dropSeams = true);

// Склейка почти совпадающих прямых отрезков: корзины по направлению, внутри
// корзины - кластеры по расстоянию от перпендикуляра (сетка distTol мм), осевые
// интервалы кластера объединяются. Значения по умолчанию - как у эталона.
std::vector<EdgeSegment> coalesceStepSegments(const std::vector<EdgeSegment> &segments,
                                              double distTol = 0.12,
                                              double gapTol = 0.5);

} // namespace gv
