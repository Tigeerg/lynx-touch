// core/outline.h - обводка меша: рёбра по двугранному углу.
//
// Отобранные рёбра склеиваются в цепочки смежности и упрощаются по
// Дугласу-Пекеру: у мелкого меша (медиана ребра Test_model.STL - 0.07 мм) контур
// иначе превращается в облако субмиллиметровых звеньев. Перенос сверяется с
// питоновским _feature_edges_np побайтово, поэтому порядок цепочек и упрощения
// здесь - часть результата.
#pragma once

#include <vector>

#include "core/packing.h"

namespace gv {

// Допуск упрощения цепочек контура, мм (зеркало _FEATURE_SIMPLIFY_MM).
constexpr double FEATURE_SIMPLIFY_MM = 0.15;

// Зеркало _feature_edges_np: суп треугольников (9 координат float64 на
// треугольник) -> упакованный Lines-буфер рёбер. Остаются рёбра границы, рёбра
// с числом граней больше двух и рёбра с двугранным углом больше angle_deg.
// simplifyMm - допуск упрощения цепочек (у уровня med - FEATURE_SIMPLIFY_MM).
PackedEdges featureEdgesNp(const std::vector<double>& soup, double angle_deg,
                           double simplifyMm = FEATURE_SIMPLIFY_MM);

} // namespace gv
