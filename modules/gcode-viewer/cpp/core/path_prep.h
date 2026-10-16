// core/path_prep.h - подготовка траектории к отрисовке: потоки вершин, этапы,
// габарит (зеркало path_prep.py).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "gv_types.h"

namespace gv {

// Габарит в координатах сцены: минимум и максимум по трём осям, мм.
struct Aabb {
    std::array<double, 3> lo{0.0, 0.0, 0.0};
    std::array<double, 3> hi{0.0, 0.0, 0.0};
};

// Этап, приведённый к долям пути: диапазон [start, end] по длине траектории.
struct StageRange {
    double start = 0.0;
    double end = 1.0;
    std::string label;
    ToolInfo info;
};

// Буфер одного потока линий: float32 (позиция 3 + параметр пути), как на GPU.
struct PackedStream {
    std::vector<unsigned char> bytes;
    int nv = 0;                 // число вершин (2 на отрезок)
};

// Подготовленная траектория (зеркало PathData из gv_types.py): упакованные
// буферы обоих потоков, полилиния анимации в координатах сцены и её метрики.
struct PathData {
    int seg_count = 0;
    std::array<double, 3> last{0.0, 0.0, 0.0};
    std::vector<double> pts;              // сцена (X, Z, -Y), N*3
    std::vector<double> cum;              // накопленная длина, N
    std::vector<unsigned char> rapid;     // флаг ИСХОДЯЩЕГО сегмента, N (1/0)
    std::vector<double> speeds;           // скорость ИСХОДЯЩЕГО сегмента, N
    double total = 0.0;
    PackedStream rapid_stream;
    PackedStream feed_stream;
    bool has_bounds = false;
    Aabb bounds;
    std::array<double, 3> center{0.0, 0.0, 0.0};
    double radius = 50.0;
    double min_y = 0.0;
    std::vector<StageRange> stages;
};

// Машинный габарит -> габарит сцены (X, Z, -Y).
Aabb sceneBoundsLoHi(const std::array<double, 3>& lo, const std::array<double, 3>& hi);

// Упаковка одного потока линий: mask по флагу исходящего сегмента, координаты
// при machine=true переводятся в сцену.
PackedStream packStream(const std::vector<double>& pts, const std::vector<unsigned char>& flags,
                        const std::vector<double>& cum, double total, bool wantRapid,
                        bool machine = true);

// Этапы (индекс первой точки, подпись) -> диапазоны [start, end] по длине пути.
std::vector<StageRange> stagesNorm(const std::vector<Stage>& stages,
                                   const std::vector<double>& cum, double total);

// Разбор пути в буферы и метрики. Тяжёлая часть загрузки: может считаться на
// рабочем потоке, объектов Qt не создаёт.
PathData prepareOrdered(const std::vector<PathPoint>& ordered,
                        const std::vector<Stage>& stages);

} // namespace gv
