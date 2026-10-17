// core/silhouette.h - топология меша для силуэта и силуэт на глазу: зеркало
// modules/gcode-viewer/silhouette.py.
//
// Слой чистый: ни Qt, ни OCCT - только арифметика над супом вершин, как у
// normals и packing. Поэтому он живёт в gvcore, а не в OCCT-цели: топология
// строится по супу, откуда бы суп ни пришёл (STEP - этап 4a, STL или меш кожи).
#pragma once

#include <cstdint>
#include <vector>

#include "core/quality.h"     // gv::QualityPreset - допуски сварки и отбора кандидатов

namespace gv {

// Топология меша для силуэта (MeshTopology в gv_types.py).
//
// ea/eb (K,3) float32 - концы кандидатных силуэтных рёбер, f0/f1 (K,) int32 -
// смежные грани (f0 < f1), n (F,3) float32 - нормали граней, d (F,) float32 -
// предвычисленное n·cen (центроиды граней): на кадре остаётся знаковый тест
// s = n·E - d. Кандидаты - только рёбра гладкого горизонта: острые переходы
// (круче 20 градусов) и вырожденные грани отсеяны, см. silhouette.cpp.
struct MeshTopology {
    std::vector<float> ea;
    std::vector<float> eb;
    std::vector<std::int32_t> f0;
    std::vector<std::int32_t> f1;
    std::vector<float> n;
    std::vector<float> d;
    std::size_t faces = 0;

    std::size_t edgeCount() const { return f0.size(); }
};

// Топология по супу вершин (F*9 double: по три вершины на треугольник, плоский
// ряд, как у load_step_model и load_stl_vertices).
//
// Повтор _build_silhouette_topology бит-в-бит, включая порядок операций:
//   * сварка вершин - по сетке weld (np.round(V / weld), округление
//     пол-к-чётному), кластеры и их номера - в лексикографическом порядке
//     ключей, как np.unique(axis=0);
//   * рёбра треугольников (a,b) с a < b, группы ровно по двум граням -
//     кандидаты в порядке лексикографической сортировки ключей;
//   * нормали и площади - по самим треугольникам (сглаживание размыло бы
//     горизонт), d - n·cen в float32 тем же порядком слагаемых, что numpy.
// Допуски берутся из пресета (core/quality.h): у уровня med они те же, что
// были у жёстко заданных чисел.
MeshTopology buildSilhouetteTopology(const std::vector<double>& soup,
                                     const QualityPreset& quality = QualityPreset{});

// Силуэтные рёбра на глазу eyeMm (raw-кадр модели): (K,2,3) float32 плоским
// рядом - концы кандидатов в порядке топологии; пустой вектор, если силуэта нет.
// Ребро силуэтное, когда смежные грани лежат по разные стороны от глаза
// (знак s у них разный): ребро разделяет видимую и невидимую грань.
std::vector<float> meshSilhouetteSegments(const MeshTopology& topo, const double eyeMm[3]);

// Буферы кадрового силуэта, переживающие кадр: у Test_model на кадр приходилось
// 20 выделений и 63 КБ, из них 45 КБ - знак по граням. Ёмкости здесь, поэтому на
// кадр остаётся заполнение, а не заведение буферов.
struct MeshSilhouetteScratch {
    std::vector<std::int8_t> sign;   // (F,) знак s = n*E - d по граням
    std::vector<float> out;          // (K,2,3) float32 плоским рядом
};

// Силуэт в переданный буфер: scratch.out очищается и заполняется тем же порядком
// операций, что и у версии выше (числа те же бит-в-бит); возврат - ссылка на него.
const std::vector<float>& meshSilhouetteSegments(const MeshTopology& topo,
                                                 const double eyeMm[3],
                                                 MeshSilhouetteScratch& scratch);

} // namespace gv
