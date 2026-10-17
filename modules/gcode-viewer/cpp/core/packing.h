// core/packing.h - упаковка отрезков в Lines-буфер.
//
// Раскладка буфера - контракт с шейдером обводки: на вершину 5 чисел float32
// (позиция 3f + texcoord 2f: видимость 1.0/0.0), то есть stride 20 байт и две
// вершины на отрезок. Ядро сверяется с питоновским эталоном побайтово
// (модули gcode-viewer/), поэтому порядок вершин и число
// набивки - часть результата, а не оформление.
#pragma once

#include <vector>

#include "core/path_prep.h"     // gv::Aabb - габарит, как у _pack_edges_np

namespace gv {

// Упакованный Lines-буфер отрезков: байты вершин, их число и габарит
// (зеркало PackedEdges из core/gv_types.h). `has_bounds` = false у пустого буфера:
// в Python габарит бывает None.
struct PackedEdges {
    std::vector<unsigned char> raw;
    int nv = 0;
    bool has_bounds = false;
    Aabb bounds;
};

// Зеркало _pack_edges_np: концы отрезков p0 и p1, по три координаты float64 на
// отрезок, в буфер float32. Габарит считается по float64-входу, как в эталоне
// (float32-буфер на него не влияет).
PackedEdges packEdgesNp(const std::vector<double>& p0, const std::vector<double>& p1);

// Зеркало packed_positions: концы отрезков из буфера как (M, 2, 3) float32,
// плоским списком по 6 чисел на отрезок. Пустой буфер -> пустой список.
std::vector<float> packedPositions(const PackedEdges& packed);

} // namespace gv
